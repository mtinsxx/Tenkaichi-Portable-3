/*
 * PSP VU1 program 4: conservative CPU clipper for stage triangles.
 * Uses original VU clip coordinates and the third clip->screen matrix.
 * Emulates the six-plane clipping and ST/color interpolation needed when
 * only one vertex of a strip falls outside the camera frustum.
 *
 * No clipping of an invalid or behind-eye W: caller uses original VU1.
 * This does not imply pixel-perfect PS2 perspective-correct rasterization;
 * PSP GU_TRANSFORM_2D remains affine, as in the legacy interpreter path.
 */
#ifndef BT3_PSP_STAGE4_CLIP_H
#define BT3_PSP_STAGE4_CLIP_H
#include "psp_stage4.h"

#define PSP_STAGE_CLIP_MAX 12

/* Conservative zero-pixel test for a whole VU1 program-4 triangle strip.
 * When every triangle is entirely beyond at least one homogeneous clip
 * plane, the reference PS2 clipper emits no visible triangles.
 * IMPORTANT: the PS2 VU1 CLIP instruction compares XYZ to +/-ABS(W),
 * not +/-signed W (gs_vu1.c case 0x1F). Negative-W geometry can still
 * pass CLIP and must not be discarded merely for W < 0.
 * The existing native validator still rejects W <= 0. We MUST NOT discard
 * a strip if any triangle may intersect the frustum, even when its corner
 * vertices are all outside different planes. No GS state is modified. */
static inline int PspStage4_EntireStripOutside(const float *vertices,
                                                uint32_t count,
                                                const float *clipMtx) {
    uint32_t i;
    unsigned older = 0, prev = 0;
    if (!vertices || !clipMtx || count < 3u || count > 1024u)
        return 0;
    for (i = 0; i < count; ++i) {
        const float *pos = vertices + (size_t)i * 12u;
        float clip[4];
        unsigned plane, mask = 0;
        float eps;
        for (plane = 0; plane < 4u; ++plane)
            if (!__builtin_isfinite(pos[plane])) return 0;
        PspStage4_Multiply(clip, clipMtx, pos);
        for (plane = 0; plane < 4u; ++plane)
            if (!__builtin_isfinite(clip[plane])) return 0;
        /* Keep a margin against 32-bit rounding and VU1 clip edge
           behavior; borderline geometry must use the existing fallback. */
        {
            float w = fabsf(clip[3]); /* exactly the VU1 CLIP boundary */
            eps = 0.0001f * (w + 1.0f);
            if (clip[0] + w < -eps) mask |= 1u << 0;
            if (w - clip[0] < -eps) mask |= 1u << 1;
            if (clip[1] + w < -eps) mask |= 1u << 2;
            if (w - clip[1] < -eps) mask |= 1u << 3;
            if (clip[2] + w < -eps) mask |= 1u << 4;
            if (w - clip[2] < -eps) mask |= 1u << 5;
        }
        if (i >= 2u && (mask & prev & older) == 0u)
            return 0; /* potentially visible; never drop it */
        older = prev;
        prev = mask;
    }
    return 1;
}

typedef struct PspStageClipVertex {
    float clip[4];
    float color[4];
    float st[2];
    unsigned mask;
} PspStageClipVertex;

/* Six half-space distances, inside iff >= 0. */
static inline float PspStage4_ClipDistance(const float c[4], unsigned plane) {
    switch (plane) {
    case 0: return c[0] + c[3]; /* -X */
    case 1: return c[3] - c[0]; /* +X */
    case 2: return c[1] + c[3]; /* -Y */
    case 3: return c[3] - c[1]; /* +Y */
    case 4: return c[2] + c[3]; /* -Z */
    default: return c[3] - c[2]; /* +Z */
    }
}

static inline int PspStage4_ClipInput(PspStageClipVertex *out,
                                      const float *pos, const float *color,
                                      const float *st, const float *clipMtx) {
    unsigned i;
    if (!out || !pos || !color || !st || !clipMtx) return 0;
    for (i = 0; i < 4; ++i)
        if (!__builtin_isfinite(pos[i]) || !__builtin_isfinite(color[i]))
            return 0;
    if (!__builtin_isfinite(st[0]) || !__builtin_isfinite(st[1])) return 0;
    PspStage4_Multiply(out->clip, clipMtx, pos);
    for (i = 0; i < 4; ++i)
        if (!__builtin_isfinite(out->clip[i])) return 0;
    /* Preserve fallback for behind-eye/crossing-W triangles, until the
       reference microprogram's reciprocal and near-plane are validated. */
    if (!(out->clip[3] > 0.0001f)) return 0;
    memcpy(out->color, color, 4 * sizeof(float));
    out->st[0] = st[0]; out->st[1] = st[1];
    out->mask = 0;
    for (i = 0; i < 6; ++i) {
        if (PspStage4_ClipDistance(out->clip, i) < 0.0f)
            out->mask |= 1u << i;
    }
    return 1;
}

