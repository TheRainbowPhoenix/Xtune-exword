#ifndef EXTUNE_PTUNE_PORT_H
#define EXTUNE_PTUNE_PORT_H
#include <gint/clock.h>
#include <stdbool.h>
#include <stdint.h>

enum { PT_ROM_MARGIN, PT_RAM_MARGIN, PT_MAX_PLL, PT_MAX_CPU, PT_MAX_SHW,
       PT_MAX_BUS, PT_MAX_IO, PT_UNLOCKED, PT_AUTO_WAIT, PT_AUTO_PFC,
       PT_START_TEST, PT_F1_CONFIRM, PT_LONG_ROM_TEST, PT_SETTING_COUNT };
typedef struct {
    uint32_t magic, version;
    uint32_t settings[PT_SETTING_COUNT];
    uint32_t roR[10], TRC[4];
    struct cpg_overclock_setting presets[4];
    uint32_t crc;
} pt_config_t;

extern pt_config_t pt;
extern int pt_expr_cursor, pt_setting_cursor, pt_mem_margin, pt_bsc_cursor;
extern int pt_preset_cursor, pt_write_armed;
extern int pt_notice;
void pt_init(void);
void pt_reset(void);
void pt_draw_express(void);
void pt_draw_memdata(void);
void pt_draw_settings(void);
void pt_draw_presets(void);
void pt_draw_help(void);
void pt_draw_bsc_editor(void);
void pt_adjust_setting(int direction, int fine);
void pt_edit_express(int direction);
void pt_edit_bsc(int direction);
void pt_choose_preset(bool capture);
int pt_load(void);
int pt_save(void);
void pt_restore(void);
void pt_run_romcheck(void);
int pt_romcheck_result(void);
void pt_set_notice(int n);
#endif
