#define _POSIX_C_SOURCE 200809L

#include "measurements/benchmark.h"

#include <stdio.h>
#include <stdlib.h>

enum { CACHE_LINE_BYTES = 64 };

int main(void)
{
    puts("test,x,median_ns,run1_ns,run2_ns,run3_ns,run4_ns,run5_ns");
    size_t page_bytes = measure_page_bytes();
    if (page_bytes < MAX_CONFLICT_LINES * CACHE_LINE_BYTES) {
        fprintf(stderr, "Страница слишком мала для контрольного замера L1.\n");
        return EXIT_FAILURE;
    }

    void *memory;
    if (posix_memalign(&memory, page_bytes, MAX_BYTES) != 0) {
        fprintf(stderr, "Не удалось выделить буфер для замеров.\n");
        return EXIT_FAILURE;
    }
    unsigned char *buffer = memory;
    size_t *order = malloc((MAX_BYTES / CACHE_LINE_BYTES) * sizeof(*order));
    if (order == NULL) {
        fprintf(stderr, "Не удалось выделить массив порядка обхода.\n");
        free(buffer);
        return EXIT_FAILURE;
    }

    measure_line_stride(buffer, order);
    measure_working_set(buffer, order, CACHE_LINE_BYTES);
    measure_l1_associativity(buffer, CACHE_LINE_BYTES, page_bytes);

    free(order);
    free(buffer);
    return EXIT_SUCCESS;
}
