/*
 * EX-word adaptation of original CPtune4 express/settings/memory-data model.
 * Preserve the stock CPG/BSC image, Ptune limits and four user presets.
 * Applies only an explicitly selected delta from the current register image:
 * never apply the calculator-specific hard-coded memory controller presets.
 */
#include "ptune_port.h"
#include <gint/clock.h>
#include <gint/cpu.h>
#include <gint/display.h>
#include <gint/exword.h>
#include <gint/gint.h>
#include <gint/mpu/cpg.h>
#include <gint/mpu/bsc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MHZ(n) ((uint32_t)(n) * 1000000u)
#define PT_MAGIC 0x45585434u /* "EXT4" */
#define PT_VER 2u

pt_config_t pt;
int pt_expr_cursor, pt_setting_cursor, pt_mem_margin, pt_bsc_cursor;
int pt_preset_cursor, pt_write_armed, pt_notice;
static struct cpg_overclock_setting original;
static clock_frequency_t original_freq;
static int romcheck_errors = -1;
static int initialised;

static const uint32_t defaults[PT_SETTING_COUNT] = {
    5, 5, MHZ(800), MHZ(275), MHZ(180), MHZ(134), MHZ(50),
    0, 1, 1, 1, 1, 1
};
static const uint32_t stock_roR[10] = {
    MHZ(13), MHZ(23), MHZ(33), MHZ(43), MHZ(53),
    MHZ(62), MHZ(72), MHZ(92), MHZ(110), MHZ(130)
};
static const uint32_t stock_trc[4] = {
    MHZ(80), MHZ(105), MHZ(127), MHZ(150)
};
static const int waits[10] = {0,1,2,3,4,5,6,8,10,12};
static const int trcs[4] = {3,4,6,9};
static const char *const setting_names[PT_SETTING_COUNT] = {
    "ROM margin", "SDRAM margin", "PLL clock max", "CPU clock max",
    "SHW clock max", "Bus clock max", "I/O clock max", "Unrestricted mode",
    "Auto reduce waits", "Auto up PFC", "Startup memory test",
    "F1 confirmation", "Extended roR 10/12"
};
static const char *const expr_names[6] = {
    "FLL multiplier", "PLL multiplier", "Iphi / CPU divider",
    "Sphi / SHW divider", "Bphi / bus divider",
    "Pphi / peripheral divider"
};

static void row(int y, const char *name, const char *value, int select)
{
    if(select) drect(17, y-3, DWIDTH-18, y+18, C_RGB(24,29,31));
    dtext(23, y, select ? C_WHITE : C_DARK, name);
    dtext(273, y, select ? C_WHITE : C_BLACK, value);
}
static void line_u(int y, const char *name, unsigned value, int select)
{
    char s[44];
    snprintf(s, sizeof s, "%u", value);
    row(y, name, s, select);
}
static void line_hex(int y, const char *name, unsigned value, int select)
{
    char s[40];
    snprintf(s, sizeof s, "%08X", value);
    row(y, name, s, select);
}
static uint32_t checksum(void)
{
    uint32_t c=0x9e3779b9u;
    pt_config_t t=pt;
    t.crc=0;
    uint8_t const *p=(void const *)&t;
    for(unsigned i=0; i<sizeof t; i++) c=(c<<5)^(c>>2)^p[i];
    return c;
}
void pt_reset(void)
{
    pt.magic=PT_MAGIC;
    pt.version=PT_VER;
    memcpy(pt.settings,defaults,sizeof defaults);
    memcpy(pt.roR,stock_roR,sizeof stock_roR);
    memcpy(pt.TRC,stock_trc,sizeof stock_trc);
    for(int i=0;i<4;i++)pt.presets[i]=original;
    pt.crc=checksum();
    pt_notice=0;
}
void pt_init(void)
{
    if(initialised)return;
    cpg_compute_freq();
    original_freq=*clock_freq();
    cpg_get_overclock_setting(&original);
    pt_reset();
    pt_write_armed=0;
    initialised=1;
}
void pt_set_notice(int n) { pt_notice=n; }