static inline void PspStage4_ClipMix(PspStageClipVertex *out,
                                     const PspStageClipVertex *a,
                                     const PspStageClipVertex *b, float t) {
    unsigned i;
    for (i = 0; i < 4; ++i) {
        out->clip[i] = a->clip[i] + t * (b->clip[i] - a->clip[i]);
        out->color[i] = a->color[i] + t * (b->color[i] - a->color[i]);
    }
    for (i = 0; i < 2; ++i)
        out->st[i] = a->st[i] + t * (b->st[i] - a->st[i]);
    out->mask = 0;
}

/* Returns 0 for triangles wholly outside, 3..9 polygon vertices on
   success, or -1 for an invalid/oversized polygon (VU1 fallback).
   Caller MUST preflight any potentially failing clips before committing
   GS texture/PRIM state to the native path. */
static inline int PspStage4_ClipTriangle(PspStageClipVertex *out,
                                         const PspStageClipVertex in[3]) {
    PspStageClipVertex src[PSP_STAGE_CLIP_MAX], dst[PSP_STAGE_CLIP_MAX];
    unsigned i, plane, count = 3;
    if (in[0].mask & in[1].mask & in[2].mask) return 0;
    memcpy(src, in, 3 * sizeof(*src));
    for (plane = 0; plane < 6; ++plane) {
        unsigned outCount = 0;
        if (!count) break;
        for (i = 0; i < count; ++i) {
            const PspStageClipVertex *a = &src[(i + count - 1u) % count];
            const PspStageClipVertex *b = &src[i];
            float da = PspStage4_ClipDistance(a->clip, plane);
            float db = PspStage4_ClipDistance(b->clip, plane);
            int aIn = da >= 0.0f, bIn = db >= 0.0f;
            if (aIn != bIn) {
                float denom = da - db, t;
                if (denom == 0.0f) return -1;
                t = da / denom;
                if (!__builtin_isfinite(t) || t < 0.0f || t > 1.0f ||
                    outCount >= PSP_STAGE_CLIP_MAX) return -1;
                PspStage4_ClipMix(&dst[outCount++], a, b, t);
            }
            if (bIn) {
                if (outCount >= PSP_STAGE_CLIP_MAX) return -1;
                dst[outCount++] = *b;
            }
        }
        count = outCount;
        if (count) memcpy(src, dst, count * sizeof(*src));
    }
    if (count < 3) return 0;
    memcpy(out, src, count * sizeof(*out));
    return (int)count;
}

/* For clipped fan vertices, PS2 prog4 uses VU constant quadwords 8..11:
   screen = clip_to_screen * clip, then XYZ/ST divided by screen.w. */
static inline int PspStage4_ClipProject(PspStage4Vertex *out,
                                        const PspStageClipVertex *in,
                                        const float *clipToScreen,
                                        float ox, float oy) {
    float scr[4], inv, z;
    unsigned i;
    PspStage4_Multiply(scr, clipToScreen, in->clip);
    for (i = 0; i < 4; ++i)
        if (!__builtin_isfinite(scr[i])) return 0;
    if (!(scr[3] > 0.0001f)) return 0;
    inv = 1.0f / scr[3];
    z = scr[2] * inv;
    if (!__builtin_isfinite(z) || z < 0.0f || z > 16777215.0f) return 0;
    out->x = scr[0] * inv - ox;
    out->y = scr[1] * inv - oy;
    if (!__builtin_isfinite(out->x) || !__builtin_isfinite(out->y))
        return 0;
    out->z = (uint32_t)z;
    out->q = inv;
    out->s = in->st[0] * inv;
    out->t = in->st[1] * inv;
    for (i = 0; i < 4; ++i)
        if (!__builtin_isfinite(in->color[i])) return 0;
    out->r = PspStage4_Color(in->color[0]);
    out->g = PspStage4_Color(in->color[1]);
    out->b = PspStage4_Color(in->color[2]);
    out->a = PspStage4_Color(in->color[3]);
    return __builtin_isfinite(out->s) && __builtin_isfinite(out->t);
}
#endif
