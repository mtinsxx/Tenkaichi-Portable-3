# FPS PR39 — batch the two layers of native fighter VU1 strips

## Evidence from `log(20261026-204248).txt`
The PSP stage triangle batch PR38 was reached (median ~22.8k fast
triangles per active interval), but the battle remained ~1.4 real GS
FPS. In an ordinary ~4-frame combat interval, native fighter VU1
reports about **4,416 successful native strips / 39,648 vertices and
zero rejected strips**. The `GsGpu_DrawVu0` caller still emitted
every triangle via `GsGpu_Draw(3,...)` separately, twice (base/cel).
Meanwhile GSchain remains ~2s with ~72-120k draw calls per interval.

This is a concrete additional CPU-side graphics bottleneck:
`GsGpu_Draw` recomputes texture ID, looks up the cache, resolves render
target, constructs full GS blend/stencil/scissor state and compares the
struct for every triangle even when the same strip/layer shares all
these fields. The stage PR38 optimization didn't reach fighter work.

## Fix
Share `Psp_NativeEmitTriangle` for **both** native stage VU1 program4
and native fighter VU1 program0/1 strips. The first triangle of each
layer always takes the complete original codepath. Following triangles
share its validated PSP state, with pixel-equivalent vertices and
original order. A NEW fast-batch is constructed for EACH fighter
base and cel/fade layer, so the second layer's TEX0, alpha and colors
are never reused from the first layer.

Safety gates: require normal flat sprite mode, successful initial GS
draw, no stencil/pixel mask/DATE, and no framebuffer alias texture.
Unusual passes stay in original `GsGpu_Draw`. All fighter VU1 math,
bone skinning, cel/fade lighting, textures and HP logic are unchanged.
`native_fighter_batch=0` reverts just this optimization, leaving
`native_stage_batch` independent.

New log:
`psp: fighter GS triangle batching: starts=N fast-triangles=M enabled=1`.

The existing native-stage per-vertex reference parity host test still
covers the shared vertex emitter equations, and the PSPSDK build
compiles both calling sites.

## Honest outcome / next step
CI confirms compilation, not PPSSPP visual accuracy or faster FPS.
Compare identical fights with `report=1` and fighter_batch 0/1;
check visual parity in all forms and special attacks. If M=0,
no acceleration happened, or if FPS and GSchain remain unchanged,
do not claim success; focus on real VU1 fallback and GS/texture cache
costs. The log also reveals heavy texture churn (~49k cumulative
evictions), so a later cache reuse redesign may be important.
