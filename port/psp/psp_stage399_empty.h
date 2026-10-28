#ifndef TP3_PSP_STAGE399_EMPTY_H
#define TP3_PSP_STAGE399_EMPTY_H
#include <stdint.h>
/*
 * PS2 GS TRISTRIP (PRIM.PRIM == 4) cannot produce a triangle with
 * fewer than three vertices. Stage399 HLE can still replay its GIF A+D
 * texture and PRIM register writes, but may avoid entering the full
 * VU1 interpreter for a strictly bounded degenerate strip.
 *
 * No guesses about non-TRISTRIP primitives, zero-copy framebuffer,
 * alpha masks, or 3+ vertex meshes: they stay on the previous path.
 */
static inline int PspStage399_EmptyStrip(uint32_t top,
                                         uint64_t primitiveTag) {
    uint32_t count = (uint32_t)(primitiveTag & 0x7FFFu);
    uint32_t prim = (uint32_t)((primitiveTag >> 47u) & 7u);
    return count < 3u && prim == 4u && top <= 1024u - 3u &&
           count <= (1024u - top - 3u) / 3u;
}
#endif
