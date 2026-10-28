#ifndef TP3_PSP_FB_SNAPSHOT_RECYCLE_H
#define TP3_PSP_FB_SNAPSHOT_RECYCLE_H
#include "psp_gs_fb_reconstruct.h"
/* Reuse the *allocation* (not its old pixels) only when the previous
 * logical image is demonstrably stale and its last GE use/copy was
 * synchronized in a previous frame. Valid snapshots of old WORK
 * owners must survive even after the physical WORK slot is replaced. */
static inline int PspGsFb_CanRecycleSnapshot(int hasAllocation,
                  int targetUsed, const PspGsFbVersion *v,
                  uint32_t targetGeneration, uint32_t gsGeneration,
                  unsigned sampledFrame, unsigned now) {
    if (!hasAllocation || !v || sampledFrame == now ||
        (v->valid && v->frame == now))
        return 0;
    if (!targetUsed) return 1;
    if (targetGeneration != gsGeneration) return 1;
    return !PspGsFb_SampleValid(v,targetGeneration);
}
#endif
