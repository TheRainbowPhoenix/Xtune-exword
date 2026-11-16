/*
 * EXTune: initial EX-word port of Xtune/Ptune4.
 *
 * Clock and BSC readings are intentionally read-only. The existing Ptune
 * profiles and wait-state calculations are specific to calculator hardware
 * and must NOT be applied to EX-word units without board-specific validation.
 */
#include <gint/config.h>
#include <gint/clock.h>
#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/prof.h>
#include <justui/jscene.h>
#include <justui/jlabel.h>
#include <justui/jwidget.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#if !GINT_HW_EX
#error "EXTune must be built with the EXWORD toolchain"
#endif

enum { PAGE_CLOCKS, PAGE_REGISTERS, PAGE_BENCH, PAGE_ABOUT, PAGE_COUNT };
static const char *const titles[] = {
    "Clock frequencies", "CPG / BSC registers", "CPU benchmark", "About EXTune"
};
static const char *const tabs[] = { "Clocks", "Registers", "Benchmark", "About" };
static volatile uint32_t benchmark_sink;
static uint32_t benchmark_us;
static bool benchmark_done;
static bool profiler_available;
static bool profiler_checked;
static jscene *heading_scene;
static jlabel *heading_label;

static void write_line(int y, char const *name, char const *value)
{
    dtext(24, y, C_DARK, name);
    dtext(235, y, C_BLACK, value);
}

static void write_frequency(int y, char const *name, int hz)
{
    char value[48];
    if(hz <= 0)
        snprintf(value, sizeof value, "Not available");
    else
        snprintf(value, sizeof value, "%u.%03u MHz",
            (unsigned)hz / 1000000, ((unsigned)hz % 1000000) / 1000);
    write_line(y, name, value);
}

static void write_register(int y, char const *name, uint32_t value)
{
    char hex[24];
    snprintf(hex, sizeof hex, "0x%08lX", (unsigned long)value);
    write_line(y, name, hex);
}

static void draw_clocks(void)
{
    cpg_compute_freq(); /* Only reads the CPG; never changes its registers. */
    clock_frequency_t const *f = clock_freq();
    char ratio[32];

    write_frequency(91, "CPU (Iphi)", f->Iphi_f);
    write_frequency(117, "SuperHyway (Sphi)", f->Sphi_f);
    write_frequency(143, "Bus (Bphi)", f->Bphi_f);
    write_frequency(169, "Peripheral (Pphi)", f->Pphi_f);
    snprintf(ratio, sizeof ratio, "%d x %d", f->FLL, f->PLL);
    write_line(199, "FLL x PLL", ratio);
    snprintf(ratio, sizeof ratio, "1/%d  1/%d  1/%d  1/%d",
        f->Iphi_div, f->Sphi_div, f->Bphi_div, f->Pphi_div);
    write_line(225, "I / S / B / P", ratio);
    dtext(24, 259, C_DARK, "Estimated using gint's SH7305 clock model.");
}

static void draw_registers(void)
{
    struct cpg_overclock_setting s;
    cpg_get_overclock_setting(&s); /* Read-only snapshot. */
    write_register(88, "FLLFRQ", s.FLLFRQ);
    write_register(111, "FRQCR", s.FRQCR);
    write_register(140, "CS0BCR (ROM)", s.CS0BCR);
    write_register(163, "CS0WCR (ROM)", s.CS0WCR);
    write_register(186, "CS2WCR", s.CS2WCR);
    write_register(209, "CS3WCR (RAM)", s.CS3WCR);
    write_register(232, "CS5aWCR", s.CS5aWCR);
    dtext(24, 260, C_DARK, "No memory latency or PLL writes are performed.");
}

static void draw_bench(void)
{
    char value[84];
    dtext(24, 96, C_BLACK, "Integer mixing, 100,000 iterations");
    if(profiler_checked && !profiler_available) {
        dtext(24, 132, C_RED, "gint profiling timer is unavailable.");
        return;
    }
    if(!benchmark_done) {
        dtext(24, 132, C_DARK, "Press EXE to start the benchmark.");
    }
    else {
        snprintf(value, sizeof value, "%lu us",
            (unsigned long)benchmark_us);
        write_line(142, "Elapsed time", value);
        snprintf(value, sizeof value, "0x%08lX",
            (unsigned long)benchmark_sink);
        write_line(170, "Checksum", value);
        dtext(24, 208, C_DARK, "Lower elapsed time is faster.");
        dtext(24, 235, C_DARK, "Press EXE to repeat.");
    }
}

