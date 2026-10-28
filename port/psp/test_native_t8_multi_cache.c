#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_native_t8_index_cache.h"
int main(void) {
    PspNativeT8Slot c[PSP_NATIVE_T8_CACHE_SLOTS];
    uint8_t img[PSP_NATIVE_T8_CACHE_SLOTS][PSP_NATIVE_T8_SLOT_BYTES]={{0}};
    unsigned i;
    const uint32_t p[4]={336u,368u,112u,0u};
    uint32_t now=0;
    memset(c,0,sizeof(c));
    assert(PSP_NATIVE_T8_CACHE_SLOTS*PSP_NATIVE_T8_SLOT_BYTES==256u*1024u);
    assert(PspNativeT8Cache_Fits(256,256));
    assert(PspNativeT8Cache_Fits(128,128));
    assert(!PspNativeT8Cache_Fits(512,512));
    assert(!PspNativeT8Cache_Fits(0,128));
    assert(!PspNativeT8Cache_Fits(513,128));
    for(i=0;i<4;i++) {
        int slot=PspNativeT8Cache_Choose(c,4,128,128,++now);
        assert(slot==(int)i);
        memset(img[slot],(int)(i+42),128*128);
        PspNativeT8Cache_Commit(&c[slot],p[i],8,1000+i,128,128,now);
    }
    /* Four independent native stage sources coexist without evicting
     * one another or exhausting the fixed static allocation. */
    for(i=0;i<4;i++) {
        int slot=PspNativeT8Cache_Lookup(c,4,p[i],8,1000+i,128,128,++now);
        assert(slot==(int)i);
        assert(img[slot][0]==i+42);
        assert(img[slot][128*128-1]==i+42);
    }
    /* Reusing 336 advances its LRU age. 368 is the next victim. */
    assert(PspNativeT8Cache_Lookup(c,4,336,8,1000,128,128,++now)==0);
    {
        int victim=PspNativeT8Cache_Choose(c,4,128,128,++now);
        assert(victim==1);
        /* Reservation does not discard the still-valid old data. */
        assert(PspNativeT8Source_Matches(&c[victim].source,368,8,1001,128,128));
        PspNativeT8Cache_Commit(&c[victim],224,8,1983,128,128,now);
        assert(PspNativeT8Cache_Lookup(c,4,368,8,1001,128,128,++now)==-1);
        assert(PspNativeT8Cache_Lookup(c,4,224,8,1983,128,128,++now)==1);
    }
    /* A mutation of the original GS source page must force decode. */
    assert(PspNativeT8Cache_Lookup(c,4,336,8,1009,128,128,++now)==-1);
    assert(PspNativeT8Cache_Lookup(c,4,336,9,1000,128,128,++now)==-1);
    assert(PspNativeT8Cache_Lookup(c,4,336,8,1000,256,128,++now)==-1);
    assert(PspNativeT8Cache_Choose(c,4,512,512,++now)==-1);
    assert(PspNativeT8Cache_Lookup(c,4,336,8,1000,512,512,++now)==-1);
    assert(PspNativeT8Cache_Lookup(NULL,4,336,8,1000,128,128,++now)==-1);
    assert(PspNativeT8Cache_Choose(NULL,4,128,128,++now)==-1);
    assert(PspNativeT8Cache_Choose(c,0,128,128,++now)==-1);
    puts("PSP native T8 four-way source cache: independent images, LRU, hashes, boundaries PASS");
    return 0;
}
