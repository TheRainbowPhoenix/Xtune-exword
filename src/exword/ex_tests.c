/* Ptune-style test suite. Tests never touch arbitrary OS memory.
   Clock/BSC sweeps are experimental, require arming, and may hard lock. */
#include "ex_tests.h"
#include "bench.h"
#include "ptune_port.h"
#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/clock.h>
#include <gint/mpu/cpg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

static const char *const names[EX_TEST_COUNT] = {
    "RAM patterns - quick (8 passes)",
    "RAM patterns - long (256 passes)",
    "ROM image read verification",
    "SDRAM March C- (owned buffer)",
    "RAM address-line / alias test",
    "CPU / ROM / RAM / display benchmarks",
    "ROM wait-state hardware sweep *",
    "SDRAM TRC hardware sweep *",
    "FLL clock-limit hardware sweep *",
    "Run all non-destructive tests"
};
ex_test_state ex_tests;
static volatile uint32_t fallback[256];
static volatile uint32_t evidence;

const char *ex_test_name(int i)
{
    return i>=0 && i<EX_TEST_COUNT?names[i]:"Unknown";
}
void ex_tests_select(int delta)
{
    int n=ex_tests.selected+delta;
    if(n<0)n=EX_TEST_COUNT-1;
    if(n>=EX_TEST_COUNT)n=0;
    ex_tests.selected=n;
}
static void step_screen(const char *name,int n,int total)
{
    char line[64];
    dclear(C_WHITE);
    dtext(18,33,C_BLACK,"EXTune - test running");
    dtext(18,94,C_BLUE,name);
    snprintf(line,sizeof line,"Step %d / %d",n,total);
    dtext(18,138,C_BLACK,line);
    dtext(18,194,C_RED,"Hardware sweeps may freeze this device.");
    dtext(18,222,C_DARK,"A hardware RESTART may be necessary.");
    dtext(18,264,C_DARK,"ESC cancels between completed steps.");
    dupdate();
}
static int cancelled(void)
{
    key_event_t e;
    while((e=pollevent()).type!=KEYEV_NONE)
        if(e.type==KEYEV_DOWN && e.key==KEY_EXIT)return 1;
    return 0;
}
static volatile uint32_t *owned(unsigned *count,int *heap)
{
    *count=2048;
    *heap=1;
    volatile uint32_t *buf=(volatile uint32_t *)malloc(*count*4);
    if(buf)return buf;
    *heap=0;
    *count=256;
    return fallback;
}
static uint32_t pattern(unsigned i,unsigned pass)
{
    switch(pass&7u) {
        case 0:return 0;
        case 1:return 0xffffffffu;
        case 2:return 0x55555555u;
        case 3:return 0xaaaaaaaau;
        case 4:return 1u<<(i&31u);
        case 5:return ~(1u<<(i&31u));
        case 6:return i*0x9e3779b9u;
        default:return ~(i*0x9e3779b9u);
    }
}
static int run_patterns(unsigned passes)
{
    unsigned count;
    int heap;
    volatile uint32_t *b=owned(&count,&heap);
    for(unsigned pass=0;pass<passes;pass++) {
        for(unsigned i=0;i<count;i++)b[i]=pattern(i,pass);
        for(unsigned i=0;i<count;i++) {
            uint32_t read=b[i];
            evidence^=read;
            if(read!=pattern(i,pass)) {
                ex_tests.errors++;
                if(heap)free((void *)b);
                return -1;
            }
        }
        ex_tests.passes++;
    }
    ex_tests.bytes_checked+=count*4*passes;
    if(heap)free((void *)b);
    return 0;
}
static int march_c(void)
{
    unsigned count;
    int heap,bad=0;
    volatile uint32_t *b=owned(&count,&heap);
    for(unsigned i=0;i<count;i++)b[i]=0;
    for(unsigned i=0;i<count;i++) {
        if(b[i]!=0){bad=1;break;}
        b[i]=0xffffffffu;
    }
    if(!bad)for(unsigned i=count;i-- > 0;) {
        if(b[i]!=0xffffffffu){bad=1;break;}
        b[i]=0;
    }
    if(!bad)for(unsigned i=0;i<count;i++) {
        if(b[i]!=0){bad=1;break;}
        b[i]=i^0x5a5a5a5au;
    }
    if(!bad)for(unsigned i=0;i<count;i++)
        if(b[i]!=(i^0x5a5a5a5au)){bad=1;break;}
    ex_tests.bytes_checked+=count*16;
    ex_tests.passes++;
    if(heap)free((void *)b);
    ex_tests.errors+=bad;
    return bad?-1:0;
}
static int address_lines(void)
{
    unsigned count;
    int heap,bad=0;
    volatile uint32_t *b=owned(&count,&heap);
    for(unsigned i=0;i<count;i++)b[i]=0x5555aaaau;
    for(unsigned bit=0;(1u<<bit)<count;bit++) {
        unsigned index=1u<<bit;
        b[index]=0xaa55aa55u;
        for(unsigned check=0;check<count;check++)
            if(b[check]!=(check==index?0xaa55aa55u:0x5555aaaau)){
                bad=1;break;
            }
        if(bad)break;
        b[index]=0x5555aaaau;
        ex_tests.passes++;
    }
    ex_tests.bytes_checked+=count*4*ex_tests.passes;
    if(heap)free((void *)b);
    ex_tests.errors+=bad;
    return bad?-1:0;
}
static int read_rom(void)
{
    pt_run_romcheck();
    int n=pt_romcheck_result();
    if(n>0)ex_tests.errors+=n;
    ex_tests.passes+=64;
    ex_tests.bytes_checked+=64*16*4;
    return n?-1:0;
}
static int run_benchmark(void)
{
    extune_bench_t b;
    if(!extune_bench_run(&b)){ex_tests.refused=1;return -1;}
    ex_tests.cpu_us=b.cpu_us;
    ex_tests.rom_us=b.rom_us;
    ex_tests.ram_us=b.ram_us;
    ex_tests.display_us=b.display_us;
    ex_tests.passes++;
    return 0;
}
static int verify_candidate(void)
{
    if(march_c())return -1;
    if(read_rom())return -1;
    return 0;
}
static int run_sweep(int kind)
{
    if(pt_write_armed!=2){ex_tests.refused=1;return -1;}
    struct cpg_overclock_setting saved,trial;
    cpg_get_overclock_setting(&saved);
    int original_wait=(saved.CS0WCR>>7)&15;
    int original_trc=saved.CS3WCR&3;
    int original_fll=saved.FLLFRQ&2047;
    int min,max,delta,total;
    if(kind==EX_TEST_ROM_WAIT_SWEEP) {
        max=12;min=pt.settings[PT_UNLOCKED]?0:original_wait;
        delta=1;total=max-min+1;
    }
    else if(kind==EX_TEST_SDRAM_TRC_SWEEP) {
        max=3;min=pt.settings[PT_UNLOCKED]?0:original_trc;
        delta=1;total=max-min+1;
    }
    else {
        min=original_fll;
        max=pt.settings[PT_UNLOCKED]?2047:original_fll+original_fll/5;
        if(max>2047)max=2047;
        delta=(max-min)/16;
        if(delta<1)delta=1;
        total=(max-min)/delta+1;
    }
    if(min>max || total<=0){ex_tests.refused=1;return -1;}
    int failed=0;
    for(int index=0;index<total;index++) {
        int step=kind==EX_TEST_FLL_SWEEP?min+index*delta:max-index;
        if(kind==EX_TEST_FLL_SWEEP && step>max)break;
        trial=saved;
        if(kind==EX_TEST_ROM_WAIT_SWEEP)
            trial.CS0WCR=(trial.CS0WCR&~(15u<<7))|((uint32_t)step<<7);
        else if(kind==EX_TEST_SDRAM_TRC_SWEEP)
            trial.CS3WCR=(trial.CS3WCR&~3u)|(uint32_t)step;
        else {
            trial.FLLFRQ=(trial.FLLFRQ&~2047u)|(uint32_t)step;
            if(step>original_fll) {
                trial.CS0WCR=(trial.CS0WCR&~(15u<<7))|(12u<<7);
                trial.CS3WCR=(trial.CS3WCR&~3u)|3u;
            }
        }
        step_screen(names[kind],index+1,total);
        if(cancelled()){ex_tests.cancelled=1;break;}
        cpg_set_overclock_setting(&trial);
        failed=verify_candidate();
        cpg_set_overclock_setting(&saved);
        if(failed)break;
        ex_tests.last_step=(uint32_t)step;
    }
    /* A hard hang cannot be caught; software restoration is best effort. */
    cpg_set_overclock_setting(&saved);
    return failed;
}
void ex_tests_run(int index)
{
    if(index<0||index>=EX_TEST_COUNT)return;
    int selected=ex_tests.selected;
    ex_tests=(ex_test_state){0};
    ex_tests.selected=selected;
    ex_tests.last_test=index;
    step_screen(names[index],1,1);
    switch(index) {
        case EX_TEST_RAM_QUICK:run_patterns(8);break;
        case EX_TEST_RAM_EXTENDED:run_patterns(256);break;
        case EX_TEST_ROM_CHECK:read_rom();break;
        case EX_TEST_SDRAM_MARCH:march_c();break;
        case EX_TEST_ADDRESS_LINES:address_lines();break;
        case EX_TEST_BENCHMARK:run_benchmark();break;
        case EX_TEST_ROM_WAIT_SWEEP:
        case EX_TEST_SDRAM_TRC_SWEEP:
        case EX_TEST_FLL_SWEEP:run_sweep(index);break;
        case EX_TEST_ALL_SAFE:
            if(!run_patterns(8)&&!march_c()&&!address_lines()&&!read_rom())
                run_benchmark();
            break;
        default:break;
    }
    ex_tests.complete=1;
    ex_tests.show_result=1;
}
void ex_tests_draw(void)
{
    if(ex_tests.show_result && ex_tests.complete) {
        char line[100];
        dtext(20,84,C_BLACK,names[ex_tests.last_test]);
        dtext(20,121,ex_tests.refused||ex_tests.errors?C_RED:C_BLUE,
            ex_tests.refused?"ARM REQUIRED":
            ex_tests.cancelled?"CANCELLED":
            ex_tests.errors?"FAILED":"PASSED");
        snprintf(line,sizeof line,"Completed passes: %lu",
            (unsigned long)ex_tests.passes);
        dtext(20,149,C_DARK,line);
        snprintf(line,sizeof line,"Bytes read/written: %lu",
            (unsigned long)ex_tests.bytes_checked);
        dtext(20,174,C_DARK,line);
        snprintf(line,sizeof line,"Detected errors: %d",ex_tests.errors);
        dtext(20,199,ex_tests.errors?C_RED:C_DARK,line);
        if(ex_tests.last_test==EX_TEST_BENCHMARK ||
            ex_tests.last_test==EX_TEST_ALL_SAFE) {
            snprintf(line,sizeof line,"CPU %lu us   ROM %lu us   RAM %lu us",
                (unsigned long)ex_tests.cpu_us,
                (unsigned long)ex_tests.rom_us,
                (unsigned long)ex_tests.ram_us);
            dtext(20,227,C_BLACK,line);
            snprintf(line,sizeof line,"LCD update %lu us",
                (unsigned long)ex_tests.display_us);
            dtext(20,251,C_BLACK,line);
        }
        else if(ex_tests.last_test==EX_TEST_ROM_WAIT_SWEEP ||
            ex_tests.last_test==EX_TEST_SDRAM_TRC_SWEEP ||
            ex_tests.last_test==EX_TEST_FLL_SWEEP) {
            snprintf(line,sizeof line,"Last completed register value: %lu",
                (unsigned long)ex_tests.last_step);
            dtext(20,227,C_BLUE,line);
        }
        dtext(20,274,C_DARK,"V: test list    EXE: repeat    ESC: menu");
        return;
    }
    dtext(20,78,C_DARK,"UP/DOWN select a test; EXE runs it.");
    for(int i=0;i<EX_TEST_COUNT;i++) {
        int y=103+i*16;
        if(ex_tests.selected==i)
            drect(15,y-3,DWIDTH-16,y+14,C_RGB(24,29,31));
        dtext(23,y,ex_tests.selected==i?C_WHITE:C_BLACK,names[i]);
    }
    if(ex_tests.complete) {
        char s[100];
        snprintf(s,sizeof s,"%s: %s   passes=%lu   errors=%d",
            names[ex_tests.last_test],
            ex_tests.refused?"ARM REQUIRED":ex_tests.cancelled?"CANCELLED":
            ex_tests.errors?"FAIL":"PASS",
            (unsigned long)ex_tests.passes,ex_tests.errors);
        dtext(20,271,ex_tests.errors||ex_tests.refused?C_RED:C_BLUE,s);
    }
    else dtext(20,271,C_RED,"* BSC/clock sweeps require S twice. May hard lock.");
}
