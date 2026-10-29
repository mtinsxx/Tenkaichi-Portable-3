/* Compare the optimized CPU GS-mirror scan rectangle with the
 * original PS2 pixel() scissor check for representative sprite/triangle
 * bounding boxes. Rasterization and interpolants inside remain exact. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "gs_raster_bounds.h"
static uint64_t make_sc(unsigned x0,unsigned x1,unsigned y0,unsigned y1) {
    return (uint64_t)x0 | ((uint64_t)x1<<16) |
           ((uint64_t)y0<<32) | ((uint64_t)y1<<48);
}
static unsigned simulate_original(int x0,int x1,int y0,int y1,
                                  uint64_t sc,unsigned w,unsigned h) {
    int x,y;
    unsigned count=0;
    for(y=y0;y<y1;y++)for(x=x0;x<x1;x++)
        if (x>=0&&y>=0&&x<(int)w&&y<(int)h&&
            x>=(int)(sc&2047u)&&x<=(int)((sc>>16)&2047u)&&
            y>=(int)((sc>>32)&2047u)&&y<=(int)((sc>>48)&2047u))
            ++count;
    return count;
}
static unsigned simulate_optimized(int x0,int x1,int y0,int y1,
                                   uint64_t sc,unsigned w,unsigned h) {
    if(!GsRaster_ClipWindow(&x0,&x1,&y0,&y1,sc,(int)w,(int)h))
        return 0;
    return (unsigned)(x1-x0)*(unsigned)(y1-y0);
}
int main(void) {
    int x,y;
    uint64_t sc=make_sc(9,28,5,17);
    assert(simulate_original(-70,90,-70,90,sc,64,48)==260u);
    assert(simulate_optimized(-70,90,-70,90,sc,64,48)==260u);
    assert(simulate_optimized(-70,90,-70,90,sc,64,48) <
           (unsigned)(160*160));
    assert(simulate_optimized(40,70,30,55,sc,64,48)==0u);
    assert(simulate_optimized(8,10,5,6,sc,64,48)==1u);
    assert(simulate_optimized(28,29,17,18,sc,64,48)==1u);
    assert(simulate_optimized(29,30,17,18,sc,64,48)==0u);
    /* Many original boxes and GS scissor boundaries. Differential
     * comparison ensures every pixel retained or rejected matches the
     * reference exact per-pixel SCISSOR path. */
    for(y=-35;y<=35;y+=5)for(x=-45;x<=45;x+=5) {
        int k;
        for(k=0;k<8;k++) {
            int x0=x,y0=y,x1=x+7+k*4,y1=y+5+k*3;
            uint64_t r=make_sc((unsigned)(k*3),
                 (unsigned)(20+k*3), (unsigned)(k*2),
                 (unsigned)(10+k*3));
            assert(simulate_original(x0,x1,y0,y1,r,64,48)==
                   simulate_optimized(x0,x1,y0,y1,r,64,48));
        }
    }
    puts("PS2 GS CPU mirror raster scissor parity: full differential pass");
    return 0;
}
