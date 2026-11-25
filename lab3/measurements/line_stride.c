#include "benchmark.h"

enum { LINE_TEST_BLOCK_BYTES = 512 };

void measure_line_stride(unsigned char *buffer, size_t *order)
{
    for (size_t stride = sizeof(size_t); stride <= 256; stride *= 2) {
        size_t start = build_ring(buffer, order, MAX_BYTES, LINE_TEST_BLOCK_BYTES, stride);
        measure("line_stride_bytes", stride, buffer, start, MAX_BYTES / stride);
    }
}
