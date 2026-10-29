# Stage visual regression: framebuffer snapshot and GE list fence (2026-10-29)

## Actual PPSSPP evidence

After the experimental GS 4 MiB exact-memory feedback mirror (PR #17), the rainbow corruption is nearly absent. The 40.8-second recording and two still captures nevertheless show oversized green stage polygons, large black gaps in the foreground, and ~0-1 actual FPS. Do not revert the GS memory mirror or the pre-existing white-font correction `fe93677`.

## Root-level problems fixed in code

1. The cached native stage/fighter triangle batcher treats **only a physical PSP colour target** as a framebuffer texture. It fails to recognize the **512x512 snapshot in main RAM**, which is now the normal source for framebuffer feedback. It then uses generic texture coordinates (`PspStageTriPack`, tw=th=512) instead of `GsGpu_Draw`'s explicit GS logical TEX0 width/height and GS-to-PSP physical backing scales. The fast path now declines both color-target and snapshot aliases, allowing the established generic renderer to retain the correct feedback UV conversion and texture state.
2. If the immutable vertex arena fills, `psp_gu_vb_reserve` closes the old command list, synchronizes the GPU, starts a new list and **invalidates the applied GU state**. An active native fast batch previously proceeded through that state transition without passing through `GsGpu_Draw` to re-establish texture/depth/alpha target state. Now a cached batch becomes inactive before any low-space fence; the next triangle takes the normal state setup path. This also handles lists invalidated by other operations.

`test_stage_tri_batch.c` verifies snapshots vs ordinary textures and state/arena edge cases. Regular PSP CI already compiles and runs that test. Build marker `TP3-STAGE-GU-STATE-SNAPSHOT-20261029`.

## Limitations, performance, and validation

These are concrete correctness bugs but not a proof that *all* giant stage triangles are caused by them. In particular this does not implement universal GS clip/primitive reconciliation, and the logged 1 FPS still needs profiling. Falling back from the fast path for framebuffer textures may cost some CPU, but most ordinary texture triangle batches remain native. **No geometry, texturing or stage objects are intentionally removed.**

Compare the same battle on PPSSPP and provide screen/video and `log.txt`. If black gaps/green wedges persist, isolate VU1 stage399 (native vs interpreter) and stage rendering target composition separately, using reliable evidence.