static void draw_about(void)
{
    dtext(24, 92, C_BLACK, "EXTune - EX-word adaptation of Xtune/Ptune4");
    dtext(24, 119, C_DARK, "Built with gint-exword, JustUI-exword and fxSDK-exword.");
    dtext(24, 158, C_BLACK, "Experimental / read-only clock diagnostics.");
    dtext(24, 184, C_BLACK, "Calculator Ptune presets are NOT EX-word presets.");
    dtext(24, 212, C_BLACK, "Clock, BSC and wait-state changes are disabled.");
    dtext(24, 252, C_DARK, "Use on real hardware at your own risk.");
}

static void draw_screen(int page)
{
    dclear(C_WHITE);
    drect(0, 0, DWIDTH-1, 36, C_RGB(3, 9, 16));
    for(int i=0; i<PAGE_COUNT; i++) {
        if(i == page)
            drect(i*132+2, 3, i*132+129, 33, C_RGB(12, 22, 28));
        dtext(i*132+14, 14, C_WHITE, tabs[i]);
    }

    if(heading_scene && heading_label) {
        jlabel_set_text(heading_label, titles[page]);
        jscene_render(heading_scene);
    }
    else {
        dtext(24, 52, C_BLACK, titles[page]);
    }

    if(page == PAGE_CLOCKS) draw_clocks();
    if(page == PAGE_REGISTERS) draw_registers();
    if(page == PAGE_BENCH) draw_bench();
    if(page == PAGE_ABOUT) draw_about();

    drect(0, 292, DWIDTH-1, DHEIGHT-1, C_RGB(3, 9, 16));
    dtext(12, 303, C_WHITE,
        "LEFT/RIGHT: page    EXE: refresh / benchmark    ESC: exit");
    dupdate();
}

static void run_benchmark(void)
{
    /* Avoid claiming a timer before the first screen is displayed. */
    if(!profiler_checked) {
        profiler_available = (prof_init() == 0);
        profiler_checked = true;
    }
    if(!profiler_available) return;
    uint32_t v = 0x12345678;
    prof_t p = prof_make();
    prof_enter(p);
    for(uint32_t i=0; i<100000; i++)
        v = (v << 5) ^ (v >> 2) ^ (i * 2654435761u);
    prof_leave(p);
    benchmark_sink = v;
    benchmark_us = prof_time(p);
    benchmark_done = true;
}

int main(void)
{
    int page = PAGE_ABOUT;

    /*
     * Hardware smoke checkpoint: display before any CPG/BSC inspection,
     * profiler initialization, or JustUI widget allocation. If this screen
     * never appears, the problem is earlier (loader, gint, or constructors).
     * The start key may still be down when the module loads; accept a fresh
     * KEYEV_DOWN and allow EXIT to return without touching hardware.
     */
    dclear(C_WHITE);
    dtext(24, 34, C_BLACK, "EXTune / EX-word - boot checkpoint");
    dtext(24, 85, C_BLACK, "gint rendering initialized.");
    dtext(24, 119, C_DARK, "No clocks or memory settings changed.");
    dtext(24, 173, C_BLUE, "Press EXE to continue.");
    dtext(24, 207, C_DARK, "Press ESC to return to menu.");
    dupdate();

    while(true) {
        key_event_t e = waitevent(NULL);
        if(e.type != KEYEV_DOWN) continue;
        if(e.key == KEY_EXIT) return -2;
        if(e.key == KEY_EXE) break;
    }

    /* JustUI renders the page title; gint handles EX-word key/touch events. */
    heading_scene = jscene_create(20, 48, DWIDTH-40, 30, NULL);
    if(heading_scene) {
        heading_label = jlabel_create(titles[page], heading_scene);
        if(heading_label) jlabel_set_text_color(heading_label, C_BLACK);
    }
    draw_screen(page);

    bool running = true;
    while(running) {
        key_event_t e = waitevent(NULL);
        if(e.type == KEYEV_TOUCH_DOWN && e.y < 38) {
            int choice = e.x / 132;
            if(choice >= 0 && choice < PAGE_COUNT) page = choice;
        }
        else if(e.type == KEYEV_DOWN || e.type == KEYEV_HOLD) {
            /* Do not interpret a held launch/AC key as an immediate exit. */
            if(e.key == KEY_EXIT && e.type == KEYEV_DOWN) running = false;
            else if(e.key == KEY_LEFT) page = (page + PAGE_COUNT - 1) % PAGE_COUNT;
            else if(e.key == KEY_RIGHT) page = (page + 1) % PAGE_COUNT;
            else if(e.key == KEY_EXE && page == PAGE_BENCH && e.type == KEYEV_DOWN)
                run_benchmark();
        }
        if(running) draw_screen(page);
    }

    if(heading_scene) jwidget_destroy(heading_scene);
    if(profiler_available) prof_quit();
    /* EX-word gint expects -2 for a normal return to its host menu. */
    return -2;
}
