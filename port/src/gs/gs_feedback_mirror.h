#ifndef TP3_GS_FEEDBACK_MIRROR_H
#define TP3_GS_FEEDBACK_MIRROR_H
#include <stdint.h>
/* PS2 GS VRAM stores 4 MiB as 16,384 physical 256-byte blocks.
 * PSMCT32 and PSMT8 use DIFFERENT virtual pixel layouts over those
 * same 4 MiB. Mirror *actual GS rasterizer writes* to those bytes;
 * never convert PSP RGB5551 back to RGBA32 (that loses low RGB and
 * all but one bit of framebuffer alpha).
 *
 * The mirror is demand-driven: the first observed cross-format TEX0
 * read enrolls its producer FBP, which is mirrored from subsequent
 * draws. It is a steady-state coherent bridge, not a retroactive
 * reconstruction of pixels drawn before the first observation.
 */
#define TP3_GS_VRAM_PAGES 512u
#define TP3_GS_VRAM_BLOCKS 16384u
static inline unsigned PspGsMirrorPageOfBlock(uint32_t block) {
    return (block & (TP3_GS_VRAM_BLOCKS-1u)) >> 5u;
}
static inline int PspGsMirrorShouldDraw(const uint8_t tracked[TP3_GS_VRAM_PAGES],
                                         unsigned fbp) {
    return fbp < TP3_GS_VRAM_PAGES && tracked[fbp]!=0u;
}
static inline int PspGsMirrorMarkPage(uint32_t stamps[TP3_GS_VRAM_PAGES],
                                      unsigned block, uint32_t drawStamp) {
    unsigned p=PspGsMirrorPageOfBlock(block);
    if(stamps[p]==drawStamp)return 0;
    stamps[p]=drawStamp;
    return 1;
}
#endif
