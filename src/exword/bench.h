#ifndef EXTUNE_EXWORD_BENCH_H
#define EXTUNE_EXWORD_BENCH_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t cpu_us;
    uint32_t rom_us;
    uint32_t ram_us;
    uint32_t display_us;
    uint32_t checksum;
    bool done;
} extune_bench_t;

typedef struct {
    uint32_t passes;
    uint32_t errors;
    uint32_t failing_index;
    uint32_t expected;
    uint32_t actual;
    bool done;
} extune_memtest_t;

/* Uses only program-owned 1 KiB RAM, the program's const ROM and the
   existing gint display buffer. No OS storage or system RAM probing. */
bool extune_bench_run(extune_bench_t *result);
void extune_memtest_run(extune_memtest_t *result, unsigned passes);
void extune_bench_quit(void);

#endif
