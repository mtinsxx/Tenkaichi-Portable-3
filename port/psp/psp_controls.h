#ifndef PSP_CONTROLS_H
#define PSP_CONTROLS_H
/*
 * Portable PSP physical-pad -> PS2 libpad mapper.
 * Pure C99, independent of PSPSDK, with deterministic state for SELECT gestures.
 * Legacy mode is a byte-for-byte replacement for the port's original mapping.
 */
#include <stdint.h>
#include <string.h>

enum {
    PI_SELECT = 1u << 0, PI_START = 1u << 1,
    PI_UP = 1u << 2, PI_RIGHT = 1u << 3, PI_DOWN = 1u << 4, PI_LEFT = 1u << 5,
    PI_L = 1u << 6, PI_R = 1u << 7,
    PI_TRIANGLE = 1u << 8, PI_CIRCLE = 1u << 9,
    PI_CROSS = 1u << 10, PI_SQUARE = 1u << 11
};
enum {
    PO_SELECT = 0x0001, PO_L3 = 0x0002, PO_R3 = 0x0004, PO_START = 0x0008,
    PO_UP = 0x0010, PO_RIGHT = 0x0020, PO_DOWN = 0x0040, PO_LEFT = 0x0080,
    PO_L2 = 0x0100, PO_R2 = 0x0200, PO_L1 = 0x0400, PO_R1 = 0x0800,
    PO_TRIANGLE = 0x1000, PO_CIRCLE = 0x2000, PO_CROSS = 0x4000, PO_SQUARE = 0x8000
};
enum { PSP_PROFILE_LEGACY = 0, PSP_PROFILE_PORTABLE = 1 };
enum {
    PSP_GESTURE_NONE = 0,
    PSP_GESTURE_PENDING,     /* wait briefly to see if a modifier follows SELECT */
    PSP_GESTURE_R3,          /* SELECT held alone or SELECT+direction: transformation */
    PSP_GESTURE_TEAM,        /* SELECT+Square: simultaneous L3+R3 */
    PSP_GESTURE_L3,          /* SELECT+Triangle: L3 */
    PSP_GESTURE_PS2_SELECT,  /* SELECT+Circle, SELECT+R, or SELECT+START */
    PSP_GESTURE_CAMERA       /* SELECT+R: PS2 SELECT; +D-pad: virtual right stick */
};
enum { PSP_SELECT_GRACE_SAMPLES = 3 };

typedef struct PspControlsState {
    unsigned char gesture;
    unsigned char wait_samples;
    unsigned char suppress_until_release;
} PspControlsState;

static inline void PspControls_Reset(PspControlsState *s) {
    memset(s, 0, sizeof(*s));
}
/* Pure fight context predicate: flags/mask come from battle/battle.h. */
static inline int PspControls_ActiveFight(int seq_state, int running,
                                          uint64_t flags, uint64_t inactive_mask) {
    return seq_state == 3 && running != 0 && (flags & inactive_mask) == 0;
}
static inline unsigned PspControls_Direction(unsigned in) {
    unsigned bits = 0;
    if (in & PI_UP) bits |= PO_UP;
    if (in & PI_DOWN) bits |= PO_DOWN;
    if (in & PI_LEFT) bits |= PO_LEFT;
    if (in & PI_RIGHT) bits |= PO_RIGHT;
    return bits;
}
static inline unsigned PspControls_Faces(unsigned in) {
    unsigned bits = 0;
    if (in & PI_TRIANGLE) bits |= PO_TRIANGLE;
    if (in & PI_CIRCLE) bits |= PO_CIRCLE;
    if (in & PI_CROSS) bits |= PO_CROSS;
    if (in & PI_SQUARE) bits |= PO_SQUARE;
    return bits;
}
static inline unsigned PspControls_Legacy(unsigned in) {
    unsigned btn = PspControls_Faces(in) | PspControls_Direction(in);
    if (in & PI_START) btn |= PO_START;
    if (in & PI_SELECT) {
        if (in & PI_L) btn |= PO_L2;
        if (in & PI_R) btn |= PO_R2;
        if (in & PI_SQUARE) btn = (btn & ~PO_SQUARE) | PO_L3;
        if (in & PI_CIRCLE) btn = (btn & ~PO_CIRCLE) | PO_R3;
        if (!(in & (PI_L | PI_R | PI_SQUARE | PI_CIRCLE))) btn |= PO_SELECT;
    } else {
        if (in & PI_L) btn |= PO_L1;
        if (in & PI_R) btn |= PO_R1;
    }
    return btn;
}
static inline unsigned PspControls_PortableNormal(unsigned in) {
    unsigned btn = PspControls_Faces(in);
    if (in & PI_START) btn |= PO_START;
    if (in & PI_L) btn |= PO_L2;       /* charge Ki */
    if (in & PI_R) btn |= PO_L1;       /* lock-on */
    if (in & PI_LEFT) btn |= PO_LEFT;
    if (in & PI_RIGHT) btn |= PO_RIGHT;
    if (in & (PI_UP | PI_DOWN)) {
        if ((in & PI_L) && !(in & PI_R)) {
            /* Original PS2 D-pad for charge+direction+Ki Blast supers/ultimate. */
            if (in & PI_UP) btn |= PO_UP;
            if (in & PI_DOWN) btn |= PO_DOWN;
        } else {
            if (in & PI_UP) btn |= PO_R1;   /* ascend */
            if (in & PI_DOWN) btn |= PO_R2; /* descend */
            if (in & PI_L) btn &= ~PO_L1;  /* L+R+dir: charged flight, not lock-on */
        }
    }
    return btn;
}
static inline unsigned PspControls_ClassifyChord(unsigned in) {
    if (in & PI_START) return PSP_GESTURE_PS2_SELECT;
    if ((in & (PI_L | PI_R)) == (PI_L | PI_R)) return PSP_GESTURE_R3;
    if (in & PI_R) return PSP_GESTURE_CAMERA;
    if (in & PI_CIRCLE) return PSP_GESTURE_PS2_SELECT;
    if (in & PI_TRIANGLE) return PSP_GESTURE_L3;
    if (in & PI_SQUARE) return PSP_GESTURE_TEAM;
    if (in & (PI_L | PI_UP | PI_DOWN | PI_LEFT | PI_RIGHT | PI_CROSS))
        return PSP_GESTURE_R3;
    return PSP_GESTURE_PENDING;
}

