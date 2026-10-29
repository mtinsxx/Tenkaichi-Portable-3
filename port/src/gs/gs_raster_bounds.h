#ifndef TP3_GS_RASTER_BOUNDS_H
#define TP3_GS_RASTER_BOUNDS_H
#include <stdint.h>

/* A PS2 GS SCISSOR register stores inclusive left/right/top/bottom
 * coordinates in four 11-bit fields. GS reference raster loops use
 * EXCLUSIVE upper bounds. Intersect BEFORE performing expensive shade(),
 * indexed texture/CLUT lookups, alpha/depth tests and VRAM writes.
 * Pixel positions inside the result are unchanged: interpolants still
 * use the original sprite/triangle vertices.
 *
 * This helper is allocation-free and works for all PSP CPU mirrors;
 * it does not remove geometry or change textures/PSM layout. */
static inline int GsRaster_ClipWindow(int *x0,int *x1,int *y0,int *y1,
                                     uint64_t sc,int surfaceW,int surfaceH) {
    int l=(int)(sc & UINT64_C(0x7ff));
    int r=(int)((sc >> 16) & UINT64_C(0x7ff)) + 1;
    int t=(int)((sc >> 32) & UINT64_C(0x7ff));
    int b=(int)((sc >> 48) & UINT64_C(0x7ff)) + 1;
    if(!x0||!x1||!y0||!y1 || surfaceW<=0||surfaceH<=0) return 0;
    if(r>surfaceW)r=surfaceW;
    if(b>surfaceH)b=surfaceH;
    if(l<0)l=0;
    if(t<0)t=0;
    if(*x0<l)*x0=l;
    if(*x1>r)*x1=r;
    if(*y0<t)*y0=t;
    if(*y1>b)*y1=b;
    return *x0<*x1 && *y0<*y1;
}
#endif
