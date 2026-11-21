/* Full SH7305 BSC explorer: all eight chip selects and their timing fields. */
#include "bsc_full.h"
#include <gint/display.h>
#include <gint/mpu/bsc.h>
#include <stdint.h>
#include <stdio.h>

static const char *const cs_names[]={"CS0","CS2","CS3","CS4","CS5A","CS5B","CS6A","CS6B"};
static const char *const bcr_names[]={"IWW","IWRWD","IWRWS","IWRRD","IWRRS"};
static const char *const wcr_names[]={"WW","SW","HW","WR"};
static const char *const sdram_names[]={"TRP","TRCD","A3CL","TRWL","TRC"};
static const uint8_t bcr_shifts[]={28,25,22,19,16};
static const uint8_t wcr_shifts[]={16,11,0,7};
static const uint8_t wcr_bits[]={3,2,2,4};
static const uint8_t sdram_shifts[]={13,10,7,3,0};
static uint32_t original_bcr[8],original_wcr[8];
static unsigned chip,field,mode;
static int initialized,notice;

static volatile uint32_t *bcr_addr(unsigned i)
{
    return ((volatile uint32_t *)&SH7305_BSC.CS0BCR)+i;
}
static volatile uint32_t *wcr_addr(unsigned i)
{
    return ((volatile uint32_t *)&SH7305_BSC.CS0WCR)+i;
}
static int field_count(void) { return mode?(chip==2?5:4):5; }
static const char *field_name(void)
{
    return mode?(chip==2?sdram_names[field]:wcr_names[field]):bcr_names[field];
}
static unsigned field_shift(void)
{
    return mode?(chip==2?sdram_shifts[field]:wcr_shifts[field]):bcr_shifts[field];
}
static unsigned field_mask(void)
{
    unsigned bits=mode?(chip==2?2:wcr_bits[field]):3;
    return (1u<<bits)-1;
}
static void apply_sdram_mode(unsigned cas)
{
    /* CPtune4's SDRAM mode-register reset after a valid CAS change. */
    if(cas==1) *(volatile uint16_t *)0xFEC15040=0;
    if(cas==2) *(volatile uint16_t *)0xFEC15060=0;
}
void ex_bsc_init(void)
{
    if(initialized)return;
    for(unsigned i=0;i<8;i++) {
        original_bcr[i]=*bcr_addr(i);
        original_wcr[i]=*wcr_addr(i);
    }
    initialized=1;
}
void ex_bsc_move_chip(int delta)
{
    chip=(chip+8+delta%8)%8;field=0;notice=0;
}
void ex_bsc_move_field(int delta)
{
    unsigned n=field_count();
    int next=(int)field+delta;
    if(next<0)next=(int)n-1;
    if(next>=(int)n)next=0;
    field=(unsigned)next;notice=0;
}
void ex_bsc_toggle_mode(void)
{
    mode=!mode;field=0;notice=0;
}
int ex_bsc_notice(void) {return notice;}
void ex_bsc_edit_field(int delta,int armed,int unlocked)
{
    if(!armed){notice=1;return;}
    volatile uint32_t *reg=mode?wcr_addr(chip):bcr_addr(chip);
    unsigned shift=field_shift(),mask=field_mask();
    unsigned old=(*reg>>shift)&mask;
    int value=(int)old+delta;
    if(value<0||value>(int)mask){notice=2;return;}
    uint32_t saved=mode?original_wcr[chip]:original_bcr[chip];
    unsigned boot=(saved>>shift)&mask;
    if(!unlocked && value<(int)boot){notice=3;return;}
    if(mode && chip==2 && field==2 && value!=1 && value!=2) {
        notice=4;return;
    }
    uint32_t change=(*reg&~(mask<<shift))|((uint32_t)value<<shift);
    *reg=change;
    if(mode && chip==2 && field==2)apply_sdram_mode((unsigned)value);
    notice=5;
}
void ex_bsc_restore_selected(int armed)
{
    if(!armed){notice=1;return;}
    volatile uint32_t *reg=mode?wcr_addr(chip):bcr_addr(chip);
    uint32_t boot=mode?original_wcr[chip]:original_bcr[chip];
    *reg=boot;
    if(mode && chip==2) apply_sdram_mode((boot>>7)&3u);
    notice=6;
}
void ex_bsc_draw(void)
{
    ex_bsc_init();
    char text[80];
    dtext(20,81,C_DARK,"Chip select       Bus control (BCR)        Wait control (WCR)");
    for(unsigned i=0;i<8;i++) {
        int y=103+i*18;
        if(chip==i)drect(14,y-3,DWIDTH-15,y+16,C_RGB(25,29,31));
        int color=chip==i?C_WHITE:C_BLACK;
        snprintf(text,sizeof text,"%-4s",cs_names[i]);
        dtext(23,y,color,text);
        snprintf(text,sizeof text,"0x%08X",(unsigned)*bcr_addr(i));
        dtext(134,y,chip==i&&!mode?C_WHITE:C_DARK,text);
        snprintf(text,sizeof text,"0x%08X",(unsigned)*wcr_addr(i));
        dtext(326,y,chip==i&&mode?C_WHITE:C_DARK,text);
    }
    volatile uint32_t *reg=mode?wcr_addr(chip):bcr_addr(chip);
    unsigned value=(*reg>>field_shift())&field_mask();
    snprintf(text,sizeof text,"%s %s: %s = %u  (field %u/%d)",cs_names[chip],
        mode?"WCR":"BCR",field_name(),value,field+1,field_count());
    dtext(20,262,C_BLUE,text);
    dtext(20,283,C_DARK,"UP/DOWN chip   A/D field   W BCR/WCR   EXE+/Q-   R reset");
    if(notice==1)dtext(350,63,C_RED,"ARM: S twice");
    else if(notice==2)dtext(350,63,C_RED,"Field limit");
    else if(notice==3)dtext(350,63,C_RED,"Locked wait");
    else if(notice==4)dtext(350,63,C_RED,"Invalid CAS");
    else if(notice==5)dtext(350,63,C_BLUE,"Written");
    else if(notice==6)dtext(350,63,C_BLUE,"Restored");
}
