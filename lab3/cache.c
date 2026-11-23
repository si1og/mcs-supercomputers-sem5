#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum {
    KIB = 1024,
    MIB = 1024 * KIB,
    MAX_BYTES = 64 * MIB,
    PAGE_BYTES = 4 * KIB,
    LINE_TEST_BLOCK_BYTES = 512,
    RUN_COUNT = 5,
    WARMUP_PASSES = 5,
    MIN_READS = 250003,
    MAX_CONFLICT_LINES = 20,
};

static volatile size_t last_offset;

static double now_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)now.tv_sec * 1e9 + (double)now.tv_nsec;
}

static void shuffle(size_t *order, size_t count)
{
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);

    for (size_t i = 0; i < count; ++i) {
        order[i] = i;
    }
    for (size_t i = count; i > 1; --i) {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        size_t j = (size_t)((state * UINT64_C(2685821657736338717)) % i);
        size_t value = order[i - 1];
        order[i - 1] = order[j];
        order[j] = value;
    }
}

/* Обходим блоки в случайном порядке, а ячейки внутри блока — с шагом stride. */
static size_t build_ring(unsigned char *buffer, size_t *order,
                         size_t bytes, size_t block_bytes, size_t stride)
{
    size_t blocks = bytes / block_bytes;
    shuffle(order, blocks);

    for (size_t i = 0; i < blocks; ++i) {
        size_t offset = order[i] * block_bytes;
        for (size_t position = stride; position < block_bytes; position += stride) {
            *(size_t *)(buffer + offset) = offset + stride;
            offset += stride;
        }
        *(size_t *)(buffer + offset) = order[(i + 1) % blocks] * block_bytes;
    }
    return order[0] * block_bytes;
}

/* На Acer шаг в 4 КиБ сохраняет индекс набора L1d. В контроле сдвигаем строку. */
static size_t build_l1_ring(unsigned char *buffer, size_t lines,
                            size_t line_bytes, int same_set)
{
    size_t order[MAX_CONFLICT_LINES];
    size_t offsets[MAX_CONFLICT_LINES];
    shuffle(order, lines);

    for (size_t i = 0; i < lines; ++i) {
        size_t page = order[i];
        offsets[i] = page * PAGE_BYTES + (same_set ? 0 : page * line_bytes);
    }
    for (size_t i = 0; i < lines; ++i) {
        *(size_t *)(buffer + offsets[i]) = offsets[(i + 1) % lines];
    }
    return offsets[0];
}

static void walk(const unsigned char *buffer, size_t offset, size_t reads)
{
    for (size_t i = 0; i < reads; ++i) {
        offset = *(const size_t *)(buffer + offset);
    }
    last_offset = offset;
}

static int compare_ns(const void *left, const void *right)
{
    double a = *(const double *)left;
    double b = *(const double *)right;
    return (a > b) - (a < b);
}

static void measure(const char *test, size_t x, const unsigned char *buffer,
                    size_t start, size_t cells)
{
    double samples[RUN_COUNT];
    double sorted[RUN_COUNT];
    size_t reads = cells > MIN_READS ? 2 * cells + 1 : MIN_READS;

    for (size_t run = 0; run < RUN_COUNT; ++run) {
        walk(buffer, start, WARMUP_PASSES * cells);
        double begin = now_ns();
        walk(buffer, start, reads);
        samples[run] = (now_ns() - begin) / (double)reads;
        sorted[run] = samples[run];
    }
    qsort(sorted, RUN_COUNT, sizeof(sorted[0]), compare_ns);

    printf("%s,%zu,%.3f", test, x, sorted[RUN_COUNT / 2]);
    for (size_t run = 0; run < RUN_COUNT; ++run) {
        printf(",%.3f", samples[run]);
    }
    putchar('\n');
    fflush(stdout);
}

int main(int argc, char **argv)
{
    size_t line_bytes = 64;
    if (argc == 2) {
        char *end;
        unsigned long value = strtoul(argv[1], &end, 10);
        if (end != argv[1] && *end == '\0' &&
            (value == 16 || value == 32 || value == 64 || value == 128)) {
            line_bytes = value;
        } else {
            fprintf(stderr, "Размер строки должен быть 16, 32, 64 или 128 байт.\n");
            return EXIT_FAILURE;
        }
    } else if (argc > 2) {
        fprintf(stderr, "Использование: %s [размер_строки_в_байтах]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (sysconf(_SC_PAGESIZE) != PAGE_BYTES) {
        fprintf(stderr, "Замер ассоциативности рассчитан на страницы по 4 КиБ.\n");
        return EXIT_FAILURE;
    }

    void *memory;
    if (posix_memalign(&memory, PAGE_BYTES, MAX_BYTES) != 0) {
        fprintf(stderr, "Не удалось выделить буфер для замеров.\n");
        return EXIT_FAILURE;
    }
    unsigned char *buffer = memory;
    size_t *order = malloc((MAX_BYTES / line_bytes) * sizeof(*order));
    if (order == NULL) {
        fprintf(stderr, "Не удалось выделить массив порядка обхода.\n");
        free(buffer);
        return EXIT_FAILURE;
    }

    puts("test,x,median_ns,run1_ns,run2_ns,run3_ns,run4_ns,run5_ns");

    for (size_t stride = sizeof(size_t); stride <= 256; stride *= 2) {
        size_t start = build_ring(buffer, order, MAX_BYTES, LINE_TEST_BLOCK_BYTES, stride);
        measure("line_stride_bytes", stride, buffer, start, MAX_BYTES / stride);
    }

    /* Четыре точки между соседними степенями двойки, начиная с 4 КиБ. */
    for (size_t base = 4 * KIB; base < MAX_BYTES; base *= 2) {
        for (size_t part = 0; part < 4; ++part) {
            size_t size = base + part * (base / 4);
            size_t start = build_ring(buffer, order, size, line_bytes, line_bytes);
            measure("working_set_bytes", size, buffer, start, size / line_bytes);
        }
    }
    size_t start = build_ring(buffer, order, MAX_BYTES, line_bytes, line_bytes);
    measure("working_set_bytes", MAX_BYTES, buffer, start, MAX_BYTES / line_bytes);

    for (size_t lines = 2; lines <= MAX_CONFLICT_LINES; ++lines) {
        start = build_l1_ring(buffer, lines, line_bytes, 1);
        measure("l1_conflict_lines", lines, buffer, start, lines);
        start = build_l1_ring(buffer, lines, line_bytes, 0);
        measure("l1_control_lines", lines, buffer, start, lines);
    }

    free(order);
    free(buffer);
    return EXIT_SUCCESS;
}
