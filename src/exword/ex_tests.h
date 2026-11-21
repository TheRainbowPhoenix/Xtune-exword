#ifndef EXTUNE_TEST_SUITE_H
#define EXTUNE_TEST_SUITE_H
#include <stdint.h>
enum {
    EX_TEST_RAM_QUICK, EX_TEST_RAM_EXTENDED, EX_TEST_ROM_CHECK,
    EX_TEST_SDRAM_MARCH, EX_TEST_ADDRESS_LINES, EX_TEST_BENCHMARK,
    EX_TEST_ROM_WAIT_SWEEP, EX_TEST_SDRAM_TRC_SWEEP, EX_TEST_FLL_SWEEP,
    EX_TEST_ALL_SAFE, EX_TEST_COUNT
};
typedef struct {
    int selected, last_test, complete, errors, refused, cancelled, show_result;
    uint32_t passes, bytes_checked, last_step;
    uint32_t cpu_us, rom_us, ram_us, display_us;
} ex_test_state;
extern ex_test_state ex_tests;
const char *ex_test_name(int index);
void ex_tests_draw(void);
void ex_tests_select(int change);
void ex_tests_run(int index);
#endif
