#ifndef BT3_PSP_STAGE_CLIP_CACHE_H
#define BT3_PSP_STAGE_CLIP_CACHE_H
/* VU1 program399: cache projected clipped fans after all-or-nothing
   validation. On failure or bounded-cache overflow use interpreter. */
#include "psp_stage4_clip.h"
#define PSP_STAGE_CLIP_CACHE_TRIANGLES 1024u
#define PSP_STAGE_CLIP_CACHE_VERTICES 4096u
typedef struct PspStageClipCache {
    unsigned short begin[PSP_STAGE_CLIP_CACHE_TRIANGLES];
    unsigned char length[PSP_STAGE_CLIP_CACHE_TRIANGLES];
    unsigned used;
    PspStage4Vertex projected[PSP_STAGE_CLIP_CACHE_VERTICES];
} PspStageClipCache;
static inline void PspStageClipCache_Reset(PspStageClipCache *c) {
    c->used=0;
}
static inline int PspStageClipCache_Store(PspStageClipCache *c,
                                         unsigned tri,
                                         const PspStageClipVertex v[3],
                                         const float *matrix, float ox, float oy) {
    PspStageClipVertex poly[PSP_STAGE_CLIP_MAX];
    int n,k;
    if (!c || !v || !matrix || tri>=PSP_STAGE_CLIP_CACHE_TRIANGLES) return 0;
    n=PspStage4_ClipTriangle(poly,v);
    if (n<0 || c->used+(unsigned)n>PSP_STAGE_CLIP_CACHE_VERTICES) return 0;
    for(k=0;k<n;k++)
        if (!PspStage4_ClipProject(&c->projected[c->used+(unsigned)k],
                                   &poly[k],matrix,ox,oy)) return 0;
    c->begin[tri]=(unsigned short)c->used;
    c->length[tri]=(unsigned char)n;
    c->used+=(unsigned)n;
    return 1;
}
#endif
