#ifndef TP3_PSP_VERTEX_ARENA_H
#define TP3_PSP_VERTEX_ARENA_H
#include <stdint.h>

/* sceGuDrawArray stores the vertex POINTER in a GE command; it does
 * not copy the current vertex bytes. Every submitted span must remain
 * unchanged until sceGuSync. A reused 8192-vertex scratch buffer
 * corrupted previously queued triangles (including stage/HUD data).
 * Keep non-overlapping spans in one frame-owned RAM arena instead.
 * When the arena fills, finish/sync the command list before recycling.
 */
#define PSP_GU_VERTEX_FRAME_VERTICES (96u * 1024u)
/* Hardware GU still receives ordinary triangles; increase the maximum
 * identical-state draw-array group, not polygon count or VRAM usage.
 * Keep count within the 16-bit GE vertex-count field. */
#define PSP_GU_VERTEX_BATCH_VERTICES (12u * 1024u)

static inline int PspGuVertexArena_Fits(unsigned committed,
                                        unsigned pending,
                                        unsigned incoming,
                                        unsigned capacity) {
    return committed <= capacity &&
           pending <= capacity - committed &&
           incoming <= capacity - committed - pending;
}
static inline unsigned PspGuVertexArena_Commit(unsigned committed,
                                                unsigned pending) {
    return committed + pending;
}
static inline int PspGuVertexArena_CanReuse(unsigned synced) {
    return synced != 0u;
}
#endif
