#ifndef TP3_PSP_NATIVE_T8_INDEX_CACHE_H
#define TP3_PSP_NATIVE_T8_INDEX_CACHE_H
#include <stdint.h>
/* T8 indices and CLUT are distinct PSP resources. A palette update must
 * not force expensive PS2 PSMT8 source-address decoding if and ONLY if
 * all original GS source pages still have the same content hashes.
 * "indexHash" is derived from Gs_PageHash across the source pages
 * BEFORE the separate CLUT hash gets combined into the full texture
 * cache key. Both hashes are content-based, not frame counters. */
typedef struct PspNativeT8Source {
    uint32_t tbp,tbw,indexHash,width,height;
    unsigned valid;
} PspNativeT8Source;
static inline int PspNativeT8Source_Matches(
       const PspNativeT8Source *c, uint32_t tbp,uint32_t tbw,
       uint32_t indexHash,unsigned width,unsigned height) {
    return c && c->valid && c->tbp==tbp && c->tbw==tbw &&
           c->indexHash==indexHash && c->width==width &&
           c->height==height && width>0u && height>0u &&
           width<=512u && height<=512u;
}
static inline void PspNativeT8Source_Remember(PspNativeT8Source *c,
       uint32_t tbp,uint32_t tbw,uint32_t indexHash,
       unsigned width,unsigned height) {
    if(!c) return;
    c->tbp=tbp; c->tbw=tbw; c->indexHash=indexHash;
    c->width=width;c->height=height;c->valid=1u;
}
#endif
