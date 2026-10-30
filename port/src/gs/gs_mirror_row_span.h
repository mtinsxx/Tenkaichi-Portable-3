#ifndef TP3_GS_MIRROR_ROW_SPAN_H
#define TP3_GS_MIRROR_ROW_SPAN_H
#include <math.h>

/* Cheap conservative rejection of a full HORIZONTAL SPAN of pixels
 * before software GS mirror raster. Crucially the raster's original
 * y-major then x-major traversal is preserved (no self-feedback
 * ordering changes). Edge(b,c) and edge(c,a) are both affine in x;
 * if the two ENDPOINTS of either edge are strongly outside, every
 * pixel between them fails the original w0/w1 >= 0 test.
 * A generous signed EDGE margin ensures no border, near-zero-area,
 * or float-rounding false negatives. Unsafe large coordinates or
 * near-degenerate areas always take the unmodified pixel path. */
static inline float GsMirror_RowEdge(float ax,float ay,float bx,float by,
                                    float x,float y) {
    return (bx-ax)*(y-ay)-(by-ay)*(x-ax);
}
static inline int GsMirror_RowSpanOutside(float ax,float ay,float bx,float by,
                                         float cx,float cy,float area,
                                         int x0,int x1,int y) {
    const float margin=1.0f;
    float e0,e1;
    if(x0>x1 || !isfinite(area) || fabsf(area)<1.0f ||
       !isfinite(ax) || !isfinite(ay) ||
       !isfinite(bx) || !isfinite(by) ||
       !isfinite(cx) || !isfinite(cy) ||
       fabsf(ax)>2048.f || fabsf(ay)>2048.f ||
       fabsf(bx)>2048.f || fabsf(by)>2048.f ||
       fabsf(cx)>2048.f || fabsf(cy)>2048.f) return 0;
    e0=GsMirror_RowEdge(bx,by,cx,cy,(float)x0,(float)y);
    e1=GsMirror_RowEdge(bx,by,cx,cy,(float)x1,(float)y);
    if(area>0.f ? (e0< -margin && e1< -margin)
                : (e0> margin && e1> margin))return 1;
    e0=GsMirror_RowEdge(cx,cy,ax,ay,(float)x0,(float)y);
    e1=GsMirror_RowEdge(cx,cy,ax,ay,(float)x1,(float)y);
    if(area>0.f ? (e0< -margin && e1< -margin)
                : (e0> margin && e1> margin))return 1;
    return 0;
}
#endif
