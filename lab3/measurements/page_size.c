#define _GNU_SOURCE

#include "benchmark.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

enum {
    PAGE_PROBE_BYTES = MIB,
    PAGE_PROBE_STEP = 64,
    PAGE_PROBE_POINTS = PAGE_PROBE_BYTES / PAGE_PROBE_STEP,
};

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

size_t measure_page_bytes(void)
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
