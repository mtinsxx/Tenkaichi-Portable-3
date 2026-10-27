/*
 * PSP controller regression suite: raw inputs, sequences, menus and modifiers.
 * Runs on host GCC/Clang; deliberately does not depend on pspdev.
 */
#include "psp_controls.h"
#include <stdio.h>
#include <stdlib.h>

static PspControlsState state;
static uint8_t report[18];
static int assertions;

static unsigned ps2_buttons(void) {
    return ((unsigned)(~report[0]) & 0xffu) |
           (((unsigned)(~report[1]) & 0xffu) << 8);
}
static void fail(const char *name, unsigned want, unsigned got) {
    fprintf(stderr, "FAIL %-40s want=0x%04x got=0x%04x\n", name, want, got);
    exit(1);
}
static void check(const char *name, unsigned want, unsigned x, unsigned y) {
    unsigned got = ps2_buttons();
    ++assertions;
    if (got != want) fail(name, want, got);
    if (report[2] != x || report[3] != y || report[4] != 17 || report[5] != 239) {
        fprintf(stderr, "FAIL %-40s stick=(%u,%u,%u,%u) expected=(%u,%u,17,239)\n",
                name, report[2], report[3], report[4], report[5], x, y);
        exit(1);
    }
}
static void reset(void) { PspControls_Reset(&state); }
static void step(unsigned buttons, int profile, int fight) {
    PspControls_Encode(&state, buttons, profile, fight, 17, 239, report);
}
static void legacy_expect(unsigned input, unsigned expected) {
    reset();
    step(input, PSP_PROFILE_LEGACY, 1);
    check("legacy truth", expected, 128, 128);
    reset();
    step(input, PSP_PROFILE_PORTABLE, 0);
    check("portable paused / menus are legacy", expected, 128, 128);
}
/* Independent oracle matching the original Port_PadRead() button conversion. */
static unsigned legacy_original(unsigned in) {
    unsigned b = PspControls_Direction(in) | PspControls_Faces(in);
    if (in & PI_START) b |= PO_START;
    if (in & PI_SELECT) {
        if (in & PI_L) b |= PO_L2;
        if (in & PI_R) b |= PO_R2;
        if (in & PI_SQUARE) b = (b & ~PO_SQUARE) | PO_L3;
        if (in & PI_CIRCLE) b = (b & ~PO_CIRCLE) | PO_R3;
        if (!(in & (PI_L|PI_R|PI_SQUARE|PI_CIRCLE))) b |= PO_SELECT;
    } else {
        if (in & PI_L) b |= PO_L1;
        if (in & PI_R) b |= PO_R1;
    }
    return b;
}
static void portable_normal(unsigned input, unsigned expected) {
    reset();
    step(input, PSP_PROFILE_PORTABLE, 1);
    check("portable normal", expected, 128, 128);
}
static void chord(const char *name, unsigned input, unsigned expected, unsigned x, unsigned y) {
    reset();
    step(input, PSP_PROFILE_PORTABLE, 1);
    check(name, expected, x, y);
}

