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
