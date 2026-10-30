# BT3 PSP — GC software GS feedback: row spans and exact source-alpha

Build ID: `TP3-GS-FEEDBACK-ROW-BLEND-20261030`

## Why this change is different from earlier VU-focused builds

The player's 10,112-line `log.txt` from
`TP3-GS-CPU-MIRROR-FAST-20261030` confirms the game becomes
almost frozen in battle while its PSP software GS mirror runs.

One interval at the end of the log contains **two GS chains spanning
84,176 ms**, `PROFILE GS draws ~= 59,963 ms` for 20,309 draw
calls, versus `gameCPU=432 ms` and estimated-VU1 ~322 ms.
The exact mirror counters reach 112,640 CPU-rasterized draws.
These are measured/sampled interval counters, not a reliable
standalone FPS number.

**The GS framebuffer mirror is required for exact PSMCT32 ↔
PSMT8 aliasing, HUD colors and GPU feedback.** This patch keeps
it and accelerates specific expensive work without guessing what
the framebuffer should look like.

## Actual code changes

1. **Row-major raster early span rejection:** for the original
   software GS triangle scan, test groups of 16 contiguous X pixels
   against TWO affine triangle halfspaces. If both endpoints of the
   group are strongly outside the same halfspace, ALL sixteen
   fail the original 2D inside-triangle test; skip the entire group
   *before* edge equations, double-precision divides, texture/CLUT
   reads, alpha blend and frame/depth swizzle.
2. **Safe boundaries:** only apply to finite areas of magnitude at
   least 1 and vertex coordinates inside ±2048. Use a large
   16-edge-unit margin to keep all pixels close to edges in the
   exact original path. The **row/column traversal order remains
   identical** for every pixel that is processed, preserving
   self-read/write feedback semantics.
3. **Lossless GS alpha equation:** PS2 blends via
   `((A-B)*C>>7)+D`. If `A==B` and `D=source`, or if `C`
   is fixed zero and `D=source`, the result is **exactly the
   source color**. Skip the framebuffer read, 3 blend channels
   and multiply/shifts in this special case. All other blend
   equations still use the original GS arithmetic, alpha/depth
   tests, FBMSK and DITHER semantics.
4. **Runtime log:** `GS CPU mirror row spans` reports
   skipped pixels in thousands and source-equivalent blend
   operations avoided. Counts are cumulative, not timing proof.

## Validation and limits

`test_gs_mirror_row_span.c` compares the optimized skip against
original double-barycentric coverage for thousands of random
triangles, extra triangles up to ±1700 and both windings.
`test_gs_mirror_source_blend.c` compares 200k randomized GS
blend registers/inputs and destination-alpha values against the
original integer formula. CI must compile the entire EBOOT.

**This is a real source-code change targeting the measured
bottleneck but NOT a complete hardware-native GS rewrite.**
Some scene triangles may cover many pixels, so even perfect
outside-pixel culling cannot eliminate their expensive alpha,
depth, CLUT and framebuffer operations. The PSP GU stores
RGB5551 while the original PS2 GS framebuffer can contain
32-bit colors and cross-PSM texture views: blindly copying
5551 back to GS memory would corrupt images.
A complete replacement needs a verified GPU/CPU shared render
target model and cross-PSM pixel tests, not a blanket deletion
of the mirror.

There is no claim of 30/60 FPS in PPSSPP or of all scenery
artifacts being fixed. The prior exact GS mirror, `fe93677`
font/HUD colors, native fighter model loader, video and controls
are unchanged. No ISO, character data or copyrighted assets
are included.