int main(void) {
    unsigned in;
    if (PSP_PROFILE_DEFAULT != PSP_PROFILE_PORTABLE) {
        fprintf(stderr, "FAIL expected embedded portable control default\n");
        return 1;
    }
    /* BT3 battle.h: pause=0x100, demo=0x200, ready=0x400,
       loading=0x2000, pause-menu=0x4000. */
    const uint64_t inactive = 0x6700ull;
    if (!PspControls_ActiveFight(3, 1, 0, inactive) ||
        PspControls_ActiveFight(3, 0, 0, inactive) ||
        PspControls_ActiveFight(2, 1, 0, inactive) ||
        PspControls_ActiveFight(3, 1, 0x100, inactive) ||
        PspControls_ActiveFight(3, 1, 0x200, inactive) ||
        PspControls_ActiveFight(3, 1, 0x400, inactive) ||
        PspControls_ActiveFight(3, 1, 0x2000, inactive) ||
        PspControls_ActiveFight(3, 1, 0x4000, inactive)) {
        fprintf(stderr, "FAIL active fight predicate or BT3 pause flags\n");
        return 1;
    }
    /* Exhaustive legacy and non-fight parity for every combination of PSP buttons. */
    for (in = 0; in < (1u << 12); ++in) {
        legacy_expect(in, legacy_original(in));
    }
    /* The fight-only scheme must still preserve every ordinary action. */
    portable_normal(0, 0);
    portable_normal(PI_START, PO_START);
    portable_normal(PI_TRIANGLE, PO_TRIANGLE);
    portable_normal(PI_CIRCLE, PO_CIRCLE);
    portable_normal(PI_CROSS, PO_CROSS);
    portable_normal(PI_SQUARE, PO_SQUARE);
    portable_normal(PI_L, PO_L2);
    portable_normal(PI_R, PO_L1);
    portable_normal(PI_UP, PO_R1);
    portable_normal(PI_DOWN, PO_R2);
    portable_normal(PI_LEFT, PO_LEFT);
    portable_normal(PI_RIGHT, PO_RIGHT);
    portable_normal(PI_L|PI_TRIANGLE, PO_L2|PO_TRIANGLE);
    portable_normal(PI_L|PI_UP|PI_TRIANGLE, PO_L2|PO_UP|PO_TRIANGLE);
    portable_normal(PI_L|PI_DOWN|PI_TRIANGLE, PO_L2|PO_DOWN|PO_TRIANGLE);
    portable_normal(PI_L|PI_CIRCLE|PI_DOWN, PO_L2|PO_CIRCLE|PO_DOWN);
    portable_normal(PI_L|PI_CROSS, PO_L2|PO_CROSS);
    portable_normal(PI_L|PI_R|PI_UP, PO_L2|PO_R1);
    portable_normal(PI_L|PI_R|PI_DOWN, PO_L2|PO_R2);
    portable_normal(PI_CROSS|PI_UP, PO_CROSS|PO_R1);

    /* SELECT chord priority, including all transformation directions. */
    chord("select+up for form", PI_SELECT|PI_UP, PO_R3|PO_UP, 128, 128);
    chord("select+down for form/fusion", PI_SELECT|PI_DOWN, PO_R3|PO_DOWN, 128, 128);
    chord("select+left for form/fusion", PI_SELECT|PI_LEFT, PO_R3|PO_LEFT, 128, 128);
    chord("select+right for form/fusion", PI_SELECT|PI_RIGHT, PO_R3|PO_RIGHT, 128, 128);
    chord("select+L+up, no unwanted L2", PI_SELECT|PI_L|PI_UP, PO_R3|PO_UP, 128, 128);
    chord("select+L+down, no unwanted L2", PI_SELECT|PI_L|PI_DOWN, PO_R3|PO_DOWN, 128, 128);
    chord("select+L alone is R3", PI_SELECT|PI_L, PO_R3, 128, 128);
    chord("select+square team", PI_SELECT|PI_SQUARE, PO_L3|PO_R3, 128, 128);
    chord("select+square+right team choice", PI_SELECT|PI_SQUARE|PI_RIGHT,
          PO_L3|PO_R3|PO_RIGHT, 128, 128);
    chord("select+triangle L3", PI_SELECT|PI_TRIANGLE, PO_L3, 128, 128);
    chord("select+circle PS2 SELECT", PI_SELECT|PI_CIRCLE, PO_SELECT, 128, 128);
    chord("select+R PS2 SELECT", PI_SELECT|PI_R, PO_SELECT, 128, 128);
    chord("select+START PS2 SELECT and START", PI_SELECT|PI_START,
          PO_SELECT|PO_START, 128, 128);
    chord("select+R+up camera", PI_SELECT|PI_R|PI_UP, 0, 128, 0);
    chord("select+R+down camera", PI_SELECT|PI_R|PI_DOWN, 0, 128, 255);
    chord("select+R+left camera", PI_SELECT|PI_R|PI_LEFT, 0, 0, 128);
    chord("select+R+right camera", PI_SELECT|PI_R|PI_RIGHT, 0, 255, 128);
    chord("select+R+up+right camera", PI_SELECT|PI_R|PI_UP|PI_RIGHT, 0, 255, 0);
    chord("select+R+contradictory camera", PI_SELECT|PI_R|PI_UP|PI_DOWN, 0, 128, 128);
    chord("select+L+R access R3+L2", PI_SELECT|PI_L|PI_R,
          PO_R3|PO_L2, 128, 128);
    chord("select+L+R+up access R3+L2+UP", PI_SELECT|PI_L|PI_R|PI_UP,
          PO_R3|PO_L2|PO_UP, 128, 128);
    chord("select+L+cross leaves L2 off", PI_SELECT|PI_L|PI_CROSS,
          PO_R3|PO_CROSS, 128, 128);

    /* Sequence: quick SELECT tap must emit a one-frame R3 press on release. */
    reset();
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("quick tap pending 1", 0, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("quick tap pending 2", 0, 128, 128);
    step(0, PSP_PROFILE_PORTABLE, 1); check("quick tap R3 pulse", PO_R3, 128, 128);
    step(0, PSP_PROFILE_PORTABLE, 1); check("quick tap releases R3", 0, 128, 128);

    /* Sequence: sustained SELECT gives held R3 after a short chord window. */
    reset();
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("hold pending 1", 0, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("hold pending 2", 0, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("hold R3 activates", PO_R3, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("hold R3 sustained", PO_R3, 128, 128);
    step(0, PSP_PROFILE_PORTABLE, 1); check("hold R3 releases", 0, 128, 128);

    /* Sequence: R3 must NEVER flicker before a 1-2 sample fusion/team chord. */
    reset();
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("fusion select pending", 0, 128, 128);
    step(PI_SELECT|PI_L|PI_UP, PSP_PROFILE_PORTABLE, 1);
    check("fusion select + L + up", PO_R3|PO_UP, 128, 128);
    step(PI_SELECT|PI_L, PSP_PROFILE_PORTABLE, 1);
    check("fusion direction released", PO_R3, 128, 128);
    step(0, PSP_PROFILE_PORTABLE, 1); check("fusion releases", 0, 128, 128);

    reset();
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("team pending", 0, 128, 128);
    step(PI_SELECT|PI_SQUARE, PSP_PROFILE_PORTABLE, 1);
    check("team chord", PO_L3|PO_R3, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1);
    check("team stays latched after Square released", PO_L3|PO_R3, 128, 128);
    step(0, PSP_PROFILE_PORTABLE, 1); check("team releases", 0, 128, 128);

    reset();
    step(PI_SELECT|PI_CIRCLE, PSP_PROFILE_PORTABLE, 1);
    check("passthrough chord", PO_SELECT, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1);
    check("passthrough never becomes R3", PO_SELECT, 128, 128);
    step(0, PSP_PROFILE_PORTABLE, 1); check("passthrough release", 0, 128, 128);

    reset();
    step(PI_SELECT|PI_R, PSP_PROFILE_PORTABLE, 1);
    check("right accessory first maps SELECT", PO_SELECT, 128, 128);
    step(PI_SELECT|PI_R|PI_UP, PSP_PROFILE_PORTABLE, 1);
    check("right accessory grows into camera", 0, 128, 0);
    step(PI_SELECT|PI_R, PSP_PROFILE_PORTABLE, 1);
    check("right accessory back to SELECT", PO_SELECT, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1);
    check("right accessory never falls into R3", PO_SELECT, 128, 128);

    /* Pause menu: automatic legacy, then prevent held SELECT becoming R3 on resume. */
    reset();
    step(PI_UP, PSP_PROFILE_PORTABLE, 1); check("live D-pad up flies", PO_R1, 128, 128);
    step(PI_UP, PSP_PROFILE_PORTABLE, 0); check("pause menu UP navigates", PO_UP, 128, 128);
    step(PI_DOWN, PSP_PROFILE_PORTABLE, 0); check("pause menu DOWN navigates", PO_DOWN, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 0); check("pause SELECT original", PO_SELECT, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("resume held SELECT suppressed", 0, 128, 128);
    step(PI_SELECT, PSP_PROFILE_PORTABLE, 1); check("resume held SELECT stays suppressed", 0, 128, 128);
    step(0, PSP_PROFILE_PORTABLE, 1); check("resume SELECT release clears suppression", 0, 128, 128);
    step(PI_SELECT|PI_RIGHT, PSP_PROFILE_PORTABLE, 1);
    check("new SELECT chord after resume", PO_R3|PO_RIGHT, 128, 128);

    /* All physical masks: output is 2-byte active-low, analog passthrough. */
    for (in = 0; in < (1u << 12); ++in) {
        reset();
        step(in, PSP_PROFILE_PORTABLE, 1);
        if (report[4] != 17 || report[5] != 239) {
            fprintf(stderr, "FAIL analog passthrough for input mask 0x%x\n", in);
            return 1;
        }
    }
    printf("PSP controller mapping: PASS (%d assertions + 4096 input masks)\n", assertions);
    return 0;
}
