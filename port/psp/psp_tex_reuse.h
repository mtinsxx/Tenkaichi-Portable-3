#ifndef BT3_PSP_TEX_REUSE_H
#define BT3_PSP_TEX_REUSE_H
#include <stdint.h>
typedef struct PspTexReuse {
    uint64_t tex0, texa;
    uint32_t epoch, serial;
    unsigned lodCap, index;
    int whiteSprite, valid;
} PspTexReuse;
static inline unsigned Psp_TexReuseSlot(uint64_t tex0, uint64_t texa,
                                        unsigned lodCap, int whiteSprite) {
    uint32_t h = (uint32_t)tex0 ^ (uint32_t)(tex0 >> 32) ^
                 (uint32_t)texa ^ (uint32_t)(texa >> 32) ^
                 (lodCap * 0x9e3779b1u) ^ (unsigned)whiteSprite;
    h ^= h >> 16;
    return (unsigned)(h & 7u);
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
    e->valid = 1;
}
#endif
