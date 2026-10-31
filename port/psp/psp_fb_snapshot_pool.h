#ifndef TP3_PSP_FB_SNAPSHOT_POOL_H
#define TP3_PSP_FB_SNAPSHOT_POOL_H
#include <stddef.h>
#include <stdint.h>
#include "psp_gs_fb_reconstruct.h"

/* Main-RAM GE snapshots are independent of the PSP's 2 MiB physical
 * VRAM. Reserve their maximum 1.5 MiB before dynamic textures and GS
 * work tables fragment the allocator; never resize a live GE texture.
 * Returned aligned slices are reused only under the existing target
 * version / in-flight GE fencing rules. This is a *5551 staging* pool,
 * NOT a surrogate for the 32-bit PS2 GS byte-addressable framebuffer.
 */
#define TP3_FB_POOL_ALIGN 64u
#define TP3_FB_POOL_SLOTS 3u
static inline size_t PspGsFb_PoolBytes(unsigned slots) {
    return (size_t)slots * (size_t)PSP_GS_FB_SNAPSHOT_BYTES +
           (size_t)(TP3_FB_POOL_ALIGN-1u);
}
static inline void *PspGsFb_PoolAligned(void *raw) {
    if (!raw) return NULL;
    return (void *)(((uintptr_t)raw + (TP3_FB_POOL_ALIGN-1u)) &
                    ~((uintptr_t)TP3_FB_POOL_ALIGN-1u));
}
static inline void *PspGsFb_PoolSlot(void *aligned,
                                      unsigned index,
                                      unsigned slots) {
    if (!aligned || !slots || slots>TP3_FB_POOL_SLOTS || index>=slots)
        return NULL;
    return (void *)((uintptr_t)aligned +
                    (size_t)index * (size_t)PSP_GS_FB_SNAPSHOT_BYTES);
}
#endif
