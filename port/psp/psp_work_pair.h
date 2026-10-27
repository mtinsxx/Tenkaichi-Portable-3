#ifndef BT3_PSP_WORK_PAIR_H
#define BT3_PSP_WORK_PAIR_H
#include <stdint.h>
/*
 * The BT3 battle's GS offscreen FBPs 336 and 368 are distinct snapshots,
 * rendered alternately and later sampled. Previously they shared the
 * same physical 512x272 PSP GU surface, so writing 368 invalidated 336.
 * Preserve one physical PSP surface per observed battle FBP.
 * Other non-display FBPs conservatively share scratch slot 0 and keep
 * the existing strict overwrite/alias invalidation.
 */
#define PSP_WORK_SURFACES 2u
static inline unsigned PspWork_Slot(uint32_t fbp) {
    return fbp == 368u ? 1u : 0u;
}
static inline int PspWork_OwnerAllowsSample(uint32_t owner,
                                            uint32_t fbp) {
    return owner == fbp;
}
#endif
