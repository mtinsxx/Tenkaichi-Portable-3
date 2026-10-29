# Stage -> fighter composition: final framebuffer version (29 October 2026)

## Observed PPSSPP failure
The recording `7696.mp4` shows the game alternately rendering scenery, green/blue outsized polygons, and black gameplay frames while the PPSSPP touch overlay remains. PSMCT32/PSMT8 GS memory feedback already removed rainbow artifacts on the previous build; preserve that work (PR #17), the stage fast-path snapshot fix (PR #18), and the established color/font fix `fe93677`.

## Concrete first-triangle-version bug

`Psp_StageRecordDraw(ctx)` only ran after the *first* normal native stage triangle of a VU1 program4 strip, inside the slow-path `Psp_NativeEmitTriangle` branch. Subsequent triangles could draw into the same GPU target through the cached fast path. That path did not call `PspGsFb_OnDraw`, and the stage record was not refreshed at strip end. If any other normal triangle updated the target, `sStageImage.written` became stale. `Psp_StageComposeBeforeFighter` then required `t->snapVersion.written == sStageImage.written` and refused to copy the stage into the fighter display target, leaving a partially rendered / black background.

Fix:
1. Advance the target's GPU write version for **every native fast triangle** using `PspGsFb_OnDraw()`. This also correctly invalidates any old framebuffer snapshot.
2. Refresh `Psp_StageRecordDraw(ctx)` at the **end of each accepted native stage strip**, so the stage metadata reflects the final version rather than its first triangle.
3. Preserve existing GE submission order: the copy still flushes all stage triangles *before* enqueuing `sceGuCopyImage` ahead of fighter geometry; no extra `sceGuSync`, broad framebuffer clears, loss of assets, or change in FPS pacing.
4. Track `end-of-strip-updates` and `native-fast-GPU-writes` in the regular PSP log for A/B correlation.

Host regression `port/psp/test_stage_compose.c` constructs the real failure sequence (first draw version 1, 90 more triangles, final version 91), verifies the old copy guard would reject stale version 1, and verifies a refreshed final version 91 permits safe composition. The existing PSP workflow runs the test and compiles EBOOT.

## Known limitations
This **does not prove** that all gigantic green triangles are fixed; those could originate earlier in the VU1 stage399 triangle stream, clip-plane transform, depth/compositing, or PS2 GS target routing. This fix is limited to one confirmed metadata/version mismatch capable of producing the observed black-background alternation. The frame rate around 1 FPS also requires independent VU1/GS mirror profiling.

Build marker: `TP3-STAGE-FINAL-COMPOSE-20261029`. Compare the identical scene using the saved preceding EBOOT and PPSSPP settings, supply video and `log.txt` with `stage composition version fix`, `stage-to-fighter background copy`, `frame composition`, `native stage VU1 program4`, `exact GS VRAM feedback mirror`, `PROFILE`.