/*
 * Each call consumes ONE PSP controller sample. Pass the persistent state from
 * psp_gfx.c. in_fight MUST be false for paused menus, transitions and cutscenes.
 *
 * SELECT alone waits up to 3 samples before starting held R3. A shorter tap
 * emits a one-sample R3 pulse when released. This avoids accidental R3 when
 * SELECT precedes another chord by 1-2 input samples. Once a chord is chosen,
 * it remains latched until SELECT is released so that releasing a modifier
 * does not accidentally transform. The 3-sample window cannot protect against
 * a modifier pressed *much* later: validate timing on physical PSP/PPSSPP.
 */
static inline void PspControls_Encode(PspControlsState *s, unsigned in, int profile,
                                      int in_fight, uint8_t lx, uint8_t ly,
                                      uint8_t out[18]) {
    unsigned btn = 0, dir = PspControls_Direction(in);
    uint8_t rx = 0x80, ry = 0x80;
    const int portable = profile == PSP_PROFILE_PORTABLE && in_fight;
    memset(out, 0, 18);
    if (!portable) {
        /* Preserve menus and cutscenes exactly, even in portable mode. */
        btn = PspControls_Legacy(in);
        s->gesture = PSP_GESTURE_NONE;
        s->wait_samples = 0;
        if (in & PI_SELECT) s->suppress_until_release = 1;
        else s->suppress_until_release = 0;
    } else if (!(in & PI_SELECT)) {
        /* SELECT released: finish a short solitary tap with a real R3 pulse. */
        btn = PspControls_PortableNormal(in);
        if (s->gesture == PSP_GESTURE_PENDING && !s->suppress_until_release) {
            btn |= PO_R3;
        }
        s->gesture = PSP_GESTURE_NONE;
        s->wait_samples = 0;
        s->suppress_until_release = 0;
    } else if (s->suppress_until_release) {
        /* SELECT was already held while a menu closed: require release first. */
        btn = PspControls_PortableNormal(in & ~PI_SELECT);
    } else {
        if (s->gesture == PSP_GESTURE_NONE) {
            s->gesture = PSP_GESTURE_PENDING;
            s->wait_samples = 0;
        }
        if (s->gesture == PSP_GESTURE_PENDING) {
            unsigned chosen = PspControls_ClassifyChord(in & ~PI_SELECT);
            if (chosen != PSP_GESTURE_PENDING) {
                s->gesture = (unsigned char)chosen;
            } else if (++s->wait_samples >= PSP_SELECT_GRACE_SAMPLES) {
                s->gesture = PSP_GESTURE_R3;
            }
        }
        /* Chord inputs are intentionally consumed here: face buttons do not
           punch, guard, Ki Blast or dash when acting as chord selectors. */
        switch (s->gesture) {
        case PSP_GESTURE_R3:
            btn = PO_R3 | dir; /* transformation/fusion uses game's actual R3 + direction */
            if (in & PI_CROSS) btn |= PO_CROSS; /* optional R3 + dash */
            /* Explicit advanced chord for R3+L2 without adding direction:
               SELECT+L+R. Regular SELECT+L+direction stays clean R3+dir. */
            if ((in & (PI_L | PI_R)) == (PI_L | PI_R)) btn |= PO_L2;
            break;
        case PSP_GESTURE_TEAM:
            btn = PO_L3 | PO_R3 | dir;
            break;
        case PSP_GESTURE_L3:
            btn = PO_L3 | dir;
            break;
        case PSP_GESTURE_PS2_SELECT:
            btn = PO_SELECT | dir;
            break;
        case PSP_GESTURE_CAMERA:
            /* R pressed before D-pad still works; camera never emits R3.
               With no direction, expose original PS2 SELECT. */
            if (!dir) btn |= PO_SELECT;
            if (in & PI_LEFT) rx = 0x00;
            if (in & PI_RIGHT) rx = 0xff;
            if (in & PI_UP) ry = 0x00;
            if (in & PI_DOWN) ry = 0xff;
            if ((in & PI_LEFT) && (in & PI_RIGHT)) rx = 0x80;
            if ((in & PI_UP) && (in & PI_DOWN)) ry = 0x80;
            break;
        default: /* pending: don't leak R3 or SELECT into the game */
            break;
        }
        if (in & PI_START) btn |= PO_START; /* physical pause always works */
    }
    out[0] = (uint8_t)~btn;
    out[1] = (uint8_t)~(btn >> 8);
    out[2] = rx;
    out[3] = ry;
    out[4] = lx;
    out[5] = ly;
}
#endif
