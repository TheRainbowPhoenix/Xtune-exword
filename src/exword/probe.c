/*
 * Standalone boot probe for EX-word CN/JA packages.
 * Keep this independent of JustUI, CPG, BSC, prof and stdio, so a successful
 * launch isolates the hardware launcher and gint screen/keyboard path.
 */
#include <gint/config.h>
#include <gint/display.h>
#include <gint/keyboard.h>

#if !GINT_HW_EX
#error "This probe must be built for EX-word"
#endif

int main(void)
{
    dclear(C_WHITE);
    drect(0, 0, DWIDTH-1, 35, C_RGB(3, 9, 16));
    dtext(12, 13, C_WHITE, "EXTune - EX-word runtime probe");
    dtext(24, 80, C_BLACK, "PASS: the EX-word module reached main().");
    dtext(24, 112, C_BLACK, "PASS: gint display update succeeded.");
    dtext(24, 164, C_DARK, "No JustUI, timer or clock-register access.");
    dtext(24, 235, C_BLUE, "Press ESC to return to the dictionary menu.");
    dupdate();

    while(1) {
        key_event_t ev = waitevent(NULL);
        if(ev.type == KEYEV_DOWN && ev.key == KEY_EXIT) return -2;
    }
}
