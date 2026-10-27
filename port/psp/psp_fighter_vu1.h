/* Native MIPS Allegrex equivalent of the ordinary VU1 fighter skinning
   and cel/fade lighting math for programs 0 (lit) and 1 (fade).
   The original interpreter remains authoritative for rejected batches.
   A batch must be validated in full before GS TEX0/PRIM are written.
 */
#ifndef BT3_PSP_NATIVE_FIGHTER_H
#define BT3_PSP_NATIVE_FIGHTER_H
#include <stdint.h>
#include <math.h>
#include "psp_stage4.h"

typedef struct PspFighterVertex {
    float x, y, s0, t0, s1, t1, q;
    uint32_t z;
} PspFighterVertex;

/* VU matrices are column-major, as in the PS2 prog0/prog1 microcode.
   layer1 shader values are normalized 0..1 and use the original model
   light vectors; only screen/pixel output is converted to PSP coordinates. */
static inline int PspFighter_Transform(PspFighterVertex *out,
                                      const float *pos,
                                      const float *normal,
                                      const float *st,
                                      const float *k,
                                      int fade) {
    float a[4], b[4], point[4], screen[4], clip[4];
    float boneInput[4], lightA, lightB, light, w, q;
    float norm4[4];
    unsigned i;

    if (!out || !pos || !normal || !st || !k) return 0;
    w = pos[3];
    if (!__builtin_isfinite(w) || w < 0.0f || w > 1.0f) return 0;
    for (i = 0; i < 3; ++i)
        if (!__builtin_isfinite(pos[i]) || !__builtin_isfinite(normal[i]))
            return 0;
    if (!__builtin_isfinite(st[0]) || !__builtin_isfinite(st[1]))
        return 0;

    for (i = 0; i < 3; ++i) boneInput[i] = pos[i] - k[8*4u+i];
    boneInput[3] = 1.0f;
    PspStage4_Multiply(a, k, boneInput);
    for (i = 0; i < 3; ++i) boneInput[i] = pos[i] - k[9*4u+i];
    PspStage4_Multiply(b, k + 16, boneInput);
    for (i = 0; i < 3; ++i) point[i] = b[i] + (a[i] - b[i]) * w;
    point[3] = 1.0f;
    PspStage4_Multiply(screen, k + 14*4u, point);
    PspStage4_Multiply(clip, k + 18*4u, point);
    for (i = 0; i < 4; ++i)
        if (!__builtin_isfinite(screen[i]) || !__builtin_isfinite(clip[i]))
            return 0;
    if (!(screen[3] > 0.0001f) || !(clip[3] > 0.0001f)) return 0;
    /* The PS2 microprogram discards any triangle with an outside
       vertex. Accept only wholly-inside strips, thus no clipping.
       Guard-volume and all other draws use the interpreter. */
    for (i = 0; i < 3; ++i)
        if (clip[i] < -clip[3] || clip[i] > clip[3]) return 0;

    q = 1.0f / screen[3];
    if (!__builtin_isfinite(q)) return 0;
    {
        float depth = screen[2] * q;
        if (!__builtin_isfinite(depth) || depth < 0.0f ||
            depth > 16777215.0f) return 0;
        out->z = (uint32_t)depth;
    }
    out->x = screen[0] * q; /* subtract per-layer GS XYOFFSET in draw */
    out->y = screen[1] * q;
    out->q = q;
    out->s0 = st[0]; /* use FST=0, set GS s/q in the drawing adapter */
    out->t0 = st[1];

    norm4[0] = normal[0];
    norm4[1] = normal[1];
    norm4[2] = normal[2];
    norm4[3] = 1.0f;
    if (fade) {
        float lightVec[4];
        PspStage4_Multiply(lightVec, k + 10*4u, norm4);
        out->s1 = (lightVec[0] + 1.0f) * 0.5f;
        out->t1 = (lightVec[1] + 1.0f) * 0.5f;
    } else {
        /* Light direction is the x component of VU 10..12 and
           the intensity offset is x of 13. Translate the normal by
           the 3x3 parts of the two bone matrices (no translation). */
        lightA = 0.0f; lightB = 0.0f;
        for (i = 0; i < 3; ++i) {
            float na = k[i] * normal[0] +
                       k[4+i] * normal[1] +
                       k[8+i] * normal[2];
            float nb = k[16+i] * normal[0] +
                       k[20+i] * normal[1] +
                       k[24+i] * normal[2];
            lightA += k[10*4u+i*4u] * na;
            lightB += k[10*4u+i*4u] * nb;
        }
        light = lightB + (lightA - lightB) * w + k[13*4u];
        out->s1 = (light + 1.0f) * 0.5f;
        out->t1 = 0.0f;
    }
    return __builtin_isfinite(out->x) && __builtin_isfinite(out->y) &&
           __builtin_isfinite(out->s1) && __builtin_isfinite(out->t1);
}
#endif
