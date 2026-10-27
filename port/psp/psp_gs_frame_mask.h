#ifndef TP3_PSP_GS_FRAME_MASK_H
#define TP3_PSP_GS_FRAME_MASK_H
#include <stdint.h>
/* Sony PS2 GS FRAME.FBMSK is a 32-bit write-protection mask in
 * R,G,B,A byte order. The game's GS software reference masks after
 * computing the fragment but before storing its packed color.
 *
 * This port draws into PSP RGB5551. GU sceGuPixelMask accepts an
 * RGBA 8-bit/channel WRITE-PROTECTION mask, with 1=preserve, like GS.
 * The PSP has only five meaningful bits for each color channel and
 * one alpha bit: use the most significant 5/1 mask bits.
 *
 * PSMCT24 does not write the stored alpha byte at all.
 * Sub-5-bit masking and 8-bit GS destination alpha cannot be
 * represented by 5551; this is a loss-limited translation, not
 * byte-exact GS feedback for framebuffer-as-texture passes. */
static inline uint32_t PspGs_FrameMaskToGu(uint64_t frame) {
    uint32_t m=(uint32_t)(frame>>32);
    unsigned psm=(unsigned)((frame>>24)&63u);
    uint32_t out=m & UINT32_C(0x80F8F8F8);
    if(psm==1u) out|=UINT32_C(0xFF000000);
    return out;
}
static inline int PspGs_FrameMaskPartial(uint32_t m) {
    return (m & UINT32_C(0x00FFFFFF)) != 0 &&
           (m & UINT32_C(0x00FFFFFF)) != UINT32_C(0x00FFFFFF);
}
#endif
