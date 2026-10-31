#ifndef TP3_GS_MIRROR_CLUT_CACHE_H
#define TP3_GS_MIRROR_CLUT_CACHE_H
#include <stdint.h>
#include <string.h>

/* The PSP software GS mirror must preserve the exact four-megabyte
 * byte-addressable PS2 framebuffer, including PSMCT32 -> PSMT8 reads.
 * Palette lookup can nonetheless be cached for ONE draw.  Each page
 * containing any CLUT entry is tagged by the current draw generation.
 * A changed GS word in such a page invalidates the cache immediately,
 * so self-modifying framebuffer/CLUT aliases still read live GS VRAM.
 * No 5551 approximation or extra texture allocation is involved. */
#define TP3_GS_MIRROR_CLUT_PAGES 512u
typedef struct GsMirrorClutCache {
    uint32_t pageStamp[TP3_GS_MIRROR_CLUT_PAGES];
    uint32_t colors[256];
    uint32_t stamp;
    unsigned ready, entries;
} GsMirrorClutCache;
static inline void GsMirrorClut_Begin(GsMirrorClutCache *c) {
    if (!c) return;
    if (++c->stamp == 0u) {
        memset(c->pageStamp,0,sizeof(c->pageStamp));
        c->stamp=1u;
    }
    c->ready=0u;
    c->entries=0u;
}
static inline unsigned GsMirrorClut_Csm1(unsigned index,unsigned bits) {
    if (bits == 8u)
        return (index & 0xE7u) |
               ((index & 0x08u) << 1) |
               ((index & 0x10u) >> 1);
    return index;
}
static inline void GsMirrorClut_AddPage(GsMirrorClutCache *c,
                                        unsigned page) {
    if(c) c->pageStamp[page & 511u]=c->stamp;
}
static inline int GsMirrorClut_ChangedWord(GsMirrorClutCache *c,
                                           unsigned page) {
    if(!c || !c->ready || c->pageStamp[page & 511u]!=c->stamp)
        return 0;
    c->ready=0u;
    return 1;
}
static inline int GsMirrorClut_Lookup(const GsMirrorClutCache *c,
                                      unsigned index, uint32_t *color) {
    if(!c || !c->ready || index>=c->entries || !color) return 0;
    *color=c->colors[index];
    return 1;
}
#endif
