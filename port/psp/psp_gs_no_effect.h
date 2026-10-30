#ifndef TP3_PSP_GS_NO_EFFECT_H
#define TP3_PSP_GS_NO_EFFECT_H
#include <stdint.h>
/* PS2 GS guaranteed no-write cases, independent of texture, FBP, Z,
 * DATE, blending and primitive kind. These predicates remove ONLY
 * primitives which cannot change any byte of GS colour or depth:
 *
 * (1) FRAME.FBMSK masks all 32 colour bits and ZBUF.ZMSK disables all
 *     depth writes. Colour/depth tests may read memory but are pure.
 * (2) TEST.ATE enabled + TEST.ATST=NEVER + TEST.AFAIL=KEEP discards every
 *     fragment before colour or depth writes; the other AFAIL modes
 *     must not be culled.
 *
 * Return 1 for all-masked, 2 for guaranteed alpha discard, 0 otherwise.
 * Colour-only / depth-only draws are deliberately kept. */
static inline int PspGs_GuaranteedNoEffect(uint64_t frame,
                                         uint64_t zbuf,
                                         uint64_t test) {
    if ((uint32_t)(frame >> 32)==UINT32_MAX &&
        ((zbuf >> 32)&UINT64_C(1))!=0u)
        return 1;
    if ((test&UINT64_C(1))!=0u &&
        ((test >> 1)&UINT64_C(7))==0u &&
        ((test >> 12)&UINT64_C(3))==0u)
        return 2;
    return 0;
}
#endif
