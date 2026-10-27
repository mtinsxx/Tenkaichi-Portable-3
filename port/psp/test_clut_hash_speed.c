/* Exact original FNV-1a palette key vs blockwise 16xN GS reader.
 * A deliberately permuted physical layout simulates a swizzled palette:
 * row-major pixel access and blockwise extraction must hash identically.
 * Actual GS formats also receive periodic runtime cross-checks in psp_gu.c.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "psp_clut_hash.h"

static uint32_t scrambled[256], rect[256];
static uint32_t rndState=0x19872631u;
static uint32_t random32(void) {
    rndState ^= rndState<<13;
    rndState ^= rndState>>17;
    rndState ^= rndState<<5;
    return rndState;
}
static unsigned phys(unsigned x, unsigned y) {
    return (((y*16u+x)*73u)+19u)&255u;
}
static uint32_t slow(unsigned x,unsigned y) {
    return scrambled[phys(x,y)];
}
int main(void) {
    unsigned iteration, count, i, by, bx, y, x, comparisons=0;
    for(iteration=0;iteration<150u;iteration++) {
        for(i=0;i<256u;i++) scrambled[i]=random32();
        for(count=16u;count<=256u;count*=16u) {
            uint32_t expected=2166136261u;
            /* Original Gs_VramRead(cbp,1,cpsm,i&15,i>>4) order. */
            for(i=0;i<count;i++)
                expected=(expected^slow(i&15u,i>>4))*16777619u;
            /* Assemble rectangle by blocks, as Gs_VramReadRect does.
               Include a random arrangement of block iteration order. */
            memset(rect,0,sizeof(rect));
            for(by=0;by<count/16u;by+=4)
                for(bx=0;bx<16u;bx+=8)
                    for(y=by;y<by+4u&&y<count/16u;y++)
                        for(x=bx;x<bx+8u;x++)
                            rect[y*16u+x]=slow(x,y);
            assert(Psp_ClutHashPixels(rect,count)==expected);
            comparisons++;
        }
    }
    {
        uint32_t one[16]={0};
        uint32_t before=Psp_ClutHashPixels(one,16);
        one[12]=0xFF77BB22u;
        assert(Psp_ClutHashPixels(one,16)!=before);
    }
    printf("PSP GS CLUT block hash: %u scrambled palette comparisons passed\n",comparisons);
    return 0;
}
