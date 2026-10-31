#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gs_mirror_clut_cache.h"

static uint32_t rng=0x4a62ff91u;
static uint32_t next(void) {
    rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;
}
int main(void) {
    static GsMirrorClutCache cache;
    unsigned i,bits,iter;
    uint32_t val;
    assert(!GsMirrorClut_Lookup(&cache,0u,&val));
    for(bits=4u;bits<=8u;bits+=4u){
        unsigned entries=bits==8u?256u:16u;
        for(i=0;i<entries;i++) {
            unsigned e=GsMirrorClut_Csm1(i,bits);
            unsigned ref=bits==8u
                  ? ((i&0xe7u)|((i&8u)<<1)|((i&16u)>>1))
                  : i;
            assert(e==ref);
            if(bits==8u) assert(GsMirrorClut_Csm1(e,bits)==i);
        }
    }
    for(iter=0;iter<4096u;iter++) {
        unsigned clutPages[4],n=1u+(next()%4u);
        GsMirrorClut_Begin(&cache);
        for(i=0;i<n;i++) {
            clutPages[i]=next()%TP3_GS_MIRROR_CLUT_PAGES;
            GsMirrorClut_AddPage(&cache,clutPages[i]);
        }
        cache.entries=256u;
        for(i=0;i<256u;i++)cache.colors[i]=next();
        cache.ready=1u;
        for(i=0;i<256u;i++){
            assert(GsMirrorClut_Lookup(&cache,i,&val));
            assert(val==cache.colors[i]);
        }
        assert(!GsMirrorClut_Lookup(&cache,256u,&val));
        assert(!GsMirrorClut_Lookup(&cache,0u,NULL));
        /* A changed GS word outside the CLUT source MUST NOT
           invalidate any cached colour. A page under the CLUT must
           invalidate before the next indexed read. */
        {
            unsigned unrelated=(clutPages[0]+1u)&511u;
            unsigned tries=0u;
            for(;;) {
                int collision=0;
                for(i=0;i<n;i++)
                    if(clutPages[i]==unrelated)collision=1;
                if(!collision)break;
                unrelated=(unrelated+1u)&511u;
                assert(++tries<512u);
            }
            assert(!GsMirrorClut_ChangedWord(&cache,unrelated));
            assert(GsMirrorClut_Lookup(&cache,17u,&val));
        }
        assert(GsMirrorClut_ChangedWord(&cache,clutPages[0]));
        assert(!GsMirrorClut_Lookup(&cache,17u,&val));
        assert(!GsMirrorClut_ChangedWord(&cache,clutPages[0]));
        assert(!GsMirrorClut_ChangedWord(&cache,clutPages[1u%n]));
        /* Next draw: old page stamps do not inherit validity. */
        GsMirrorClut_Begin(&cache);
        cache.entries=16u;
        cache.ready=1u;
        assert(!GsMirrorClut_ChangedWord(&cache,clutPages[0]));
        assert(GsMirrorClut_Lookup(&cache,15u,&val));
        assert(!GsMirrorClut_Lookup(&cache,16u,&val));
    }
    cache.stamp=UINT32_MAX;
    memset(cache.pageStamp,0xff,sizeof(cache.pageStamp));
    GsMirrorClut_Begin(&cache);
    assert(cache.stamp==1u);
    assert(!GsMirrorClut_ChangedWord(&cache,0u));
    puts("PSP GS indexed CLUT mirror: CSM1 4/8-bit index, exact per-draw page invalidation, 4096 randomized cases and stamp wrap PASSED");
    return 0;
}
