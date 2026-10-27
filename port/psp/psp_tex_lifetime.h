#ifndef PSP_TEX_LIFETIME_H
#define PSP_TEX_LIFETIME_H
#include <stdint.h>

/* GsGpu_FrameEnd() always finishes and synchronizes the GE command list
   BEFORE advancing sFrame. A texture last used in an older frame
   cannot be referenced by any outstanding GE draw command. A texture
   used in the current frame may still be queued and MUST be fenced
   before its backing memory can be returned to malloc. The >= case
   is intentionally conservative across invalid/future timestamps. */
static inline int Psp_TexNeedsGeFence(unsigned lastUsed, unsigned currentFrame) {
    return lastUsed >= currentFrame;
}

/* Both buffers are recyclable decode STAGING, never the cached
   swizzled GPU output. We reserve at startup while heap is available
   and subtract the reservation from the normal RAM texture budget. */
#define PSP_TEX_SCRATCH_RGBA_BYTES (512u * 512u * 4u)
#define PSP_TEX_SCRATCH_INDEX_BYTES (512u * 512u)
static inline unsigned Psp_TexRamBudget(unsigned nominal, unsigned reserved) {
    return reserved < nominal ? nominal - reserved : 0u;
}
static inline int Psp_TexUseScratch(unsigned bytes, unsigned available) {
    return available && bytes <= available;
}
#endif
