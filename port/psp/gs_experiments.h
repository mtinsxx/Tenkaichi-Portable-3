/* PSP-only, opt-in GS conversion helpers. Pure integer arithmetic so the
   same code can be tested by host gcc and compiled by psp-gcc. */
#ifndef BT3_PSP_GS_EXPERIMENTS_H
#define BT3_PSP_GS_EXPERIMENTS_H
#include <stdint.h>

/* PS2 TEX0.TFX=DECAL outputs texture RGB regardless of texture
   alpha; TCC=RGB keeps vertex alpha and TCC=RGBA takes texture alpha.
   PSP GU_TFX_DECAL instead mixes texture and fragment RGB when
   TCC=RGBA; GU_TFX_REPLACE is equivalent to the PS2 rule.
   These are the public numeric GU_TFX constants in pspsdk/pspgu.h:
   MODULATE=0, BLEND=2, REPLACE=3. Use literal values so the pure
   conversion header also compiles with host gcc, as earlier GS tests
   do without PSP headers. */
static inline int Psp_GsTexFuncToGu(unsigned tfx) {
    switch (tfx & 3u) {
    case 0: return 0; /* GU_TFX_MODULATE */
    case 1: return 3; /* GU_TFX_REPLACE */
    default: return 2; /* GU_TFX_BLEND; HIGHLIGHT still incomplete */
    }
}

/* PS2 GS: FIX=0x80 represents full coverage; GU_BLEND FIX uses 0xFF. */
static inline unsigned Psp_GsFixToGu(unsigned fix) {
    fix &= 0xFFu;
    return fix >= 0x80u ? 0xFFu : fix * 2u;
}

/* GS TEX0: when indexed and requesting neither a new palette nor a CBP,
   the previously selected palette registers may be used. This only
   substitutes CBP/CPSM/CSM/CSA (bits 37..60). CLD (61..63) is left alone.
   Does not modify the live GS registers, only the effective texture key. */
static inline uint64_t Psp_GsEffectiveClut(uint64_t tex0, uint64_t last) {
    unsigned psm = (unsigned)((tex0 >> 20) & 0x3Fu);
    const uint64_t mask = 0x1FFFFFE000000000ull;
    if (last == 0 ||
        ((tex0 >> 61) & 7u) != 0 ||
        ((tex0 >> 37) & 0x3FFFu) != 0 ||
        (psm != 0x13u && psm != 0x14u && psm != 0x1Bu &&
         psm != 0x24u && psm != 0x2Cu))
        return tex0;
    return (tex0 & ~mask) | (last & mask);
}
#endif
