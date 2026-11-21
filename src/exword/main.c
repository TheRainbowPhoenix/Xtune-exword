/*
 * EXTune EX-word: Ptune-style monitor and non-destructive performance lab.
 *
 * The calculator Ptune presets assume *different* ROM, SDRAM, LCD and BSC
 * timings. These are intentionally NOT written to unknown EX-word hardware.
 */
#include <gint/config.h>
#include <gint/clock.h>
#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/mpu/cpg.h>
#include <gint/mpu/bsc.h>
#include <justui/jscene.h>
#include <justui/jlabel.h>
#include <justui/jwidget.h>
#include <justui/jbutton.h>
#include <justui/jlayout.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "bench.h"
#include "ptune_port.h"

#if !GINT_HW_EX
#error "EXTune EX-word requires EXWORD platform"
#endif

enum {
    PAGE_EXPRESS, PAGE_CLOCK, PAGE_CPG, PAGE_BSC,
    PAGE_BENCH, PAGE_MEMORY, PAGE_MEMDATA, PAGE_SETTINGS,
    PAGE_TUNE, PAGE_PRESETS, PAGE_HELP, PAGE_ABOUT, PAGE_COUNT
};
static const char *const tabs[] = {
    "Express", "Clocks", "CPG", "BSC", "Bench", "RAM test",
    "Mem Data", "Settings", "Plan", "Presets", "Help", "About"
};
static const char *const titles[] = {
    "CPtune4 Express", "Live clocks", "Clock generator (CPG)",
    "Memory controller (BSC)", "Performance benchmark",
    "Non-destructive RAM validation", "CPtune4 Memory data",
    "CPtune4 Settings", "Frequency planning lab", "F2-F5 Presets",
    "CPtune4 controls", "EXTune for EX-word"
};
static int page = PAGE_EXPRESS;
static int cpg_subpage, bsc_subpage;
static int tune_field; /* 0 FLL, 1 PLL */
static int tune_fll, tune_pll;
static int baseline_fll, baseline_pll;
static bool captured;
static struct cpg_overclock_setting baseline;
static clock_frequency_t baseline_freq;
static extune_bench_t benchmark, benchmark_reference;
static extune_memtest_t memory_result;
static bool benchmark_ready;
static bool benchmark_failed;
static jscene *heading_scene;
static jscene *tab_scene;
static jbutton *tab_buttons[6];
static jlabel *heading_label;

static void row(int y, const char *name, const char *value)
{
    dtext(22, y, C_DARK, name);
    dtext(233, y, C_BLACK, value);
}
static void number(int y, const char *name, uint32_t value)
{
    char s[32];
    snprintf(s, sizeof s, "%lu", (unsigned long)value);
    row(y, name, s);
}
static void hex(int y, const char *name, uint32_t value)
{
    char s[24];
    snprintf(s, sizeof s, "0x%08lX", (unsigned long)value);
    row(y, name, s);
}
static void freq(int y, const char *name, uint32_t hz)
{
    char s[32];
    snprintf(s, sizeof s, "%lu.%03lu MHz",
        (unsigned long)(hz / 1000000u),
        (unsigned long)((hz % 1000000u) / 1000u));
    row(y, name, s);
}
static void mhz_uint(int y, const char *name, uint64_t hz)
{
    if(hz > UINT32_MAX) {
        row(y, name, "Beyond 32-bit range");
        return;
    }
    freq(y, name, (uint32_t)hz);
}
static void capture(void)
{
    if(captured) return;
    cpg_compute_freq();
    baseline_freq = *clock_freq();
    cpg_get_overclock_setting(&baseline);
    baseline_fll = baseline.FLLFRQ & 2047u;
    baseline_pll = ((baseline.FRQCR >> 24) & 63u) + 1;
    tune_fll = baseline_fll;
    tune_pll = baseline_pll;
    captured = true;
}

