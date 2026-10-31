#ifndef TP3_PSP_MIRROR_DEPTH_ONLY_H
#define TP3_PSP_MIRROR_DEPTH_ONLY_H
#include <stdint.h>
/* Native PSP CPU translation of GS depth-only raster operations.
 *
 * Strictly requires:
 * - FRAME.FBMSK masks all 32 colour bits, including destination alpha.
 * - TEST.ATE is disabled: the source texture/vertex colour is never
 *   consulted to decide whether the Z test/write executes.
 * - ZBUF.ZMSK is clear: depth still needs to be produced.
 * - A still-valid Z-test/depth mode; its exact byte-addressable PS2
 *   storage continues to be handled by vram_rw, NOT by lossy 5551.
 *
 * Geometry, scissor, barycentric Z and test ordering stay unchanged;
 * skip only texel/CLUT/TFX/color blending and masked colour stores.
 */
static inline int PspGsMirror_DepthOnly(uint64_t frame,uint64_t test,
                                       uint64_t zbuf) {
    return (uint32_t)(frame>>32)==UINT32_MAX &&
           (test & UINT64_C(1))==0u &&
           ((zbuf>>32)&UINT64_C(1))==0u;
}
/* A GS enabled Z test with function NEVER rejects every fragment
 * before any framebuffer or depth write. This rule holds regardless
 * of source alpha/AFAIL and whether a texture was sampled. */
static inline int PspGsMirror_ZNever(uint64_t test) {
    return ((test>>16)&UINT64_C(1))!=0u &&
           ((test>>17)&UINT64_C(3))==0u;
}
#endif
