#include "benchmark.h"

void measure_working_set(unsigned char *buffer, size_t *order, size_t line_bytes)
{
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
}