static void draw_clock(void)
{
    cpg_compute_freq();
    const clock_frequency_t *f = clock_freq();
    char s[60];
    freq(89, "CPU / Iphi", f->Iphi_f);
    freq(114, "SuperHyway / Sphi", f->Sphi_f);
    freq(139, "Memory bus / Bphi", f->Bphi_f);
    freq(164, "Peripheral / Pphi", f->Pphi_f);
    snprintf(s, sizeof s, "FLL %d  x  PLL %d", f->FLL, f->PLL);
    row(194, "Multiplier", s);
    snprintf(s, sizeof s, "I/%d  S/%d  B/%d  P/%d",
        f->Iphi_div, f->Sphi_div, f->Bphi_div, f->Pphi_div);
    row(219, "Dividers", s);
    dtext(22, 254, C_DARK, "Computed from SH7305 CPG, not externally measured.");
    dtext(22, 275, C_DARK, "EXE: refresh clocks / registers");
}
static void draw_cpg(void)
{
    struct cpg_overclock_setting s;
    cpg_get_overclock_setting(&s);
    if(!cpg_subpage) {
        hex(86, "FLLFRQ", s.FLLFRQ);
        hex(111, "FRQCR", s.FRQCR);
        hex(136, "PLLCR", SH7305_CPG.PLLCR.lword);
        hex(161, "SSCGCR", SH7305_CPG.SSCGCR.lword);
        hex(186, "LSTATS", SH7305_CPG.LSTATS);
        number(217, "FLL multiplier", SH7305_CPG.FLLFRQ.FLF);
        number(242, "PLL multiplier", SH7305_CPG.FRQCR.STC + 1);
    }
    else {
        number(86, "FLL / 2", SH7305_CPG.FLLFRQ.SELXM);
        number(111, "PLL enabled", SH7305_CPG.PLLCR.PLLE);
        number(136, "FLL enabled", SH7305_CPG.PLLCR.FLLE);
        number(161, "Spread spectrum", SH7305_CPG.SSCGCR.SSEN);
        number(186, "CPU divider field", SH7305_CPG.FRQCR.IFC);
        number(211, "Bus divider field", SH7305_CPG.FRQCR.BFC);
        number(236, "I/O divider field", SH7305_CPG.FRQCR.P1FC);
    }
    dtext(22, 271, C_DARK, "UP/DOWN: CPG details       EXE: resample");
}
static void draw_bsc(void)
{
    struct cpg_overclock_setting s;
    cpg_get_overclock_setting(&s);
    if(!bsc_subpage) {
        hex(86, "CS0BCR (ROM)", s.CS0BCR);
        hex(110, "CS0WCR (ROM)", s.CS0WCR);
        hex(134, "CS2BCR", s.CS2BCR);
        hex(158, "CS2WCR", s.CS2WCR);
        hex(182, "CS3BCR (SDRAM)", s.CS3BCR);
        hex(206, "CS3WCR (SDRAM)", s.CS3WCR);
        hex(230, "CS5aBCR", s.CS5aBCR);
        hex(254, "CS5aWCR", s.CS5aWCR);
    }
    else {
        number(86, "ROM read WR field", SH7305_BSC.CS0WCR.WR);
        number(111, "ROM write WW field", SH7305_BSC.CS0WCR.WW);
        number(136, "CS2 read WR field", SH7305_BSC.CS2WCR.WR);
        number(161, "CS2 write WW field", SH7305_BSC.CS2WCR.WW);
        number(186, "SDRAM TRC field", SH7305_BSC.CS3WCR.TRC);
        number(211, "SDRAM CAS latency field", SH7305_BSC.CS3WCR.A3CL);
        hex(236, "SDCR", SH7305_BSC.SDCR.lword);
    }
    dtext(22, 276, C_DARK, "UP/DOWN: BSC details    No wait-state writes.");
}
static void bench_row(int y, const char *name, uint32_t result,
                      uint32_t reference)
{
    char s[64];
    if(reference && result) {
        unsigned ratio = (unsigned)(((uint64_t)reference * 100u) / result);
        snprintf(s, sizeof s, "%lu us   %u%% of baseline",
            (unsigned long)result, ratio);
    }
    else
        snprintf(s, sizeof s, "%lu us", (unsigned long)result);
    row(y, name, s);
}
static void draw_bench(void)
{
    if(benchmark_failed) {
        dtext(22, 107, C_RED, "Profiler timer unavailable.");
        dtext(22, 140, C_DARK, "Check the EX-word peripheral clock configuration.");
    }
    else if(!benchmark.done) {
        dtext(22, 94, C_BLACK, "Press EXE to run Ptune-style performance tests.");
        dtext(22, 130, C_DARK, "CPU: 100k integer iterations");
        dtext(22, 153, C_DARK, "ROM: 64 KiB reads from own add-in");
        dtext(22, 176, C_DARK, "RAM: 128 KiB traffic in owned buffer");
        dtext(22, 199, C_DARK, "LCD: one hardware display transfer");
        dtext(22, 246, C_DARK, "Timing methods differ from original Ptune.");
    }
    else {
        bench_row(92, "CPU integer", benchmark.cpu_us,
            benchmark_reference.cpu_us);
        bench_row(123, "ROM reads", benchmark.rom_us,
            benchmark_reference.rom_us);
        bench_row(154, "RAM mix", benchmark.ram_us,
            benchmark_reference.ram_us);
        bench_row(185, "LCD update", benchmark.display_us,
            benchmark_reference.display_us);
        hex(216, "Checksum", benchmark.checksum);
        dtext(22, 252, C_DARK, "100% = first run; higher % means faster.");
        dtext(22, 276, C_DARK, "EXE: repeat for comparison.");
    }
}
static void draw_memory(void)
{
    if(!memory_result.done) {
        dtext(22, 90, C_BLACK, "Checks only 1 KiB of EXTune-owned RAM.");
        dtext(22, 128, C_DARK, "Patterns: 00, FF, 55, AA, walking bits,");
        dtext(22, 151, C_DARK, "inverse walking bits, address noise.");
        dtext(22, 190, C_BLACK, "EXE: 8 passes (quick check)");
        dtext(22, 217, C_BLACK, "UP: 128 passes (stress check)");
        dtext(22, 256, C_DARK, "No OS RAM, flash, or storage is touched.");
    }
    else {
        row(94, "Result", memory_result.errors ? "FAIL" : "PASS");
        number(130, "Completed passes", memory_result.passes);
        number(164, "Mismatches", memory_result.errors);
        if(memory_result.errors) {
            number(202, "Index", memory_result.failing_index);
            hex(229, "Expected", memory_result.expected);
            hex(256, "Actual", memory_result.actual);
        }
        else {
            dtext(22, 229, C_DARK, "Only the owned 1 KiB buffer was tested.");
            dtext(22, 256, C_DARK, "EXE: retest    UP: extended test");
        }
    }
}
static uint64_t predicted(uint32_t current)
{
    if(!baseline_fll || !baseline_pll) return 0;
    uint64_t result = (uint64_t)current * (unsigned)tune_fll;
    result = result / (unsigned)baseline_fll;
    result = result * (unsigned)tune_pll / (unsigned)baseline_pll;
    return result;
}
static void draw_tune(void)
{
    char s[72];
    dtext(22, 86, C_DARK, "EX-word clock planner (no register writes)");
    snprintf(s, sizeof s, "FLL %d  (stock %d)", tune_fll, baseline_fll);
    row(116, tune_field == 0 ? "> FLL multiplier" : "  FLL multiplier", s);
    snprintf(s, sizeof s, "PLL %d  (stock %d)", tune_pll, baseline_pll);
    row(145, tune_field == 1 ? "> PLL multiplier" : "  PLL multiplier", s);
    mhz_uint(180, "Predicted CPU", predicted(baseline_freq.Iphi_f));
    mhz_uint(207, "Predicted bus", predicted(baseline_freq.Bphi_f));
    mhz_uint(234, "Predicted I/O", predicted(baseline_freq.Pphi_f));

    if(predicted(baseline_freq.Bphi_f) * 10 >
           (uint64_t)baseline_freq.Bphi_f * 11 ||
       predicted(baseline_freq.Pphi_f) * 10 >
           (uint64_t)baseline_freq.Pphi_f * 11)
        dtext(22, 265, C_RED, "Bus/I/O >110% stock: timings unverified.");
    else
        dtext(22, 265, C_DARK, "UP/DOWN: field  EXE:+  Q:-  R:reset");
}
static void draw_about(void)
{
    dtext(22, 91, C_BLACK, "EXTune for EX-word - Ptune-inspired diagnostics");
    dtext(22, 124, C_DARK, "Clocks + CPG/BSC inspection + benchmarks");
    dtext(22, 147, C_DARK, "RAM validation + clock candidate planning");
    dtext(22, 182, C_BLACK, "Original Ptune/Ftune calculator profiles");
    dtext(22, 205, C_BLACK, "are NOT safe presets for EX-word hardware.");
    dtext(22, 246, C_RED, "S,S arm hardware edits; R restores boot clocks.");
    dtext(22, 272, C_DARK, "All tests use this add-in's own memory.");
}

