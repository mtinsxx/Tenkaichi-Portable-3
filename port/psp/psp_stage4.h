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
