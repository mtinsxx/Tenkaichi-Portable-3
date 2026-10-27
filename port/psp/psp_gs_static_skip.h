#ifndef TP3_PSP_GS_STATIC_SKIP_H
#define TP3_PSP_GS_STATIC_SKIP_H
/* PSP native VU1 strips have fixed GS PRIM/FRAME/ZBUF/TEX0/TEST for
   all their triangles. An unsupported FRAME/ZBUF or texture format is
   therefore fatal for the entire strip, not just its first triangle.
   Do NOT cache visibility or decoded-texture failures here: triangles
   of the same strip have different screen positions. */
enum PspGsStaticSkip {
    PSP_GS_STATIC_NONE,
    PSP_GS_STATIC_COLOR16,
    PSP_GS_STATIC_DEPTH_ALIAS,
    PSP_GS_STATIC_SPECIAL_TEXTURE
};
typedef struct PspGsStripSkip {
    unsigned kind;
    unsigned skipped;
} PspGsStripSkip;
static inline int PspGsStripSkip_MaySkip(unsigned kind) {
    return kind==PSP_GS_STATIC_COLOR16 ||
           kind==PSP_GS_STATIC_DEPTH_ALIAS ||
           kind==PSP_GS_STATIC_SPECIAL_TEXTURE;
}
static inline void PspGsStripSkip_Set(PspGsStripSkip *s,unsigned kind) {
    if(s && PspGsStripSkip_MaySkip(kind))s->kind=kind;
}
static inline int PspGsStripSkip_Next(PspGsStripSkip *s) {
    if(!s || !PspGsStripSkip_MaySkip(s->kind))return 0;
    ++s->skipped;
    return 1;
}
#endif