static void render(void)
{
    dclear(C_WHITE);
    drect(0, 0, DWIDTH-1, 38, C_RGB(3, 9, 16));
    int group = page / 6;
    for(int i=0;i<6;i++) {
        if(tab_buttons[i]) {
            jbutton_set_text(tab_buttons[i],tabs[group*6+i]);
            jbutton_set_disabled(tab_buttons[i],group*6+i == page);
        }
    }
    if(tab_scene) jscene_render(tab_scene);
    else for(int i=0;i<6;i++)
        dtext(8+88*i,14,C_WHITE,tabs[group*6+i]);
    if(heading_scene && heading_label) {
        jlabel_set_text(heading_label, titles[page]);
        jscene_render(heading_scene);
    }
    else dtext(22, 53, C_BLACK, titles[page]);

    switch(page) {
        case PAGE_EXPRESS: pt_draw_express(); break;
        case PAGE_CLOCK: draw_clock(); break;
        case PAGE_CPG: draw_cpg(); break;
        case PAGE_BSC:
            if(bsc_subpage) pt_draw_bsc_editor();
            else draw_bsc();
            break;
        case PAGE_BENCH: draw_bench(); break;
        case PAGE_MEMORY: draw_memory(); break;
        case PAGE_MEMDATA:
            pt_draw_memdata();
            if(pt_romcheck_result() >= 0) {
                char s[48];
                snprintf(s,sizeof s,"ROM check errors: %d",pt_romcheck_result());
                dtext(278,257,pt_romcheck_result()?C_RED:C_BLUE,s);
            }
            break;
        case PAGE_SETTINGS: pt_draw_settings(); break;
        case PAGE_TUNE: draw_tune(); break;
        case PAGE_PRESETS: pt_draw_presets(); break;
        case PAGE_HELP: pt_draw_help(); break;
        case PAGE_ABOUT: draw_about(); break;
    }
    if(pt_notice) {
        const char *msg[]={
            "", "Arm writes with S first", "Rejected by clock limits",
            "SDRAM CAS change locked", "Config save failed",
            "Config saved", "Config load failed", "Config loaded"
        };
        if(pt_notice>=0 && pt_notice<8)
            dtext(20,281,pt_notice==2||pt_notice==4||pt_notice==6?C_RED:C_BLUE,
                msg[pt_notice]);
        else if(pt_notice==8)
            dtext(20,281,C_RED,"Press S again to ARM experimental writes");
    }
    drect(0, 294, DWIDTH-1, DHEIGHT-1, C_RGB(3, 9, 16));
    dtext(11, 305, C_WHITE,
        "LEFT/RIGHT page   EXE action   UP/DOWN option   ESC exit");
    dupdate();
}
static void perform_action(int direction)
{
    if(page == PAGE_EXPRESS) pt_edit_express(direction);
    else if(page == PAGE_BSC && bsc_subpage) pt_edit_bsc(direction);
    else if(page == PAGE_MEMDATA) pt_mem_margin ^= 1;
    else if(page == PAGE_SETTINGS) pt_adjust_setting(direction,0);
    else if(page == PAGE_PRESETS && direction > 0) pt_choose_preset(false);
    else if(page == PAGE_CPG) cpg_compute_freq();
    else if(page == PAGE_CLOCK) cpg_compute_freq();
    else if(page == PAGE_BENCH && direction > 0) {
        benchmark_failed = !extune_bench_run(&benchmark);
        if(!benchmark_failed && !benchmark_ready) {
            benchmark_reference = benchmark;
            benchmark_ready = true;
        }
    }
    else if(page == PAGE_MEMORY) {
        extune_memtest_run(&memory_result, direction > 1 ? 128 : 8);
    }
    else if(page == PAGE_TUNE) {
        if(tune_field == 0) {
            tune_fll += direction * 2;
            if(tune_fll < 1) tune_fll = 1;
            if(tune_fll > 2047) tune_fll = 2047;
        }
        else {
            tune_pll += direction;
            if(tune_pll < 1) tune_pll = 1;
            if(tune_pll > 64) tune_pll = 64;
        }
    }
}
int main(void)
{
    /*
     * Preserve the boot checkpoint that was confirmed to work on CN EX-word.
     * Nothing related to clock reads or JustUI runs before this screen.
     */
    dclear(C_WHITE);
    dtext(22, 33, C_BLACK, "EXTune - CPtune4 EX-word port");
    dtext(22, 87, C_BLACK, "Express / BSC / Memory data / Settings");
    dtext(22, 127, C_DARK, "Experimental tuning requires arming.");
    dtext(22, 188, C_BLUE, "Press EXE to enter.");
    dtext(22, 218, C_DARK, "Press ESC to return to menu.");
    dupdate();
    for(;;) {
        key_event_t e = waitevent(NULL);
        if(e.type != KEYEV_DOWN) continue;
        if(e.key == KEY_EXIT) return -2;
        if(e.key == KEY_EXE) break;
    }

    capture();
    pt_init();
    (void)pt_load(); /* CPtune4 loads saved settings on startup. */
    pt_set_notice(0);

    /* CPtune4 startup memory test prompt, before the Express menu. */
    if(pt.settings[PT_START_TEST]) {
        dclear(C_WHITE);
        dtext(20,30,C_BLACK,"CPtune4 - Startup memory test");
        dtext(20,84,C_DARK,"Choose an option (keyboard):");
        dtext(20,121,C_BLUE,"Q / 1  RAM only");
        dtext(20,158,C_BLUE,"W / 2  ROM and RAM");
        dtext(20,195,C_BLUE,"E / 3  None");
        dtext(20,242,C_DARK,"Tests use owned memory; firmware is not written.");
        dupdate();
        int kind=0;
        for(;;) {
            key_event_t e=waitevent(NULL);
            if(e.type!=KEYEV_DOWN)continue;
            if(e.key==KEY_EX_Q || e.key==KEY_1) {kind=1;break;}
            if(e.key==KEY_EX_W || e.key==KEY_2) {kind=2;break;}
            if(e.key==KEY_EX_E || e.key==KEY_3 || e.key==KEY_EXE)
                break;
            if(e.key==KEY_EXIT)return -2;
        }
        if(kind) {
            extune_memtest_run(&memory_result,8);
            if(kind==2)pt_run_romcheck();
            dclear(C_WHITE);
            dtext(20,40,C_BLACK,"Startup memory test completed");
            dtext(20,97,memory_result.errors?C_RED:C_BLUE,
                memory_result.errors?"RAM FAIL":"RAM PASS");
            if(kind==2)
                dtext(20,130,pt_romcheck_result()?C_RED:C_BLUE,
                    pt_romcheck_result()?"ROM FAIL":"ROM PASS");
            dtext(20,205,C_DARK,"Press EXE for CPtune4 Express.");
            dupdate();
            for(;;) {
                key_event_t e=waitevent(NULL);
                if(e.type==KEYEV_DOWN&&e.key==KEY_EXE)break;
                if(e.type==KEYEV_DOWN&&e.key==KEY_EXIT)return -2;
            }
        }
    }

    tab_scene=jscene_create(0,0,DWIDTH,38,NULL);
    if(tab_scene) {
        jlayout_box *layout=jlayout_set_hbox(tab_scene);
        if(layout) layout->spacing=1;
        for(int i=0;i<6;i++) {
            tab_buttons[i]=jbutton_create(tabs[i],tab_scene);
            if(tab_buttons[i]) {
                jwidget_set_fixed_size(tab_buttons[i],87,36);
                jbutton_set_disabled(tab_buttons[i],i==0);
            }
        }
    }
    heading_scene = jscene_create(18, 46, DWIDTH-36, 32, NULL);
    if(heading_scene) {
        heading_label = jlabel_create(titles[page], heading_scene);
        if(heading_label) jlabel_set_text_color(heading_label, C_BLACK);
    }

    render();
    bool running = true;
    while(running) {
        key_event_t e = waitevent(NULL);
        if(e.type == KEYEV_TOUCH_DOWN) {
            if(e.y < 39) {
                int next = (page/6)*6 + e.x/(DWIDTH/6);
                if(next >= 0 && next < PAGE_COUNT) page = next;
            }
            else if(page == PAGE_TUNE && e.y > 247 && e.y < 292) {
                if(e.x < DWIDTH / 2) perform_action(-1);
                else perform_action(+1);
            }
        }
        else if(e.type == KEYEV_DOWN || e.type == KEYEV_HOLD) {
            if(e.key == KEY_EXIT && e.type == KEYEV_DOWN) {
                running = false;
            }
            else if(e.key == KEY_LEFT)
                page = (page + PAGE_COUNT - 1) % PAGE_COUNT;
            else if(e.key == KEY_RIGHT)
                page = (page + 1) % PAGE_COUNT;
            else if(e.key == KEY_UP) {
                if(page == PAGE_EXPRESS) {
                    if(pt_expr_cursor>0)pt_expr_cursor--;
                }
                else if(page == PAGE_SETTINGS) {
                    if(pt_setting_cursor>0)pt_setting_cursor--;
                }
                else if(page == PAGE_PRESETS) {
                    if(pt_preset_cursor>0)pt_preset_cursor--;
                }
                else if(page == PAGE_CPG) cpg_subpage ^= 1;
                else if(page == PAGE_BSC) {
                    if(!bsc_subpage)bsc_subpage=1;
                    else if(pt_bsc_cursor>0)pt_bsc_cursor--;
                }
                else if(page == PAGE_TUNE) tune_field ^= 1;
                else if(page == PAGE_MEMORY && e.type == KEYEV_DOWN)
                    perform_action(2);
            }
            else if(e.key == KEY_DOWN) {
                if(page == PAGE_EXPRESS) {
                    if(pt_expr_cursor<5)pt_expr_cursor++;
                }
                else if(page == PAGE_SETTINGS) {
                    if(pt_setting_cursor<PT_SETTING_COUNT-1)pt_setting_cursor++;
                }
                else if(page == PAGE_PRESETS) {
                    if(pt_preset_cursor<3)pt_preset_cursor++;
                }
                else if(page == PAGE_CPG) cpg_subpage ^= 1;
                else if(page == PAGE_BSC) {
                    if(!bsc_subpage)bsc_subpage=1;
                    else if(pt_bsc_cursor<4)pt_bsc_cursor++;
                }
                else if(page == PAGE_TUNE) tune_field ^= 1;
            }
            else if(e.type == KEYEV_DOWN) {
                if(e.key == KEY_EXE) perform_action(1);
                else if(e.key == KEY_EX_S &&
                    (page == PAGE_EXPRESS || page == PAGE_BSC)) {
                    pt_write_armed=(pt_write_armed+1)%3;
                    pt_set_notice(pt_write_armed==1?8:0);
                }
                else if(e.key == KEY_EX_Q &&
                    (page == PAGE_EXPRESS ||
                     (page == PAGE_BSC && bsc_subpage) ||
                     page == PAGE_SETTINGS || page == PAGE_TUNE))
                    perform_action(-1);
                else if(e.key == KEY_EX_Q && page == PAGE_MEMDATA)
                    pt_run_romcheck();
                else if(e.key == KEY_EX_W && page == PAGE_MEMDATA) {
                    extune_memtest_run(&memory_result,128);
                    page=PAGE_MEMORY;
                }
                else if(e.key == KEY_EX_S && page == PAGE_PRESETS)
                    pt_choose_preset(true);
                else if(e.key == KEY_EX_O &&
                    (page == PAGE_PRESETS || page == PAGE_SETTINGS))
                    pt_save();
                else if(e.key == KEY_EX_I &&
                    (page == PAGE_PRESETS || page == PAGE_SETTINGS))
                    pt_load();
                else if(e.key == KEY_EX_R && page == PAGE_EXPRESS)
                    pt_restore();
                else if(e.key == KEY_EX_R && page == PAGE_SETTINGS)
                    pt_adjust_setting(-1,0);
                else if(e.key == KEY_EX_R && page == PAGE_PRESETS)
                    pt_reset();
                else if(e.key == KEY_EX_R && page == PAGE_TUNE) {
                    tune_fll = baseline_fll;
                    tune_pll = baseline_pll;
                }
                else if(e.key == KEY_EX_V && page == PAGE_BSC)
                    bsc_subpage ^= 1;
            }
        }
        if(running) render();
    }

    if(heading_scene) jwidget_destroy(heading_scene);
    if(tab_scene) jwidget_destroy(tab_scene);
    extune_bench_quit();
    return -2;
}
