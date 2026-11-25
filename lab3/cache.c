#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

enum {
    KIB = 1024,
    MIB = 1024 * KIB,
    MAX_BYTES = 64 * MIB,
    LINE_TEST_BLOCK_BYTES = 512,
    RUN_COUNT = 5,
    WARMUP_PASSES = 5,
    MIN_READS = 250003,
    MAX_CONFLICT_LINES = 20,
    PAGE_PROBE_BYTES = MIB,
    PAGE_PROBE_STEP = 64,
    PAGE_PROBE_POINTS = PAGE_PROBE_BYTES / PAGE_PROBE_STEP,
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

/*
    - обходим блоки в случайном порядке, а ячейки внутри блока — с шагом stride.
    - блок – непрывный участок из общего буфера
    - внутри блока адреса с регулярным шагом
    - сами блоки перемещиваются, чтобы сложнее было предугадать след блок
*/
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
        // и соответственно пишем в память
        *(size_t *)(buffer + offset) = order[(i + 1) % blocks] * block_bytes;
    }
    return order[0] * block_bytes;
}

/*
   смотрим, чтолько кэш-лтний в одном наборе L1

*/
static size_t build_l1_ring(unsigned char *buffer, size_t lines,
                            size_t line_bytes, size_t page_bytes, int same_set)
{
    size_t order[MAX_CONFLICT_LINES]; // MAX_CONFLICT_LINES - сколько макс кэш-линий проеряем
    size_t offsets[MAX_CONFLICT_LINES];
    shuffle(order, lines);

    for (size_t i = 0; i < lines; ++i) {
        size_t page = order[i];
        offsets[i] = page * page_bytes + (same_set ? 0 : page * line_bytes);
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

static double median_ns(const double samples[RUN_COUNT])
{
    double sorted[RUN_COUNT];
    for (size_t run = 0; run < RUN_COUNT; ++run) {
        sorted[run] = samples[run];
    }
    qsort(sorted, RUN_COUNT, sizeof(sorted[0]), compare_ns);
    return sorted[RUN_COUNT / 2];
}

static void print_sample(const char *test, size_t x, const double samples[RUN_COUNT])
{
    printf("%s,%zu,%.3f", test, x, median_ns(samples));
    for (size_t run = 0; run < RUN_COUNT; ++run) {
        printf(",%.3f", samples[run]);
    }
    putchar('\n');
}

static size_t find_page_period(const double medians[PAGE_PROBE_POINTS])
{
    for (size_t period = 2; period <= PAGE_PROBE_POINTS / 16; period *= 2) {
        size_t groups = PAGE_PROBE_POINTS / period;
        size_t peaks = 0;

        // Сравниваем начало и середину каждой предполагаемой страницы.
        for (size_t point = 0; point < PAGE_PROBE_POINTS; point += period) {
            if (medians[point] > 1.5 * medians[point + period / 2]) {
                ++peaks;
            }
        }
        if (peaks * 8 >= groups * 7) {
            return period * PAGE_PROBE_STEP;
        }
    }
    return 0;
}

static size_t measure_page_bytes(void)
{
    double (*samples)[RUN_COUNT] = malloc(PAGE_PROBE_POINTS * sizeof(*samples));
    if (samples == NULL) {
        perror("Замер размера страницы: malloc");
        exit(EXIT_FAILURE);
    }

    for (size_t run = 0; run < RUN_COUNT; ++run) {
        void *mapping = mmap(NULL, PAGE_PROBE_BYTES, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            perror("Замер размера страницы: mmap");
            exit(EXIT_FAILURE);
        }
#ifdef MADV_NOHUGEPAGE
        if (madvise(mapping, PAGE_PROBE_BYTES, MADV_NOHUGEPAGE) != 0) {
            perror("Замер размера страницы: madvise");
            exit(EXIT_FAILURE);
        }
#endif
        volatile unsigned char *probe = mapping;
        for (size_t point = 0; point < PAGE_PROBE_POINTS; ++point) {
            double begin = now_ns();
            probe[point * PAGE_PROBE_STEP] = 1;
            samples[point][run] = now_ns() - begin;
        }
        if (munmap(mapping, PAGE_PROBE_BYTES) != 0) {
            perror("Замер размера страницы: munmap");
            exit(EXIT_FAILURE);
        }
    }

    double medians[PAGE_PROBE_POINTS];
    for (size_t point = 0; point < PAGE_PROBE_POINTS; ++point) {
        medians[point] = median_ns(samples[point]);
    }
    size_t page_bytes = find_page_period(medians);
    if (page_bytes == 0) {
        fprintf(stderr, "Не удалось найти период пиков первой записи.\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "Размер страницы по замеру: %zu байт.\n", page_bytes);
    for (size_t point = 0; point < 8 * page_bytes / PAGE_PROBE_STEP; ++point) {
        print_sample("page_first_write_bytes", point * PAGE_PROBE_STEP, samples[point]);
    }
    fflush(stdout);
    free(samples);
    return page_bytes;
}

static void measure(const char *test, size_t x, const unsigned char *buffer,
                    size_t start, size_t cells)
{
    double samples[RUN_COUNT];
    size_t reads = cells > MIN_READS ? 2 * cells + 1 : MIN_READS;

    for (size_t run = 0; run < RUN_COUNT; ++run) {
        walk(buffer, start, WARMUP_PASSES * cells);
        double begin = now_ns();
        walk(buffer, start, reads);
        samples[run] = (now_ns() - begin) / (double)reads;
    }
    print_sample(test, x, samples);
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
    puts("test,x,median_ns,run1_ns,run2_ns,run3_ns,run4_ns,run5_ns");
    size_t page_bytes = measure_page_bytes();
    if (page_bytes < MAX_CONFLICT_LINES * line_bytes) {
        fprintf(stderr, "Страница слишком мала для контрольного замера L1.\n");
        return EXIT_FAILURE;
    }

    void *memory;
    if (posix_memalign(&memory, page_bytes, MAX_BYTES) != 0) {
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
        start = build_l1_ring(buffer, lines, line_bytes, page_bytes, 1);
        measure("l1_conflict_lines", lines, buffer, start, lines);
        start = build_l1_ring(buffer, lines, line_bytes, page_bytes, 0);
        measure("l1_control_lines", lines, buffer, start, lines);
    }

    free(order);
    free(buffer);
    return EXIT_SUCCESS;
}
