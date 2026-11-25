#include "benchmark.h"

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

void measure_l1_associativity(unsigned char *buffer, size_t line_bytes, size_t page_bytes)
{
    for (size_t lines = 2; lines <= MAX_CONFLICT_LINES; ++lines) {
        size_t start = build_l1_ring(buffer, lines, line_bytes, page_bytes, 1);
        measure("l1_conflict_lines", lines, buffer, start, lines);
        start = build_l1_ring(buffer, lines, line_bytes, page_bytes, 0);
        measure("l1_control_lines", lines, buffer, start, lines);
    }
}
