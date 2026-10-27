#ifndef BT3_PSP_SCENE_PAIR_H
#define BT3_PSP_SCENE_PAIR_H
#include <stdint.h>
/* The paired FBP is obtained from the *same* PS2 sceGsSwapDBuff call.
   Only the two known display surfaces map to PSP SCREEN. Generic
   auxiliary GS targets remain in WORK. */
static inline int Psp_GsScenePairIsScreen(uint32_t fbp, int current,
                                           int paired, int enable) {
    if (current < 0) return 1; /* legacy boot-time path */
    if (fbp == (uint32_t)current) return 1;
    return enable && paired >= 0 && paired != current &&
           fbp == (uint32_t)paired;
}
/* Two PS2 libgraph display FBPs represent different images. The paired
   *previous* picture lives in the PSP's other physical screen buffer.
   Keep auxiliary FBPs in WORK, never sharing the active screen. */
static inline unsigned Psp_GsScenePairBackingOffset(
        uint32_t fbp, int current, int paired, int enable, int separate,
        unsigned draw, unsigned other, unsigned work) {
    if (current < 0 || fbp == (uint32_t)current) return draw;
    if (enable && paired >= 0 && paired != current &&
        fbp == (uint32_t)paired) return separate ? other : draw;
    return work;
}
#endif
