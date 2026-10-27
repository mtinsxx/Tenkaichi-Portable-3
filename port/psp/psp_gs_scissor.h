#ifndef TP3_PSP_GS_SCISSOR_H
#define TP3_PSP_GS_SCISSOR_H
#include <stdint.h>
#include <float.h>
/* PS2 GS SCISSOR coordinates are INCLUSIVE. An empty scissor rejects
 * all pixels. The PSP backend had accidentally DISABLED GU_SCISSOR_TEST
 * for an empty projected rect, allowing a discarded GS pass to paint the
 * whole scene. Preserve the source GS clip before any texture/depth work.
 * Bounding-box culling is conservative: if coordinates are not finite,
 * keep the primitive on the normal route. The tiny 1px tolerance avoids
 * throwing away triangles touching an edge through rasterization rules. */
static inline int PspGs_ScissorReject(const uint64_t sc,
                                      const float *xy, unsigned count) {
    unsigned i;
    float minx=FLT_MAX,miny=FLT_MAX,maxx=-FLT_MAX,maxy=-FLT_MAX;
    unsigned x0=(unsigned)(sc&0x7ffu),x1=(unsigned)((sc>>16)&0x7ffu);
    unsigned y0=(unsigned)((sc>>32)&0x7ffu),y1=(unsigned)((sc>>48)&0x7ffu);
    if(x1<x0 || y1<y0) return 1;
    if(!xy || count==0u) return 0;
    for(i=0;i<count;i++) {
        const float x=xy[i*2u],y=xy[i*2u+1u];
        if(!(x>=-FLT_MAX && x<=FLT_MAX &&
             y>=-FLT_MAX && y<=FLT_MAX)) return 0;
        if(x<minx)minx=x;
        if(x>maxx)maxx=x;
        if(y<miny)miny=y;
        if(y>maxy)maxy=y;
    }
    return maxx<(float)x0-1.f || minx>(float)x1+1.f ||
           maxy<(float)y0-1.f || miny>(float)y1+1.f;
}
static inline int PspGs_ScissorViewportEmpty(uint64_t sc) {
    const unsigned x0=(unsigned)(sc&0x7ffu);
    const unsigned x1=(unsigned)((sc>>16)&0x7ffu);
    const unsigned y0=(unsigned)((sc>>32)&0x7ffu);
    const unsigned y1=(unsigned)((sc>>48)&0x7ffu);
    return x1<x0 || y1<y0 || x0>=512u || y0>=448u;
}
#endif
