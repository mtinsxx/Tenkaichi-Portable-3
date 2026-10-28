#ifndef TP3_PSP_T8_PALETTE_REFRESH_H
#define TP3_PSP_T8_PALETTE_REFRESH_H
#include <stdint.h>
/*
 * PSP GU indexed-texture pixels and 256-entry CLUT are separate:
 * when a PS2 CLUT changes but source indices are identical, rewriting
 * ONLY the CLUT in a completed-frame texture avoids decoding and
 * allocating an entire duplicate T8 texture.
 *
 * The PSP GE may be reading both the texture and its CLUT from queued
 * draws. In-place updates are forbidden until FrameEnd's GE fence has
 * completed the frame in which this texture was last used.
 */
typedef struct PspT8PaletteIdentity {
    uint64_t tex0,texa;
    uint32_t sourceHash;
    unsigned width,height;
    unsigned lastFrame;
    int indexed,whiteSprite;
} PspT8PaletteIdentity;
static inline int PspT8Palette_CanRefresh(const PspT8PaletteIdentity *t,
                        uint64_t tex0,uint64_t texa,
                        uint32_t sourceHash,
                        unsigned width,unsigned height,
                        unsigned currentFrame) {
    return t && t->indexed && !t->whiteSprite &&
        t->lastFrame < currentFrame &&
        t->tex0==tex0 && t->texa==texa &&
        t->sourceHash==sourceHash &&
        t->width==width && t->height==height &&
        width>0u && height>0u && width<=512u && height<=512u;
}
#endif
