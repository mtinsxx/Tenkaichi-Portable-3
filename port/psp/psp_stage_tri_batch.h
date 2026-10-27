#ifndef BT3_PSP_STAGE_TRI_BATCH_H
#define BT3_PSP_STAGE_TRI_BATCH_H
/* Same PSP 2D GS triangle packing used by GsGpu_Draw, but the expensive
   texture decode/key, render target, scissor, alpha and GPU state setup
   is performed only ONCE per VU1 program-4 strip. This header is
   included AFTER Vtx and GsVertex declarations. */
typedef struct PspStageTriScale {
    float sx, sy, lodU, lodV;
    int tw, th, fst, modulate, flat;
} PspStageTriScale;

static inline unsigned PspStageTriAlpha(unsigned a) {
    return a >= 0x80u ? 255u : a * 2u;
}
static inline PspStageTriScale PspStageTriMakeScale(
    uint64_t prim, uint64_t t0, int tw, int th) {
    PspStageTriScale c;
    uint32_t w = 1u << ((t0 >> 26) & 15u);
    uint32_t h = 1u << ((t0 >> 30) & 15u);
    if (w > 512u) w = 512u;
    if (h > 512u) h = 512u;
    c.sx = 480.f / 512.f; c.sy = 272.f / 448.f;
    c.tw = tw; c.th = th;
    c.lodU = tw ? (float)tw / (float)w : 1.f;
    c.lodV = th ? (float)th / (float)h : 1.f;
    c.fst = (int)((prim >> 8) & 1u);
    c.modulate = ((prim >> 4) & 1u) && ((t0 >> 35) & 3u) == 0u;
    c.flat = !(prim & (1u << 3));
    return c;
}
static inline void PspStageTriPack(Vtx dst[3], const GsVertex src[3],
                                    const PspStageTriScale *c) {
    unsigned i;
    for (i=0; i<3u; ++i) {
        const GsVertex *s = &src[i];
        const GsVertex *shade = c->flat ? &src[2] : s;
        float u, v;
        unsigned r=shade->r, g=shade->g, b=shade->b;
        if (c->fst) {
            u = (float)s->u / 16.f * c->lodU;
            v = (float)s->v / 16.f * c->lodV;
        } else {
            float q = s->q != 0.f ? s->q : 1.f;
            u = (s->s / q) * (float)(c->tw ? c->tw : 1);
            v = (s->t / q) * (float)(c->th ? c->th : 1);
        }
        if (c->modulate) {
            r = PspStageTriAlpha(r);
            g = PspStageTriAlpha(g);
            b = PspStageTriAlpha(b);
        }
        dst[i].u=u; dst[i].v=v;
        dst[i].c=r | (g << 8) | (b << 16) | (PspStageTriAlpha(shade->a) << 24);
        dst[i].x=s->x*c->sx; dst[i].y=s->y*c->sy;
        dst[i].z=(float)Psp_GsDepthZ24ToGuZ16(s->z);
    }
}
#endif
