#ifndef TP3_PSP_FIGHTER_PACK_MISS_CACHE_H
#define TP3_PSP_FIGHTER_PACK_MISS_CACHE_H
#include <stdint.h>

/* Only cache NOT-FOUND or REJECTED source identities, never successful
 * loaded assets. Old code marked every lookup "seen", then permanently
 * stopped probing after 96 unique fighter strips; new characters and
 * costumes could never use offline packs. A circular 256-miss cache
 * keeps negative filesystem lookups bounded across arbitrary models.
 * Imported packets stay in the independent positive slot cache. */
enum { TP3_FIGHTER_NEGATIVE_CAP=256 };
typedef struct PspFighterNegativeCache {
    uint32_t hashes[TP3_FIGHTER_NEGATIVE_CAP];
    uint32_t counts[TP3_FIGHTER_NEGATIVE_CAP];
    unsigned length,next;
} PspFighterNegativeCache;
static inline int PspFighterNegative_Has(
       const PspFighterNegativeCache *cache,uint32_t hash,uint32_t count) {
    unsigned i;
    if(!cache) return 0;
    for(i=0;i<cache->length;i++)
        if(cache->hashes[i]==hash && cache->counts[i]==count)return 1;
    return 0;
}
static inline void PspFighterNegative_Add(
       PspFighterNegativeCache *cache,uint32_t hash,uint32_t count) {
    unsigned j;
    if(!cache || PspFighterNegative_Has(cache,hash,count))return;
    j=cache->next++%TP3_FIGHTER_NEGATIVE_CAP;
    cache->hashes[j]=hash;
    cache->counts[j]=count;
    if(cache->length<TP3_FIGHTER_NEGATIVE_CAP)cache->length++;
}
#endif
