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
#endif
