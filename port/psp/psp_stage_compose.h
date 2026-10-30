#ifndef TP3_PSP_STAGE_COMPOSE_H
#define TP3_PSP_STAGE_COMPOSE_H
#include <stdint.h>
/* Conservative battle-stage -> fighter GE transfer policy.
 * A stage framebuffer remains independently rendered until a native
 * fighter is about to draw into ANOTHER physical screen framebuffer.
 * In that case only copy completed 5551 pixels before the fighter
 * triangles, using sceGuCopyImage in the same ordered display list.
 * Logical GS targets and framebuffer-as-texture snapshots stay separate.
 * This is a guard for a visual A/B experiment, not GS-accurate blending
 * or destination-alpha reconstruction. */
/* Stage strips can submit hundreds of triangles AFTER the first
 * accepted draw. Capturing the first triangle's framebuffer version
 * causes the end-of-strip copy to fail the written==recorded test.
 * Refresh only when this frame's final GPU content has a newer
 * version (or when this is the first valid record of the frame). */
static inline int PspStageCompose_NeedsFinalRecord(
       unsigned frame,unsigned previousFrame,
       unsigned written,unsigned previousWritten,
       int previousValid) {
    return written!=0u && (!previousValid || previousFrame!=frame ||
                           written!=previousWritten);
}

/* A stage framebuffer may lose ownership of a shared PSP WORK backing
 * before the first fighter draw. A completed 5551 snapshot already
 * queued in the GE list is a valid *visual* source if its version and
 * originating GS generation still match the stage record. Never
 * resurrect an older scene after another stage draw changed the image. */
static inline int PspStageCompose_UseSnapshot(unsigned recordedVersion,
                                               unsigned actualVersion,
                                               int matchingGeneration,
                                               int savedValid,
                                               int physicalReady,
                                               int ownerValid) {
    return recordedVersion != 0u &&
           recordedVersion == actualVersion &&
           matchingGeneration && savedValid &&
           (!physicalReady || !ownerValid);
}

static inline int PspStageCompose_ShouldCopy(unsigned now,
                                             unsigned stageFrame,
                                             unsigned stageWritten,
                                             unsigned recordedWritten,
                                             uintptr_t source,
                                             uintptr_t destination,
                                             int destinationIsScreen,
                                             int sourceReady,
                                             int sourceStillOwned,
                                             int alreadyComposed) {
    if (!destinationIsScreen || !sourceReady || !sourceStillOwned ||
        alreadyComposed || !source || !destination ||
        source == destination || stageWritten == 0u ||
        stageWritten != recordedWritten)
        return 0;
    /* At most the current or immediately previous PSP frame. Do not
     * resurrect stale stage images after battle/menu transitions. */
    return (unsigned)(now - stageFrame) <= 1u;
}
#endif
