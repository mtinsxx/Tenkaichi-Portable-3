#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psp_fb_snapshot_pool.h"

/* PSP main RAM snapshots: 480x272 GE image, 512x512 padded 5551
 * storage; test allocation alignment, bounded capacity and isolation.
 * No proprietary game data needed. This DOES NOT validate exact
 * PS2 GS PSMCT32/PSMT8 feedback, which is kept in gs_core.c. */
int main(void) {
    unsigned slots,i,j;
    assert(PspGsFb_PoolBytes(3u)==3u*512u*512u*2u+63u);
    assert(PspGsFb_PoolSlot(NULL,0u,3u)==NULL);
    for(slots=1u;slots<=TP3_FB_POOL_SLOTS;slots++) {
        unsigned char *raw=(unsigned char *)malloc(PspGsFb_PoolBytes(slots));
        unsigned char *aligned;
        assert(raw);
        aligned=(unsigned char *)PspGsFb_PoolAligned(raw);
        assert(((uintptr_t)aligned&63u)==0u);
        assert(PspGsFb_PoolSlot(aligned,slots,slots)==NULL);
        assert(PspGsFb_PoolSlot(aligned,0u,0u)==NULL);
        assert(PspGsFb_PoolSlot(aligned,0u,4u)==NULL);
        for(i=0u;i<slots;i++) {
            unsigned char *ptr=(unsigned char *)PspGsFb_PoolSlot(
                                      aligned,i,slots);
            assert(ptr && ((uintptr_t)ptr&63u)==0u);
            assert(ptr>=raw &&
                (size_t)(ptr-raw)+PSP_GS_FB_SNAPSHOT_BYTES <=
                     PspGsFb_PoolBytes(slots));
            memset(ptr,(int)(i+1u),PSP_GS_FB_SNAPSHOT_BYTES);
        }
        for(i=0u;i<slots;i++) {
            unsigned char *ptr=(unsigned char *)PspGsFb_PoolSlot(
                                      aligned,i,slots);
            for(j=0;j<PSP_GS_FB_SNAPSHOT_BYTES;j+=4093u)
                assert(ptr[j]==(unsigned char)(i+1u));
            assert(ptr[PSP_GS_FB_SNAPSHOT_BYTES-1u]==(unsigned char)(i+1u));
        }
        free(raw);
    }
    puts("PSP GS framebuffer snapshot pool PASS: aligned, isolated 0.5/1/1.5 MiB main-RAM images");
    return 0;
}
