#ifndef TP3_PSP_NATIVE_GU_PASSES_H
#define TP3_PSP_NATIVE_GU_PASSES_H
/* PSP GU native pass boundaries. These classify the triangles emitted
 * by the already native VU stage / fighter paths, plus legacy GS
 * sprites. Do NOT reorder draws: PS2 GS blends, FBMSK/DATE and
 * framebuffer-as-texture passes are order-dependent. A pending GE
 * triangle batch must be submitted before a new pass takes ownership. */
enum {
    PSP_GU_PASS_NONE=0,
    PSP_GU_PASS_STAGE=1,
    PSP_GU_PASS_FIGHTER=2,
    PSP_GU_PASS_SPRITE=3,
    PSP_GU_PASS_EFFECT=4
};
static inline int PspGuPass_MustFlush(unsigned previous,
                                     unsigned next,
                                     unsigned pendingVertices) {
    return pendingVertices!=0u && previous!=PSP_GU_PASS_NONE &&
           previous!=next && next!=PSP_GU_PASS_NONE;
}
static inline unsigned PspGuPass_Next(unsigned next) {
    return next<=PSP_GU_PASS_EFFECT?next:PSP_GU_PASS_NONE;
}
#endif
