#ifndef TP3_GS_MIRROR_EDGE_ROW_H
#define TP3_GS_MIRROR_EDGE_ROW_H
/* For edge(A,B,x,y): (Bx-Ax)*(y-Ay) - (By-Ay)*(x-Ax).
 * On PSP the float ops must retain their original association to keep
 * exact coverage decisions, winding, depth and colour interpolation.
 * Only the part independent of X is computed once per scanline.
 * NO incremental float additions: accumulated rounding changes the
 * pixel-coverage mask and can cause cracks in 3D arenas.
 *
 * The original reference function is still used on non-mirror paths.
 */
typedef struct GsMirrorEdgeRow {
    float base, slope, ax;
} GsMirrorEdgeRow;
static inline GsMirrorEdgeRow GsMirror_EdgeRow(float ax,float ay,
                                                float bx,float by,
                                                float y) {
    GsMirrorEdgeRow row;
    row.base=(bx-ax)*(y-ay);
    row.slope=by-ay;
    row.ax=ax;
    return row;
}
static inline float GsMirror_EdgeAt(const GsMirrorEdgeRow *row,float x) {
    return row->base-row->slope*(x-row->ax);
}
#endif
