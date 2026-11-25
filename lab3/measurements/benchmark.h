#ifndef CACHE_BENCHMARK_H
#define CACHE_BENCHMARK_H

#include <stddef.h>

enum {
    KIB = 1024,
    MIB = 1024 * KIB,
    MAX_BYTES = 64 * MIB,
    RUN_COUNT = 5,
    MAX_CONFLICT_LINES = 20,
};

double now_ns(void);
void shuffle(size_t *order, size_t count);
size_t build_ring(unsigned char *buffer, size_t *order,
                  size_t bytes, size_t block_bytes, size_t stride);
double median_ns(const double samples[RUN_COUNT]);
void print_sample(const char *test, size_t x, const double samples[RUN_COUNT]);
void measure(const char *test, size_t x, const unsigned char *buffer,
             size_t start, size_t cells);

size_t measure_page_bytes(void);
void measure_line_stride(unsigned char *buffer, size_t *order);
void measure_working_set(unsigned char *buffer, size_t *order, size_t line_bytes);
void measure_l1_associativity(unsigned char *buffer, size_t line_bytes, size_t page_bytes);

#endif
