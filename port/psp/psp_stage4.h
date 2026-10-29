/* PSP native VU1 program 4: conservative, CPU-only stage vertex transform.
 * Inputs are the original PS2 VU memory layout, NOT Vita GLSL shaders.
 *
 * The PSP GE lacks programmable vertex processing. Translate the
 * screen/clip matrix multiplies to Allegrex-native C and feed the
 * existing PSP GS primitive/state renderer.
 *
 * Only accept triangles which need no VU clipper. PSP GU's current 2D
 * path already divides ST by Q before affine interpolation even on the
 * reference interpreter, so different valid vertex W values are allowed.
 * Unsafe / clipped triangle fans still fall back to the original VU1.
 */
#ifndef BT3_PSP_STAGE4_H
#define BT3_PSP_STAGE4_H
#include <stdint.h>
#include <string.h>
#include <math.h>

typedef struct PspStage4Vertex {
    float x, y, s, t, q;
    uint32_t z;
    uint8_t r, g, b, a;
} PspStage4Vertex;

static inline void PspStage4_Multiply(float out[4], const float *cols,
                                      const float *pos) {
    unsigned k, i;
    for (k = 0; k < 4; ++k) {
        float v = cols[k] * pos[0]; /* VU matrices: four column vectors */
        for (i = 1; i < 4; ++i)
            v += cols[i * 4u + k] * pos[i];
        out[k] = v;
    }
}

/* GS XYZ2 stores screen X/Y in unsigned 16-bit 12.4 fixed point.
 * The native PSP path uses floats, and would otherwise rasterize a huge
 * primitive instead of following the reference VU->GS wrapping rules.
 * Reject coordinates that cannot fit one GS 12.4 register (including
 * conservative negative-side overflow) and let VU1 interpret the strip. */
static inline int PspStage4_GsCoordRepresentable(float c) {
    return __builtin_isfinite(c) && c >= -4096.0f &&
           c <= 4095.9375f;
}

/* The VU1 program4 normal-strip and clipped-fan paths use DIFFERENT
 * matrices: screen*pos vs clip_to_screen*(clip*pos). Before accepting a
 * native clipped strip, require both to project the original vertex
 * within one GS screen pixel. Divergent camera matrices can otherwise
 * join unrelated triangle corners into full-screen polygons.
 * Comparing projected ratios, rather than raw W, allows equivalent
 * homogeneous matrices with a different overall scale. */
static inline int PspStage4_ProjectionCoherent(const float *pos,
                     const float *screenMtx, const float clip[4],
                     const float *clipToScreen) {
    float direct[4], clipped[4], dx,dy, cx,cy;
    unsigned i;
    if (!pos || !screenMtx || !clip || !clipToScreen) return 0;
    PspStage4_Multiply(direct,screenMtx,pos);
    PspStage4_Multiply(clipped,clipToScreen,clip);
    for(i=0;i<4u;i++)
        if(!__builtin_isfinite(direct[i]) ||
           !__builtin_isfinite(clipped[i])) return 0;
    if(!(direct[3]>0.0001f) || !(clipped[3]>0.0001f))
        return 0;
    dx=direct[0]/direct[3]; dy=direct[1]/direct[3];
    cx=clipped[0]/clipped[3]; cy=clipped[1]/clipped[3];
    if(!PspStage4_GsCoordRepresentable(dx) ||
       !PspStage4_GsCoordRepresentable(dy) ||
       !PspStage4_GsCoordRepresentable(cx) ||
       !PspStage4_GsCoordRepresentable(cy)) return 0;
    return fabsf(dx-cx)<=1.0f && fabsf(dy-cy)<=1.0f;
}

static inline uint8_t PspStage4_Color(float c) {
    if (!(c > 0.0f)) return 0;
    if (c >= 255.0f) return 255;
    return (uint8_t)c; /* PS2 ftoi/GLSL floor for nonnegative colour */
}

/* Returns 0 if a vertex needs the original PS2 clipper / has invalid
   coordinates; only then is it safe to hand the whole strip to the
   old VU1 interpreter (never drop it from the scene).
   clip/pos are float4 in PS2 VU program 4's native packet order. */
/* The renderer already computed clip[] for the six frustum planes.
 * Reusing it avoids the duplicate 4x4 clip matrix multiplication. */
static inline int PspStage4_TransformClipReady(PspStage4Vertex *out,
                                     const float *pos, const float *color,
                                     const float *st, const float *matrix,
                                     const float clip[4],
                                     float xyoffX, float xyoffY) {
    float screen[4], q, z;
    unsigned i;
    PspStage4_Multiply(screen, matrix, pos);
    for (i = 0; i < 4; ++i) {
        if (!__builtin_isfinite(screen[i]) || !__builtin_isfinite(clip[i]))
            return 0;
    }
    if (!(screen[3] > 0.0001f) || !(clip[3] > 0.0001f)) return 0;
    /* Margin avoids the clipped-fan and guard-volume VU microprogram.
       Affine UV conversion matches the existing PSP interpreter backend. */
    for (i = 0; i < 3; ++i)
        if (clip[i] < -clip[3] || clip[i] > clip[3]) return 0;
    q = 1.0f / screen[3];
    z = screen[2] * q;
    if (!__builtin_isfinite(z) || z < 0.0f || z > 16777215.0f)
        return 0;
    for (i = 0; i < 4; ++i)
        if (!__builtin_isfinite(color[i])) return 0;
    if (!__builtin_isfinite(st[0]) || !__builtin_isfinite(st[1]))
        return 0;
    /* Guard raw GS screen coordinates BEFORE XYOFFSET, not the PSP
       viewport: clipped/offscreen triangles must remain eligible for the
       reference interpreter rather than being silently discarded. */
    if (!PspStage4_GsCoordRepresentable(screen[0]*q) ||
        !PspStage4_GsCoordRepresentable(screen[1]*q)) return 0;
    out->x = screen[0] * q - xyoffX;
    out->y = screen[1] * q - xyoffY;
    out->z = (uint32_t)z;
    out->s = st[0] * q; /* PS2 VU output uses ST and Q divided by W */
    out->t = st[1] * q;
    out->q = q;
    out->r = PspStage4_Color(color[0]);
    out->g = PspStage4_Color(color[1]);
    out->b = PspStage4_Color(color[2]);
    out->a = PspStage4_Color(color[3]);
    return __builtin_isfinite(out->x) && __builtin_isfinite(out->y);
}
/* Preserve the original entry point for independent users and validation. */
static inline int PspStage4_Transform(PspStage4Vertex *out, const float *pos,
                                     const float *color, const float *st,
                                     const float *matrix,
                                     float xyoffX, float xyoffY) {
    float clip[4];
    PspStage4_Multiply(clip, matrix + 16, pos);
    return PspStage4_TransformClipReady(out, pos, color, st, matrix,
                                        clip, xyoffX, xyoffY);
}
#endif
