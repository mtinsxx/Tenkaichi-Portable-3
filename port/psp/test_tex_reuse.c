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
    puts("PSP GS guarded texture reuse invalidation passed");
    return 0;
}
