#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_fighter_pack_miss_cache.h"
int main(void) {
    PspFighterNegativeCache cache={0};
    unsigned i;
    for(i=1;i<=1024;i++) {
        uint32_t hash=0x40000000u+i;
        PspFighterNegative_Add(&cache,hash,i%40u+3u);
        assert(PspFighterNegative_Has(&cache,hash,i%40u+3u));
        assert(cache.length<=TP3_FIGHTER_NEGATIVE_CAP);
    }
    /* The original hard cap at 96 prevented any later lookup. This
       rotating cache discards only OLD negative results; a new
       fighter/skin can always be checked for an existing F3P1 pack. */
    assert(cache.length==TP3_FIGHTER_NEGATIVE_CAP);
    assert(!PspFighterNegative_Has(&cache,0x40000001u,4u));
    assert(PspFighterNegative_Has(&cache,0x40000400u,1024u%40u+3u));
    PspFighterNegative_Add(&cache,0x40000400u,1024u%40u+3u);
    assert(PspFighterNegative_Has(&cache,0x40000400u,1024u%40u+3u));
    puts("Original BT3 offline fighter submesh negative cache: 1024+ sources PASS");
    return 0;
}
