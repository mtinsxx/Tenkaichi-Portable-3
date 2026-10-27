# Battle FPS: reduce repeated GS texture/state work on VU1 stage triangles

## Real battle log Oct 26 20:18 build (PR #37)
Recent battle windows: **four GS frames per 2.5 s (1.5–1.6 FPS)**,
about 4,750–4,850 stage native strips, ~49k stage vertices,
~660 provably invisible strips skipped (PR #37), still 88–142
fallback strips. `PROFILE GS draws`: **~114,000 calls** per 4 frames
with ~750–880 ms CPU sampled, a large remaining component even after
the original interpreter optimizations. `GSchain` ~2,150–2,360 ms
per interval. `GUfinishSync=0`, so bottleneck is CPU-side command
translation and prep, not waiting for GPU queue completion.
Stage program 399 still ~180–284 interpreter calls per window, but
far fewer than the ~1,800 before PR #37.

## Root cause
`GsGpu_DrawVu4` generates triangles for an already-prepared
native stage strip, but then invokes `GsGpu_Draw(3,...)` for each
triangle, even though the strip has ONE constant TEX0 / PRIM /
TEST / CLAMP / FRAME / scissor. `GsGpu_Draw` repeats texture cache
checks, framebuffer routing and GS->GU full state comparison on
each triangle. State batching avoids duplicate PSP GU calls, but
not the repeated *CPU* translation.

## Fix
In `GsGpu_DrawVu4`, execute the full validated `GsGpu_Draw`
for the first accepted triangle. While in that same stage strip,
following triangles directly append EXACTLY the same PSP GU vertex
format as the normal `GsGpu_Draw` method:
- x/y scaled 480/512 and 272/448; Z24->Z16 remains unchanged.
- Texcoord FST: UV texels/16 with 3D texture LOD conversion.
- Non-FST: ST/Q * texture dimension, same affine PSP path.
- Per-triangle flat vs interpolated RGB, /128 PS2->PSP MODULATE
  conversion, PS2 alpha 0x80 -> PSP 0xFF.
- Preserve triangle winding/order and flush vertex buffers without
  reordering GPU geometry or altering textures or effects.
- Only eligible for native stage program4 after the **first accepted
  triangle**; do not bypass source alpha DATE/stencil masks or
  framebuffer-as-texture special scaling. Do not change fighters,
  HUD, game simulation, or the original VU1 interpreter.

Configuration `native_stage_batch=1` by default; set to 0 to
fully restore the old per-triangle path.

New logger:
`stage GS triangle batching: starts=... fast-triangles=... enabled=...`.
The GS draw time profiler samples only slow-path calls, not directly
appended fast triangles. Treat wall-time `GSchain` and measured
real GS FPS as the authoritative A/B indicators.

## Validation
Host pure-C tests compare FST/ST-Q coordinates, IIP/flat coloring,
opaque alpha, depth and screen scaling against a reference copy of
the existing packing equations. CI PSP builds EBOOT with the change.
Visual accuracy and actual speed require a real PPSSPP A/B capture
of identical fight/camera, `report=1,native_stage_batch=0` vs `=1`.
No FPS gain is promised until that comparison.
