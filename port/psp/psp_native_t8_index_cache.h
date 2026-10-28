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
/* Four fixed 64 KiB slots use the SAME total 256 KiB as the former
 * single 512x512 source cache. Small stage images may coexist, while
 * bigger images use the existing safe decoder. PSP GE is never given
 * a pointer to this CPU-side scratch cache. */
#define PSP_NATIVE_T8_CACHE_SLOTS 4u
#define PSP_NATIVE_T8_SLOT_BYTES (64u * 1024u)
typedef struct PspNativeT8Slot {
    PspNativeT8Source source;
    uint32_t usedTick;
} PspNativeT8Slot;
static inline int PspNativeT8Cache_Fits(unsigned width,unsigned height) {
    return width>0u && height>0u &&
        width<=512u && height<=512u &&
        (uint32_t)width * height <= PSP_NATIVE_T8_SLOT_BYTES;
}
static inline int PspNativeT8Cache_Lookup(PspNativeT8Slot *slots,
                    unsigned count,uint32_t tbp,uint32_t tbw,
                    uint32_t indexHash,unsigned width,unsigned height,
                    uint32_t tick) {
    unsigned i;
    if(!slots || !PspNativeT8Cache_Fits(width,height)) return -1;
    for(i=0;i<count;i++) {
        if(PspNativeT8Source_Matches(&slots[i].source,tbp,tbw,
                                    indexHash,width,height)) {
            slots[i].usedTick=tick;
            return (int)i;
        }
    }
    return -1;
}
/* Select an invalid slot first, otherwise least recently used.
 * Caller only commits after verified decoding: selection alone never
 * invalidates a live source image. Wrap-safe ages work for lifetimes
 * shorter than half a 32-bit timestamp cycle. */
static inline int PspNativeT8Cache_Choose(const PspNativeT8Slot *slots,
                               unsigned count,unsigned width,
                               unsigned height,uint32_t now) {
    unsigned i,oldest=0;
    uint32_t age=0;
    if(!slots || !count || !PspNativeT8Cache_Fits(width,height)) return -1;
    for(i=0;i<count;i++) {
        uint32_t candidate;
        if(!slots[i].source.valid) return (int)i;
        candidate=now-slots[i].usedTick;
        if(i==0u || candidate>age) {
            age=candidate;
            oldest=i;
        }
    }
    return (int)oldest;
}
static inline void PspNativeT8Cache_Commit(PspNativeT8Slot *slot,
                uint32_t tbp,uint32_t tbw,uint32_t indexHash,
                unsigned width,unsigned height,uint32_t tick) {
    if(!slot || !PspNativeT8Cache_Fits(width,height)) return;
    PspNativeT8Source_Remember(&slot->source,tbp,tbw,indexHash,width,height);
    slot->usedTick=tick;
}

#endif
