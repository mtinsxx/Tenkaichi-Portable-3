#ifndef TP3_PSP_GS_AFAIL_H
#define TP3_PSP_GS_AFAIL_H
#include <stdint.h>
/* PS2 GS TEST.ATST compares source alpha with AREF.
 * To render failed fragments with the PSP GU, the second geometry pass
 * uses the inverse comparator. The GS reference is gs_core.c:pixel().
 * PSP alpha is scaled from 0..128 to 0..255 and may be quantized.
 * The selection is exact algebraically, but NOT byte-exact GS alpha. */
static inline unsigned PspGs_InverseAtst(unsigned atst) {
    static const uint8_t opposite[8] = {1,0,5,6,7,2,3,4};
    return opposite[atst&7u];
}
static inline unsigned PspGs_AfailNeedsReplay(unsigned ate,
                                              unsigned afail) {
    return ate && (afail&3u)!=0u;
}
/* 0 KEEP: no second pass. 1 FB_ONLY: only colour, not Z.
 * 2 ZB_ONLY: Z and no colour. 3 RGB_ONLY: RGB, not alpha or Z.
 * The PSP's framebuffer is RGB5551; bit 31 masks its one-bit alpha. */
static inline uint32_t PspGs_AfailPassMask(unsigned afail) {
    return (afail&3u)==2u?UINT32_C(0xFFFFFFFF):
           (afail&3u)==3u?UINT32_C(0x80000000):0u;
}
static inline int PspGs_AfailWritesZ(unsigned afail) {
    return (afail&3u)==2u;
}
#endif
