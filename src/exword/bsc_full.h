#ifndef EXTUNE_BSC_FULL_H
#define EXTUNE_BSC_FULL_H
void ex_bsc_init(void);
void ex_bsc_draw(void);
void ex_bsc_move_chip(int delta);
void ex_bsc_move_field(int delta);
void ex_bsc_toggle_mode(void);
void ex_bsc_edit_field(int delta, int armed, int unlocked);
void ex_bsc_restore_selected(int armed);
int ex_bsc_notice(void);
#endif
