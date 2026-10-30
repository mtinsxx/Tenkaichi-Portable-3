#ifndef TP3_GS_MIRROR_SCANLINE_H
#define TP3_GS_MIRROR_SCANLINE_H
#include <math.h>
/* Conservative scanline interval for the PSP's exact PS2 GS VRAM mirror.
 * Rejects off-triangle horizontal regions before barycentric divides,
 * swizzled framebuffer access, depth/alpha tests or CLUT sampling. The caller
 * still uses the original float edges and double interpolation for every
 * surviving pixel, in the original row-major ordering. No GS state is altered.
 * Return 0 for an empty row, 1 otherwise. Invalid/unsafe geometry takes
 * the full unmodified span (including nonfinite/near-degenerate triangles).
 */
static inline int GsMirror_ClipScanline(float ax,float ay,float bx,float by,
                                        float cx,float cy,float area,int y,
                                        int *left,int *right) {
    double e0,e1,e2,s0,s1,s2,lo,hi,guard,sgn;
    double vals[3], slopes[3];
    int i,originalLeft,originalRight;
    if(!left || !right)return 1;
    originalLeft=*left; originalRight=*right;
    if(originalLeft>originalRight)return 0;
    if(!isfinite(area) || fabsf(area)<1.f ||
       !isfinite(ax)||!isfinite(ay)||!isfinite(bx)||!isfinite(by)||
       !isfinite(cx)||!isfinite(cy)||
       fabsf(ax)>2048.f || fabsf(ay)>2048.f ||
       fabsf(bx)>2048.f || fabsf(by)>2048.f ||
       fabsf(cx)>2048.f || fabsf(cy)>2048.f ||
       originalLeft < -2048 || originalRight > 2048 || y < -2048 || y > 2048)
        return 1;
    sgn=area>0.f?1.0:-1.0;
    e0=sgn*((double)(cx-bx)*(y-by)-(double)(cy-by)*(originalLeft-bx));
    e1=sgn*((double)(ax-cx)*(y-cy)-(double)(ay-cy)*(originalLeft-cx));
    e2=sgn*(double)area-e0-e1;
    s0=sgn*(double)(by-cy);
    s1=sgn*(double)(cy-ay);
    s2=-s0-s1;
    vals[0]=e0; vals[1]=e1; vals[2]=e2;
    slopes[0]=s0; slopes[1]=s1; slopes[2]=s2;
    lo=(double)originalLeft;
    hi=(double)originalRight;
    /* Guard for float edge rounding, barycentric w2 tolerance and
     * integer endpoint snapping within the +/-2048 safe coordinate limit. */
    guard=64.0+fabs((double)area)*1e-8;
    for(i=0;i<3;i++){
        double v=vals[i]+guard;
        double slope=slopes[i];
        if(slope==0.0){if(v<0.0)return 0;continue;}
        /* v+slope*(x-originalLeft)>=0. */
        double root=(double)originalLeft-v/slope;
        if(slope>0.0){if(root>lo)lo=root;}
        else {if(root<hi)hi=root;}
        if(lo>hi+4.0)return 0;
    }
    if(lo>hi)return 0;
    /* Always retain extra boundary pixels for exact reference coverage. */
    if(lo>(double)originalLeft){
        int cut=(int)lo-4;
        if(cut>*left)*left=cut;
    }
    if(hi<(double)originalRight){
        int cut=(int)hi+4;
        if(cut<*right)*right=cut;
    }
    return *left<=*right;
}
#endif
