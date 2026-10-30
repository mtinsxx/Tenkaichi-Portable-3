#ifndef TP3_GS_MIRROR_FAST_H
#define TP3_GS_MIRROR_FAST_H
#include <stdint.h>
#include <math.h>

/* GS mirror optimization: calculations identical for every covered
 * pixel. A negative first or second barycentric weight ALWAYS fails
 * the original raster check (third weight is tested separately).
 * Do not infer winding or use approximate epsilon-based culling. */
static inline int GsMirror_EarlyReject(float area,float e0,float e1) {
    return area>0.0f ? (e0<0.0f || e1<0.0f)
                      : (e0>0.0f || e1>0.0f);
}

/* GS FRAME.FBMSK masks all 32 framebuffer bits, while ZBUF.ZMSK
 * suppresses the depth write. The draw cannot change any memory,
 * regardless of alpha test, blending, destination-alpha test or DATE. */
static inline int GsMirror_IsNoop(uint64_t frame,uint64_t zbuf) {
    return (uint32_t)(frame>>32)==UINT32_MAX &&
           ((zbuf>>32)&UINT64_C(1))!=0;
}

/* Never convert huge/non-finite raster bounds to int on Allegrex.
 * Offscreen primitives have empty GS scissor intersection; preserve
 * all potentially visible pixels by returning 1 in that case only. */
/* Exact PS2 GS ALPHA: ((A - B) * C >> 7) + D.
 * When A==B and D==SRC, the output is SRC independent of C;
 * when C is fixed zero and D==SRC, it is also SRC. The original
 * per-channel clamp cannot change any 8-bit source component.
 * Never replace blends that depend on destination or SRC alpha. */
static inline int GsMirror_BlendEqualsSource(uint64_t alpha) {
    unsigned a=(unsigned)(alpha&3u);
    unsigned b=(unsigned)((alpha>>2)&3u);
    unsigned c=(unsigned)((alpha>>4)&3u);
    unsigned d=(unsigned)((alpha>>6)&3u);
    return d==0u && (a==b || (c==2u && ((alpha>>32)&255u)==0u));
}

static inline int GsMirror_BoundsMayOverlap(float minx,float maxx,
                                           float miny,float maxy,
                                           int width,int height) {
    if(!isfinite(minx)||!isfinite(maxx)||
       !isfinite(miny)||!isfinite(maxy))return 0;
    if(width<=0||height<=0)return 0;
    return maxx>=0.0f && maxy>=0.0f &&
           minx<=(float)(width-1) && miny<=(float)(height-1);
}
#endif
