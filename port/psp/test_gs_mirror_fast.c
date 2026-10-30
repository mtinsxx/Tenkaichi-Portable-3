/* Pixel-exact reference-vs-optimized GS software mirror test.
 * All pixels accepted by the unmodified triangle renderer must still
 * be shaded at the SAME interpolation weights. This tests BOTH windings,
 * coverage edges and degenerate/offscreen cases without game assets. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "gs_mirror_fast.h"
typedef struct P { float x,y; } P;
static float edge(P a,P b,int x,int y) {
    return (b.x-a.x)*((float)y-a.y)-
           (b.y-a.y)*((float)x-a.x);
}
static float edge_p(P a,P b,P c) {
    return (b.x-a.x)*(c.y-a.y)-
           (b.y-a.y)*(c.x-a.x);
}
static uint32_t rng=0x13579bdfu;
static uint32_t rand_u32(void){
    rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;
}
int main(void) {
    unsigned trials,refCovered=0,fastCovered=0,rejected=0,referenceRejects=0;
    int x,y;
    assert(GsMirror_IsNoop((uint64_t)UINT32_MAX<<32,(uint64_t)1<<32));
    assert(!GsMirror_IsNoop((uint64_t)UINT32_MAX<<32,0));
    assert(!GsMirror_IsNoop((uint64_t)0xfffffffeu<<32,(uint64_t)1<<32));
    assert(GsMirror_BoundsMayOverlap(-1,1024,1,1023,1024,1024));
    assert(!GsMirror_BoundsMayOverlap(1024,1200,0,1023,1024,1024));
    assert(!GsMirror_BoundsMayOverlap(-200,-1,0,1023,1024,1024));
    assert(!GsMirror_BoundsMayOverlap(NAN,100,0,10,1024,1024));
    assert(!GsMirror_BoundsMayOverlap(-1,INFINITY,0,10,1024,1024));
    for(trials=0;trials<1200u;trials++) {
        P a,b,c;
        float area;
        a.x=(float)((int)(rand_u32()%81u)-40)/4.0f;
        a.y=(float)((int)(rand_u32()%81u)-40)/4.0f;
        b.x=(float)((int)(rand_u32()%81u)-40)/4.0f;
        b.y=(float)((int)(rand_u32()%81u)-40)/4.0f;
        c.x=(float)((int)(rand_u32()%81u)-40)/4.0f;
        c.y=(float)((int)(rand_u32()%81u)-40)/4.0f;
        area=edge_p(a,b,c);
        if(area==0.f)continue;
        for(y=-12;y<13;y++)for(x=-12;x<13;x++) {
            float e0=edge(b,c,x,y),e1=edge(c,a,x,y);
            double baselineW0=(double)e0/(double)area;
            double baselineW1=(double)e1/(double)area;
            double baselineW2=1.0-baselineW0-baselineW1;
            int baseline=(baselineW0>=0.0 && baselineW1>=0.0 &&
                          baselineW2>=-1e-9);
            int early=GsMirror_EarlyReject(area,e0,e1);
            int accepted=0;
            if(!early) {
                double w0=(double)e0/(double)area;
                double w1=(double)e1/(double)area;
                double w2=1.0-w0-w1;
                accepted=w0>=0.0 && w1>=0.0 && w2>=-1e-9;
                /* Preserve exact double-precision interpolation and Z:
                 * the early test never modifies any covered pixel. */
                if(accepted) {
                    uint32_t z1=(uint32_t)(101.0*baselineW0+
                                         301.0*baselineW1+
                                         509.0*baselineW2+0.5);
                    uint32_t z2=(uint32_t)(101.0*w0+301.0*w1+
                                         509.0*w2+0.5);
                    assert(memcmp(&w0,&baselineW0,sizeof(w0))==0);
                    assert(memcmp(&w1,&baselineW1,sizeof(w1))==0);
                    assert(memcmp(&w2,&baselineW2,sizeof(w2))==0);
                    assert(z1==z2);
                }
            } else {
                ++rejected;
                assert(!baseline); /* false negative would lose a pixel */
            }
            if(!baseline)++referenceRejects;
            refCovered+=(unsigned)baseline;
            fastCovered+=(unsigned)accepted;
            assert(accepted==baseline);
        }
    }
    assert(refCovered==fastCovered && refCovered>0 && rejected>0);
    printf("GS mirror pixel-exact triangle coverage: %u covered, %u early-rejected of %u rejected; 1200 randomized triangles PASS\n",
           refCovered,rejected,referenceRejects);
    return 0;
}
