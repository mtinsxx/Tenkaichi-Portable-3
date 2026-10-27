#ifndef BT3_PSP_VU_PROFILE_H
#define BT3_PSP_VU_PROFILE_H

/* PSP VU1 interpreter attribution only. The numbers are the PS2 VU1
   microprogram lengths actually uploaded by BT3; no game behavior,
   memory layout, character geometry or shader dispatch changes. */
enum {
    PSP_VU_BUCKET_STAGE = 0,   /* 399 insns: stage transforms/clip */
    PSP_VU_BUCKET_FIGHTER = 1, /* 127: 2-bone fighter */
    PSP_VU_BUCKET_FADE = 2,    /* 124: cel/fade fighter */
    PSP_VU_BUCKET_EFFECT = 3,  /* 451: scene/effects */
    PSP_VU_BUCKET_SHADOW = 4,  /* 99 or 101: fighter shadow */
    PSP_VU_BUCKET_MISC_HLE = 5,/* 94 or 104: other recognized HLE */
    PSP_VU_BUCKET_OTHER = 6,
    PSP_VU_BUCKET_COUNT = 7
};
static inline unsigned Psp_VuBucket(unsigned size) {
    switch (size) {
    case 399: return PSP_VU_BUCKET_STAGE;
    case 127: return PSP_VU_BUCKET_FIGHTER;
    case 124: return PSP_VU_BUCKET_FADE;
    case 451: return PSP_VU_BUCKET_EFFECT;
    case 99: case 101: return PSP_VU_BUCKET_SHADOW;
    case 94: case 104: return PSP_VU_BUCKET_MISC_HLE;
    default: return PSP_VU_BUCKET_OTHER;
    }
}
/* Only the interpreter's run() is tracked; native HLE bypasses are not
   counted as CPU interpreter time. Times are approximate 1/16 sampled
   elapsed microseconds; never add them to the GSchain wall time. */
typedef struct PspVuProfileBucket {
    unsigned runs;
    unsigned instructions;
    unsigned samples;
    unsigned estimated_ms;
} PspVuProfileBucket;
#endif
