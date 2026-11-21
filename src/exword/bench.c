#include "bench.h"

#include <gint/clock.h>
#include <gint/display.h>
#include <gint/prof.h>

/* Limit the footprint: EX-word user RAM is tight and reserved by the OS. */
static volatile uint32_t ram_area[256];
static volatile uint32_t sink;

/* Deliberately use volatile loads to measure ROM rather than precomputed
   constants. These values live in this add-in's own read-only image. */
static const uint32_t rom_area[64] = {
    0x9E3779B9,0x7F4A7C15,0xC2B2AE35,0x165667B1,
    0xDEADBEEF,0x31415926,0x27182818,0xA5A5A5A5,
    0x01234567,0x89ABCDEF,0x13579BDF,0x2468ACE0,
    0xFEDCBA98,0x10293847,0x55667788,0x11223344,
    0xBADC0FFE,0x68E31DA4,0xC6EF3720,0xFF00FF00,
    0x55AA55AA,0x5555AAAA,0x0F0F0F0F,0xF0F0F0F0,
    0xCAB005E5,0x07F7A12E,0xF00DBABE,0xA1B2C3D4,
    0x13A5C029,0xF1EA5EED,0x02468ACE,0xECA86420,
    0xA23B49C5,0x49B57A11,0x4D5E6F70,0x8E9FA0B1,
    0x102A3040,0xF0E0D0C0,0x6F3C2D1E,0x6ABC345F,
    0x1122AABB,0x88997766,0xC001D00D,0xD15EA5E5,
    0xFACEC0DE,0x0A0B0C0D,0x3141F00D,0x99118822,
    0x00FF11EE,0x22DD33CC,0x44BB55AA,0x66778899,
    0xC3D2E1F0,0x1234ABCD,0x66AA55FF,0xFE01DC23,
    0xBC34AB56,0xCD78EF90,0xAABBCBDD,0xEF012345,
    0x44556677,0x9ABCDEFF,0x19283746,0xFFEEDDCC,
};
static bool profiler_started;

static uint32_t finish(prof_t p)
{
    uint32_t us = prof_time(p);
    /* Detect a non-functional timer instead of reporting infinite speed. */
    return us ? us : 1;
}

bool extune_bench_run(extune_bench_t *out)
{
    *out = (extune_bench_t){ 0 };
    if(!profiler_started) {
        /* Profiling requires a peripheral clock and one free TMU. */
        if(clock_freq()->Pphi_f <= 0 || prof_init() != 0) return false;
        profiler_started = true;
    }

    uint32_t acc = 0x12345678u;
    prof_t p = prof_make();
    prof_enter(p);
    for(uint32_t i = 0; i < 100000; i++)
        acc = (acc << 5) ^ (acc >> 2) ^ (i * 2654435761u);
    prof_leave(p);
    out->cpu_us = finish(p);
    sink = acc;

    /* ROM: 64 KiB of volatile reads from the add-in's own image. */
    volatile const uint32_t *source = rom_area;
    p = prof_make();
    prof_enter(p);
    for(unsigned i = 0; i < 16384; i++)
        acc = (acc << 1) ^ source[i & 63u];
    prof_leave(p);
    out->rom_us = finish(p);

    /* RAM: 128 KiB of verified program-owned writes plus readbacks. */
    p = prof_make();
    prof_enter(p);
    for(unsigned i = 0; i < 32768; i++) {
        unsigned index = i & 255u;
        ram_area[index] = (i * 1664525u) ^ acc;
        acc ^= ram_area[index];
    }
    prof_leave(p);
    out->ram_us = finish(p);
    out->checksum = sink = acc;

    /* One real display-transfer duration, not an artificial FPS estimate. */
    p = prof_make();
    prof_enter(p);
    dupdate();
    prof_leave(p);
    out->display_us = finish(p);
    out->done = true;
    return true;
}

static uint32_t pattern(uint32_t i, uint32_t pass)
{
    switch(pass & 7u) {
        case 0: return 0x00000000;
        case 1: return 0xffffffff;
        case 2: return 0x55555555;
        case 3: return 0xaaaaaaaa;
        case 4: return (1u << (i & 31u));
        case 5: return ~(1u << (i & 31u));
        case 6: return i * 0x9e3779b9u;
        default: return ~(i * 0x9e3779b9u);
    }
}

void extune_memtest_run(extune_memtest_t *out, unsigned passes)
{
    *out = (extune_memtest_t){ 0 };
    if(passes > 256) passes = 256;
    for(unsigned p = 0; p < passes; p++) {
        for(unsigned i = 0; i < 256; i++)
            ram_area[i] = pattern(i, p);
        for(unsigned i = 0; i < 256; i++) {
            uint32_t expected = pattern(i, p);
            uint32_t actual = ram_area[i];
            if(actual != expected) {
                out->errors++;
                out->failing_index = i;
                out->expected = expected;
                out->actual = actual;
                out->done = true;
                return;
            }
        }
        out->passes++;
    }
    out->done = true;
}

void extune_bench_quit(void)
{
    if(profiler_started) prof_quit();
    profiler_started = false;
}