static void freq_str(char *s, size_t size, uint32_t hz)
{
    snprintf(s,size,"%u.%03u MHz",hz/1000000u,(hz%1000000u)/1000u);
}
static void foot(const char *message)
{
    dtext(20,276,C_DARK,message);
}
static unsigned setting_divider(int f, int selector)
{
    unsigned v=(f >> selector)&15u;
    return (v<10)?1u<<(v+1):0;
}
void pt_draw_express(void)
{
    cpg_compute_freq();
    const clock_frequency_t *f=clock_freq();
    struct cpg_overclock_setting s;
    cpg_get_overclock_setting(&s);
    int values[6]={ f->FLL, f->PLL, f->Iphi_div, f->Sphi_div,
                    f->Bphi_div, f->Pphi_div };
    for(int i=0;i<6;i++) {
        char value[64];
        if(i<2)snprintf(value,sizeof value,"x%d",values[i]);
        else snprintf(value,sizeof value,"1/%d",values[i]);
        row(85+i*29,expr_names[i],value,pt_expr_cursor==i);
    }
    char m[64];
    freq_str(m,sizeof m,f->Iphi_f);
    dtext(20,264,C_DARK,"CPU:");
    dtext(78,264,C_BLUE,m);
    foot(pt_write_armed==2
         ? "EXE apply +   Q apply -   R stock   S disarm"
         : "S unlock writes   EXE/Q adjust   R restore");
}
void pt_draw_memdata(void)
{
    char s[64];
    snprintf(s,sizeof s,"ROM margin %u%%",pt.settings[PT_ROM_MARGIN]);
    dtext(20,83,C_BLUE,s);
    snprintf(s,sizeof s,"SDRAM margin %u%%",pt.settings[PT_RAM_MARGIN]);
    dtext(274,83,C_BLUE,s);
    for(int i=0;i<10;i++) {
        unsigned value=pt.roR[i];
        if(pt_mem_margin)value=value/100*(100-pt.settings[PT_ROM_MARGIN]);
        snprintf(s,sizeof s,"roR_%d: %u.%03u MHz",waits[i],
            value/1000000u,(value%1000000u)/1000u);
        dtext(20,104+16*i,C_BLACK,s);
    }
    for(int i=0;i<4;i++) {
        unsigned value=pt.TRC[i];
        if(pt_mem_margin)value=value/100*(100-pt.settings[PT_RAM_MARGIN]);
        snprintf(s,sizeof s,"TRC_%d: %u.%03u MHz",trcs[i],
            value/1000000u,(value%1000000u)/1000u);
        dtext(274,110+28*i,C_BLACK,s);
    }
    foot(pt_mem_margin
        ? "EXE: raw values  Q: ROM check  W: RAM test"
        : "EXE: margins  Q: ROM check  W: RAM test");
}
void pt_draw_settings(void)
{
    int top=pt_setting_cursor-3;
    if(top<0)top=0;
    if(top>PT_SETTING_COUNT-8)top=PT_SETTING_COUNT-8;
    for(int i=top;i<top+8;i++) {
        char s[48];
        unsigned v=pt.settings[i];
        if(i>=PT_MAX_PLL&&i<=PT_MAX_IO)
            snprintf(s,sizeof s,"%u MHz",v/1000000);
        else if(i<=PT_RAM_MARGIN)
            snprintf(s,sizeof s,"%u %%",v);
        else snprintf(s,sizeof s,"%s",v?"On":"Off");
        row(87+(i-top)*22,setting_names[i],s,pt_setting_cursor==i);
    }
    foot("UP/DOWN select   EXE increase   Q decrease");
}
void pt_draw_presets(void)
{
    for(int i=0;i<4;i++) {
        struct cpg_overclock_setting *p=&pt.presets[i];
        char s[48];
        snprintf(s,sizeof s,"FLL %u   PLL %u",
            p->FLLFRQ & 2047u,((p->FRQCR >> 24)&63u)+1);
        row(93+i*42,i==0?"F2":i==1?"F3":i==2?"F4":"F5",s,
            pt_preset_cursor==i);
    }
    foot("EXE recall  S capture  O save  I load  R stock");
}
void pt_draw_help(void)
{
    dtext(20,88,C_BLACK,"CPtune4 original menu mapping:");
    dtext(20,112,C_DARK,"Express: FLL PLL IFC SFC BFC PFC");
    dtext(20,134,C_DARK,"Memory data: ROM wait / SDRAM TRC thresholds");
    dtext(20,156,C_DARK,"BSC: ROM and SDRAM timing fields");
    dtext(20,178,C_DARK,"Presets: F2 F3 F4 F5, capture / recall");
    dtext(20,200,C_DARK,"Settings: 13 original CPtune controls");
    dtext(20,222,C_DARK,"Startup: optional ROM/RAM/skip memory test");
    dtext(20,248,C_RED,"Hardware writes require arming. Unstable clocks");
    dtext(20,269,C_RED,"may freeze EX-word; RESTART may be necessary.");
}
void pt_draw_bsc_editor(void)
{
    struct cpg_overclock_setting s;
    cpg_get_overclock_setting(&s);
    const int f[5] = {
        SH7305_BSC.CS0WCR.WR, SH7305_BSC.CS0WCR.WW,
        SH7305_BSC.CS3WCR.TRC, SH7305_BSC.CS3WCR.A3CL,
        SH7305_BSC.CS3WCR.TRCD
    };
    const char *labels[5]={
        "CS0 ROM read WR","CS0 ROM write WW",
        "CS3 SDRAM TRC","CS3 SDRAM CAS","CS3 SDRAM TRCD"
    };
    for(int i=0;i<5;i++)line_u(94+i*31,labels[i],f[i],pt_bsc_cursor==i);
    foot(pt_write_armed==2
        ? "UP/DOWN select   EXE+  Q-  S disarm"
        : "UP/DOWN select   S arm hardware writes");
}
void pt_adjust_setting(int direction,int fine)
{
    int i=pt_setting_cursor;
    uint32_t cur=pt.settings[i];
    if(i<=PT_RAM_MARGIN) {
        int v=(int)cur+direction; if(v<0)v=0;if(v>15)v=15;
        pt.settings[i]=v;
    }
    else if(i<=PT_MAX_IO) {
        uint32_t max[]={1000,350,200,200,60};
        int delta=(fine?1:10)*1000000;
        int64_t v=(int64_t)cur+direction*delta;
        if(v<0)v=0;if(v>(int64_t)MHZ(max[i-PT_MAX_PLL]))
            v=MHZ(max[i-PT_MAX_PLL]);
        pt.settings[i]=(uint32_t)v;
    }
    else pt.settings[i]=direction>0?1:0;
    pt.crc=checksum();
}
static int cap(const struct cpg_overclock_setting *newsetting)
{
    if(pt.settings[PT_UNLOCKED])return 1;
    unsigned new_fll=newsetting->FLLFRQ & 2047u;
    unsigned new_pll=((newsetting->FRQCR>>24)&63u)+1;
    unsigned old_fll=original.FLLFRQ & 2047u;
    unsigned old_pll=((original.FRQCR>>24)&63u)+1;
    if(!old_fll||!old_pll||new_fll<1||new_pll<1)return 0;
    /* Unlocked mode is the sole route beyond stock +20% clocks. */
    uint64_t mul=(uint64_t)new_fll*new_pll;
    uint64_t base=(uint64_t)old_fll*old_pll;
    if(mul*100u>base*120u)return 0;
    uint64_t basecpu=(uint64_t)original_freq.Iphi_f*mul/base;
    uint64_t basebus=(uint64_t)original_freq.Bphi_f*mul/base;
    uint64_t baseio=(uint64_t)original_freq.Pphi_f*mul/base;
    return basecpu<=pt.settings[PT_MAX_CPU] &&
        basebus<=pt.settings[PT_MAX_BUS] &&
        baseio<=pt.settings[PT_MAX_IO];
}
void pt_edit_express(int direction)
{
    if(pt_write_armed!=2){pt_notice=1;return;}
    struct cpg_overclock_setting s;
    cpg_get_overclock_setting(&s);
    int i=pt_expr_cursor;
    int shift=i==0?0:i==1?24:i==2?20:i==3?12:i==4?8:0;
    unsigned mask=i==0?2047u:i==1?63u:15u;
    unsigned value=((i==0?s.FLLFRQ:s.FRQCR)>>shift)&mask;
    int newvalue=(int)value+direction;
    if(newvalue<0||newvalue>(int)mask){pt_notice=2;return;}
    if(i==0)s.FLLFRQ=(s.FLLFRQ&~mask)|newvalue;
    else s.FRQCR=(s.FRQCR&~(mask<<shift))|((uint32_t)newvalue<<shift);
    if(!cap(&s)){pt_notice=2;return;}
    cpg_set_overclock_setting(&s);
    pt_notice=0;
}
void pt_edit_bsc(int direction)
{
    if(pt_write_armed!=2){pt_notice=1;return;}
    struct cpg_overclock_setting s;
    cpg_get_overclock_setting(&s);
    const unsigned masks[5]={15,7,3,3,3};
    const int shifts[5]={7,16,0,7,10};
    int i=pt_bsc_cursor;
    uint32_t *reg=i<2?&s.CS0WCR:&s.CS3WCR;
    unsigned old=(*reg>>shifts[i])&masks[i];
    int v=(int)old+direction;
    if(v<0||v>(int)masks[i]){pt_notice=2;return;}
    /* Avoid changing SDRAM CAS latency directly until its mode-reset timing
       can be reliably established for this EX-word model. */
    if(i==3){pt_notice=3;return;}
    *reg=(*reg&~(masks[i]<<shifts[i]))|((uint32_t)v<<shifts[i]);
    /* One field change, preserving the other 9 boot memory controller words. */
    cpg_set_overclock_setting(&s);
    pt_notice=0;
}
void pt_choose_preset(bool capture)
{
    int i=pt_preset_cursor;
    if(capture){
        cpg_get_overclock_setting(&pt.presets[i]);
        pt.crc=checksum();
        pt_notice=0;
    }
    else {
        if(pt_write_armed!=2){pt_notice=1;return;}
        /* Never replay a calculator preset. These four slots contain the
           user's captured EX-word register images only. */
        cpg_set_overclock_setting(&pt.presets[i]);
        pt_notice=0;
    }
}
void pt_restore(void)
{
    /* Original full machine image, captured during the initial session. */
    cpg_set_overclock_setting(&original);
    pt_write_armed=0;
    pt_notice=0;
}
/* EX-word file calls must run in the OS world, not from gint drivers. */
static const char config_path[]="\\\\drv0\\EXTUNE.SAV";
static int save_raw(void)
{
    pt.crc=checksum();
    (void)exword_sys_delete(config_path);
    if(exword_sys_create(config_path,1)<0)return -1;
    int fd=exword_sys_open(config_path,2);
    if(fd<0)return -2;
    int written=exword_sys_write(fd,&pt,sizeof pt);
    int rc=exword_sys_close(fd);
    return written==(int)sizeof pt && rc>=0?0:-3;
}
static int load_raw(void)
{
    int fd=exword_sys_open(config_path,1);
    if(fd<0)return -1;
    pt_config_t tmp;
    int n=exword_sys_read(fd,&tmp,sizeof tmp);
    exword_sys_close(fd);
    if(n!=(int)sizeof tmp || tmp.magic!=PT_MAGIC || tmp.version!=PT_VER)
        return -2;
    pt_config_t previous=pt;
    pt=tmp;
    if(checksum()!=tmp.crc) {pt=previous;return -3;}
    return 0;
}
int pt_save(void)
{
    int rc=gint_world_switch(GINT_CALL(save_raw));
    pt_notice=rc?4:5;
    return rc;
}
int pt_load(void)
{
    int rc=gint_world_switch(GINT_CALL(load_raw));
    pt_notice=rc?6:7;
    return rc;
}
void pt_run_romcheck(void)
{
    /* Read our own const data; never probe A0000000 or EX-word OS ROM. */
    static const volatile uint32_t words[16]={
        0x12345678,0x23456789,0x3456789a,0x456789ab,
        0x56789abc,0x6789abcd,0x789abcde,0x89abcdef,
        0x9abcdef0,0xabcdef01,0xbcdef012,0xcdef0123,
        0xdef01234,0xef012345,0xf0123456,0x12345677
    };
    romcheck_errors=0;
    for(int trial=0;trial<64;trial++)
    for(int i=0;i<16;i++) {
        uint32_t expected=0x12345678u+(uint32_t)i*0x11111111u;
        if(words[i]!=expected)romcheck_errors++;
    }
}
int pt_romcheck_result(void) {return romcheck_errors;}
