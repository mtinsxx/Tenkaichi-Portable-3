# BT3-PSP FPS bottleneck: evidence-based attribution

## Observations from user's last accessible detailed battle log
`log(20261026-160641).txt` was captured on a build from **15:47**,
before PRs #29-35; it cannot prove the latest build's current FPS.
In a stable combat interval:
- 2 GS frames in ~3.4s, about 0.6 GS FPS
- `GSchain=3268ms (2 chains)`, `gameCPU=132ms`, `GUfinishSync=0ms`.
  CPU emulation/translation dominates the measured wall interval, not
  waiting for the PSP GPU.
- 51,203 triangles, ~52,645 GS draw calls, ~4,335 GU batches.
- `VU1-interpreter=6896 calls`, estimated ~4.97s extrapolated from
  every 16th call. This **estimate can exceed GSchain** due to biased
  sampling and overlap: it is not additive or direct wall-clock proof.
- `native stage4 batches=11 vertices=55 conservative-fallbacks=2310`.
  PR #30 added native clipping and may change these counts; must retest.
- `native fighter batches=856 rejected=1132`.
- `PROFILE textures key+CLUT+lookup ~649ms`, `GS draws ~899ms`
  (both sampled). PR #29 added guarded texture reuse, so numbers
  may have improved in the newer versions.

## Source bottlenecks and mechanism
1. `port/src/gs/gs_vu1.c:GsVu1_Call` falls back to `run()`, a
   dynamic VU1 instruction interpreter with decoded operands and VU
   hazards, when native handlers reject a batch.
2. `port/src/gs/gs_core.c:vertex / Gs_Gif` translates GS register
   streams and calls `GsGpu_Draw` for each primitive.
3. `port/psp/psp_gu.c:GsGpu_Draw` performs state/texturing work
   on each triangle, even when VU1 transforms are handled natively.
   Approximately 26,000 draw calls per rendered frame are emitted in the
   cited older battle.  The PSP GU path is affine and does not yet use
   a batch-of-triangles path.
4. `port/src/gs/gs_core.c` has a specialized `tri_run()` for a
   particular GIF REGLIST effect pattern, but it and
   `GsGpu_DrawTriRun` are **PORT_VITA-only**. Directly enabling it on
   PSP would be unsafe until implemented and validated with matching
   raster/state behavior.

## This PR: diagnosis (no rendering changes)
Group the existing 1-in-16 sampled `run()` work by uploaded VU1
microprogram size (stage399, fighter127, fade124, effect451,
shadow99/101, other recognized HLE94/104, unknown).
Log each bucket's `runs/instructions/estimated-ms` every report
interval. Cost is a few counter increments per **VU1 invocation**,
NOT a clock call per instruction; it uses the already existing timing
sample so is relatively low-overhead. It introduces no changes to GS
state, GPU routing, model textures, VU1 instructions, gameplay, or
characters. Host checks ensure program categorization.

## Required validation
With `report=1` run the same PPSSPP battle on current EBOOT and send:
`PROFILE interval`, `PROFILE VU1 programs`,
`PROFILE VU1 other`, `PROFILE GS draws`,
`texture guarded reuse`, `native stage VU1`,
`native fighter VU1`.
Then prioritize actual largest contributor in the current build.
Do not promise 30/60 FPS or a fixed speed without comparative logs.
