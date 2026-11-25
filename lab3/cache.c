#define _POSIX_C_SOURCE 200809L

#include "measurements/benchmark.h"

#include <stdio.h>
#include <stdlib.h>

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

    measure_line_stride(buffer, order);
    measure_working_set(buffer, order, line_bytes);
    measure_l1_associativity(buffer, line_bytes, page_bytes);

    free(order);
    free(buffer);
    return EXIT_SUCCESS;
}
