#define _POSIX_C_SOURCE 200809L

#include "benchmark.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum {
    WARMUP_PASSES = 5,
    MIN_READS = 250003,
};

static volatile size_t last_offset;

double now_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)now.tv_sec * 1e9 + (double)now.tv_nsec;
}

void shuffle(size_t *order, size_t count)
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
size_t build_ring(unsigned char *buffer, size_t *order,
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

double median_ns(const double samples[RUN_COUNT])
{
    double sorted[RUN_COUNT];
    for (size_t run = 0; run < RUN_COUNT; ++run) {
        sorted[run] = samples[run];
    }
    qsort(sorted, RUN_COUNT, sizeof(sorted[0]), compare_ns);
    return sorted[RUN_COUNT / 2];
}

void print_sample(const char *test, size_t x, const double samples[RUN_COUNT])
{
    printf("%s,%zu,%.3f", test, x, median_ns(samples));
    for (size_t run = 0; run < RUN_COUNT; ++run) {
        printf(",%.3f", samples[run]);
    }
    putchar('\n');
}

void measure(const char *test, size_t x, const unsigned char *buffer,
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
