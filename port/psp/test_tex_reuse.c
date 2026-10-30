#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "psp_tex_reuse.h"
int main(void) {
    PspTexReuse hot[PSP_TEX_REUSE_SLOTS] = {0};
    const uint64_t t = UINT64_C(0xAABBCCDD00003012);
    const uint64_t a = UINT64_C(0x1234000000000080);
    unsigned s = Psp_TexReuseSlot(t, a, 256, 0);
    assert(s < PSP_TEX_REUSE_SLOTS);
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 256, 0, 4));
    Psp_TexReuseRemember(&hot[s], t, a, 7, 12, 256, 0, 3);
    assert(Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t+1, a, 7, 12, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a+1, 7, 12, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 8, 12, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 13, 256, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 128, 0, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 256, 1, 4));
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 256, 0, 3));
    /* Appending keeps all existing descriptor indices unchanged. */
    assert(Psp_TexReuseMatches(&hot[s], t, a, 7, 12, 256, 0, 5));
    /* Evictions move descriptors, so changing serial must invalidate. */
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 7, 13, 256, 0, 5));
    /* GS VRAM uploads must still invalidate through global epoch. */
    assert(!Psp_TexReuseMatches(&hot[s], t, a, 8, 12, 256, 0, 5));
    {
        unsigned i;
        for (i=0;i<512u;++i) {
            unsigned q=Psp_TexReuseSlot(t+i,a,256,0);
            assert(q<PSP_TEX_REUSE_SLOTS);
            Psp_TexReuseRemember(&hot[q],t+i,a,11,18,256,0,i%4u);
            assert(Psp_TexReuseMatches(&hot[q],t+i,a,11,18,256,0,4));
            assert(!Psp_TexReuseMatches(&hot[q],t+i,a,12,18,256,0,4));
        }
    }
    {
        uint32_t pages[512] = {0};
        PspTexReuse e = {0};
        unsigned i;
        for(i=0;i<512;++i) pages[i] = i * 3u + 1u;
        Psp_TexReuseRemember(&e,t,a,101,31,256,0,2);
        Psp_TexReuseRememberPages(&e,pages,510u,4u); /* crossing VRAM end */
        assert(Psp_TexReuseKeyStable(&e,t,a,31,256,0,3));
        assert(Psp_TexReusePagesStillMatch(&e,pages,510u,4u));
        assert(!Psp_TexReuseMatches(&e,t,a,102,31,256,0,3));
        /* Unrelated write must allow fast reuse despite global epoch. */
        pages[80]++;
        assert(Psp_TexReusePagesStillMatch(&e,pages,510u,4u));
        /* Any source-page write forbids it. */
        pages[511]++;
        assert(!Psp_TexReusePagesStillMatch(&e,pages,510u,4u));
        pages[511]--;
        pages[1]++;
        assert(!Psp_TexReusePagesStillMatch(&e,pages,510u,4u));
        pages[1]--;
        assert(!Psp_TexReusePagesStillMatch(&e,pages,510u,5u));
        assert(!Psp_TexReusePagesStillMatch(&e,pages,511u,4u));
        assert(!Psp_TexReusePagesStillMatch(&e,pages,510u,17u));
        Psp_TexReuseRemember(&e,t+1,a,102,31,256,0,3);
        assert(!Psp_TexReusePagesStillMatch(&e,pages,510u,4u));
        Psp_TexReuseRememberPages(&e,pages,10u,17u);
        assert(!Psp_TexReusePagesStillMatch(&e,pages,10u,17u));
        assert(!Psp_TexReuseKeyStable(&e,t,a,31,256,0,4));
        assert(!Psp_TexReuseKeyStable(&e,t+1,a,32,256,0,4));
    }
    {
        /* Alternating TEX0 values deliberately colliding in one set:
         * the old direct-map cache evicted the other one on EVERY draw.
         * Both are now retained with fully checked TEX0/TEXA/serial. */
        static PspTexReuse ways[PSP_TEX_REUSE_SLOTS];
        uint64_t a0=UINT64_C(0x120034);
        uint64_t b0=a0+1u;
        unsigned base=Psp_TexReuseSlot(a0,a,256,0);
        unsigned first,second,i;
        for(i=0;i<1000000u;i++,b0++)
            if(b0!=a0 && Psp_TexReuseSlot(b0,a,256,0)==base)break;
        assert(i<1000000u);
        assert((base&1u)==0u && base+1u<PSP_TEX_REUSE_SLOTS);
        first=Psp_TexReuseChooseWay(ways,base,a0,a,31,256,0,8,0);
        assert(first==base);
        Psp_TexReuseRemember(&ways[first],a0,a,41,31,256,0,2);
        second=Psp_TexReuseChooseWay(ways,base,b0,a,31,256,0,8,0);
        assert(second==base+1u);
        Psp_TexReuseRemember(&ways[second],b0,a,41,31,256,0,3);
        for(i=0;i<2000u;i++) {
            assert(Psp_TexReuseChooseWay(ways,base,a0,a,31,256,0,8,i&1u)==base);
            assert(Psp_TexReuseChooseWay(ways,base,b0,a,31,256,0,8,i&1u)==base+1u);
            assert(Psp_TexReuseMatches(&ways[base],a0,a,41,31,256,0,8));
            assert(Psp_TexReuseMatches(&ways[base+1u],b0,a,41,31,256,0,8));
        }
        /* Upload epoch mismatch cannot be silently accepted: the
         * matching descriptor is selected, but full epoch/page check
         * will reject a stale texture. */
        assert(!Psp_TexReuseMatches(&ways[base],a0,a,42,31,256,0,8));
        assert(Psp_TexReuseChooseWay(ways,base,a0,a,31,256,0,8,1)==base);
        assert(Psp_TexReuseChooseWay(ways,base,a0,a,32,256,0,8,1)==base);
        /* Serial mismatch invalidates an old descriptor before a new
         * cached texture can adopt that way. */
        assert(!Psp_TexReuseMatches(&ways[base],a0,a,41,32,256,0,8));
    }
    puts("PSP GS guarded texture reuse invalidation passed");
    return 0;
}
