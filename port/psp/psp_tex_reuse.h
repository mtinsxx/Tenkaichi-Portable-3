#ifndef BT3_PSP_TEX_REUSE_H
#define BT3_PSP_TEX_REUSE_H
#include <stdint.h>
/* More direct-mapped hot keys; full-key+epoch+serial guards are unchanged. */
/* 128 two-way sets, 256 descriptors. Each slot is ~96 B in main RAM:
 * insignificant beside texture allocations but prevents alternating TEX0
 * keys mapping to the same single hot-cache descriptor in menus. */
#define PSP_TEX_REUSE_SLOTS 256u
#define PSP_TEX_REUSE_PAGE_LIMIT 16u
typedef struct PspTexReuse {
    uint64_t tex0, texa;
    uint32_t epoch, serial;
    unsigned lodCap, index;
    int whiteSprite, valid;
    unsigned pageBase, pageCount;
    uint32_t pageGeneration[PSP_TEX_REUSE_PAGE_LIMIT];
} PspTexReuse;
static inline unsigned Psp_TexReuseSlot(uint64_t tex0, uint64_t texa,
                                        unsigned lodCap, int whiteSprite) {
    uint32_t h = (uint32_t)tex0 ^ (uint32_t)(tex0 >> 32) ^
                 (uint32_t)texa ^ (uint32_t)(texa >> 32) ^
                 (lodCap * 0x9e3779b1u) ^ (unsigned)whiteSprite;
    h ^= h >> 16;
    return (unsigned)((h & ((PSP_TEX_REUSE_SLOTS / 2u) - 1u)) * 2u);
}
static inline int Psp_TexReuseMatches(const PspTexReuse *e,
                                      uint64_t tex0, uint64_t texa,
                                      uint32_t epoch, uint32_t serial,
                                      unsigned lodCap, int whiteSprite,
                                      unsigned count) {
    return e->valid && e->tex0 == tex0 && e->texa == texa &&
           e->epoch == epoch && e->serial == serial &&
           e->lodCap == lodCap && e->whiteSprite == whiteSprite &&
           e->index < count;
}
/* Like the guarded hot-key lookup, but allows a global upload epoch
   change only when all source VRAM pages can be proven unchanged.
   Palette-indexed textures keep the strict epoch validation because
   their palette may reference other/aliased page layouts. */
static inline int Psp_TexReuseKeyStable(const PspTexReuse *e,
                                        uint64_t tex0, uint64_t texa,
                                        uint32_t serial, unsigned lodCap,
                                        int whiteSprite, unsigned count) {
    return e->valid && e->tex0 == tex0 && e->texa == texa &&
           e->serial == serial && e->lodCap == lodCap &&
           e->whiteSprite == whiteSprite && e->index < count;
}
/* Both ways have the SAME hash set; never probe another set.
 * Prefer a matching full key even when epoch changed: the full-key
 * generation path still validates the source pages/CLUT. On a miss,
 * reuse an invalid/stale descriptor before evicting either valid way.
 * LRU eviction/compaction repairs all descriptor indices separately. */
static inline unsigned Psp_TexReuseChooseWay(const PspTexReuse *entries,
                                      unsigned base,
                                      uint64_t tex0,uint64_t texa,
                                      uint32_t serial,
                                      unsigned lodCap,int whiteSprite,
                                      unsigned count,unsigned replacement) {
    unsigned other=base+1u;
    if(Psp_TexReuseKeyStable(&entries[base],tex0,texa,serial,
                             lodCap,whiteSprite,count)) return base;
    if(Psp_TexReuseKeyStable(&entries[other],tex0,texa,serial,
                             lodCap,whiteSprite,count)) return other;
    if(!entries[base].valid || entries[base].serial!=serial ||
       entries[base].index>=count) return base;
    if(!entries[other].valid || entries[other].serial!=serial ||
       entries[other].index>=count) return other;
    return base+(replacement&1u);
}
static inline void Psp_TexReuseRememberPages(PspTexReuse *e,
                                               const uint32_t *generations,
                                               unsigned pageBase,
                                               unsigned pageCount) {
    unsigned i;
    e->pageCount = 0;
    if (!generations || pageCount == 0 ||
        pageCount > PSP_TEX_REUSE_PAGE_LIMIT) return;
    e->pageBase = pageBase & 511u;
    for (i=0;i<pageCount;++i)
        e->pageGeneration[i] = generations[(pageBase + i) & 511u];
    e->pageCount = pageCount;
}
static inline int Psp_TexReusePagesStillMatch(const PspTexReuse *e,
                                               const uint32_t *generations,
                                               unsigned pageBase,
                                               unsigned pageCount) {
    unsigned i;
    if (!generations || !pageCount || e->pageCount != pageCount ||
        e->pageBase != (pageBase & 511u) ||
        pageCount > PSP_TEX_REUSE_PAGE_LIMIT) return 0;
    for (i=0;i<pageCount;++i)
        if (e->pageGeneration[i] != generations[(pageBase+i)&511u])
            return 0;
    return 1;
}

static inline void Psp_TexReuseRemember(PspTexReuse *e,
                                         uint64_t tex0, uint64_t texa,
                                         uint32_t epoch, uint32_t serial,
                                         unsigned lodCap, int whiteSprite,
                                         unsigned index) {
    e->tex0 = tex0;
    e->texa = texa;
    e->epoch = epoch;
    e->serial = serial;
    e->lodCap = lodCap;
    e->whiteSprite = whiteSprite;
    e->index = index;
    e->pageCount = 0; /* never inherit another texture's snapshot */
    e->valid = 1;
}
/* Remove exactly one descriptor without invalidating unrelated hot keys.
 * Every cached index greater than the removed slot shifts down by one.
 * The old global serial++ on each LRU eviction made *all* hot keys cold
 * (and multiplied TEX0/CLUT lookup costs in battle). The epoch/page-
 * generation checks still guard changes to GS texture contents. */
static inline unsigned Psp_TexReuseOnErase(PspTexReuse *entries,
                                           unsigned slots,
                                           unsigned removed,
                                           uint32_t currentSerial) {
    unsigned i, repaired=0u;
    if (!entries) return 0;
    for(i=0u;i<slots;i++) {
        PspTexReuse *e=&entries[i];
        if (!e->valid || e->serial != currentSerial) continue;
        if (e->index==removed) {
            e->valid=0; /* descriptor was evicted */
            ++repaired;
        } else if(e->index>removed) {
            --e->index; /* memmove() shifts existing descriptors */
            ++repaired;
        }
    }
    return repaired;
}

#endif
