#ifndef BT3_PSP_LOG_ALL_H
#define BT3_PSP_LOG_ALL_H
/* Opt-in, bounded diagnostic incidents (NO per-primitive file writes).
 * First sample for each category + total number since last ~2-second
 * report. Does not alter texture/GS state, pixels or game logic. */
#include <stdint.h>
#include <string.h>
enum {
    PSP_DIAG_STAGE_BAD_ARGS,
    PSP_DIAG_STAGE_INVALID_CLIP_INPUT,
    PSP_DIAG_STAGE_CLIP_DISABLED,
    PSP_DIAG_STAGE_INVALID_PROJECTION,
    PSP_DIAG_STAGE_POLY_FAILURE,
    PSP_DIAG_STAGE_INTERPRETER,
    PSP_DIAG_STAGE_NATIVE_ACCEPT,
    PSP_DIAG_STAGE_TRIVIAL_CULL,
    PSP_DIAG_STAGE_TRIANGLE,
    PSP_DIAG_DRAW_SKIP_COLOR16,
    PSP_DIAG_DRAW_SKIP_DEPTH_ALIAS,
    PSP_DIAG_DRAW_SKIP_SPECIAL_TEXTURE,
    PSP_DIAG_DRAW_SKIP_TEX_DECODE,
    PSP_DIAG_DRAW_SKIP_NO_TARGET,
    PSP_DIAG_FB_INVALID_GENERATION,
    PSP_DIAG_FB_NOT_RENDERED,
    PSP_DIAG_FB_SELF_ALIAS,
    PSP_DIAG_FB_WORK_OWNER,
    PSP_DIAG_FB_OTHER_ALIAS,
    PSP_DIAG_FB_ALIAS_VALID,
    PSP_DIAG_FB_WORK_REPLACED,
    PSP_DIAG_NATIVE_EFFECT_UNSUPPORTED,
    PSP_DIAG_FB_UPLOAD_UNSUPPORTED,
    PSP_DIAG_COUNT
};
static const char *const PSP_DIAG_LABELS[PSP_DIAG_COUNT] = {
    "stage-invalid-arguments",
    "stage-clip-invalid-input",
    "stage-clip-disabled",
    "stage-projection-invalid",
    "stage-polygon-clip-failed-or-overflow",
    "stage-interpret-fallback",
    "stage-native-accepted",
    "stage-invisible-culled",
    "stage-triangle-submitted",
    "draw-skipped-unsupported-fb16",
    "draw-skipped-color-depth-alias",
    "draw-skipped-special-texture",
    "draw-skipped-no-decoded-texture",
    "draw-skipped-no-target",
    "fb-texture-generation-changed",
    "fb-texture-never-rendered",
    "fb-texture-self-alias",
    "fb-texture-work-owner-changed",
    "fb-texture-other-reject",
    "fb-texture-accepted",
    "fb-work-surface-replaced",
    "native-effect-not-implemented",
    "framebuffer-upload-not-implemented"
};
typedef struct PspDiagAllSample {
    uint32_t frame, fbp, zbp, tbp, detail;
    uint64_t tex0, test, prim;
    int ctx;
} PspDiagAllSample;
typedef struct PspDiagAll {
    uint32_t counts[PSP_DIAG_COUNT];
    PspDiagAllSample first[PSP_DIAG_COUNT];
    uint32_t lastFrame[PSP_DIAG_COUNT];
} PspDiagAll;
static inline void PspDiagAll_Reset(PspDiagAll *d) {
    memset(d, 0, sizeof(*d));
}
/* Counters always represent observed calls, not pixel corruption proof.
   One sample per category prevents high-frequency log I/O. */
static inline void PspDiagAll_Record(PspDiagAll *d, unsigned category,
                                     const PspDiagAllSample *s) {
    if (!d || !s || category >= PSP_DIAG_COUNT) return;
    if (d->counts[category] == 0u) d->first[category] = *s;
    if (d->counts[category] != UINT32_MAX) ++d->counts[category];
    d->lastFrame[category] = s->frame;
}
#endif
