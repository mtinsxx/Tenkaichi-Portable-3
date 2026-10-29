# BT3 PSP native renderer performance — CPU GS feedback bounds (2026-10-29)

## Goal and current limits
Target performance closer to a purpose-built PSP title like Tenkaichi Tag Team, while preserving BT3 character animation and game mechanics. The last user tests still show 0–1 FPS and black/giant stage polygons even though the 4 MiB GS VRAM feedback mirror removed rainbow texturing. Achieving Tag Team performance requires further native geometry, effect and texture work; this **single patch does not achieve it**.

## Confirmed bottleneck
The existing `Gs_PspMirrorDraw` mirrors CPU primitives to the original 4 MiB swizzled GS VRAM so PSMCT32/PSMT8 reinterpretation stays byte-correct. Its reference `draw_sprite` and `draw_triangle` loops calculate pixel colors/textures/depth for bounding-box pixels outside the GS SCISSOR and only reject them inside `pixel()` AFTER doing all that expensive work. Large stage polygons can make that a huge extra CPU cost.

## Implemented (always on; no separate performance mode)
- `gs_raster_bounds.h` intersects the primitive raster window with the exact original GS inclusive 11-bit SCISSOR X0/X1/Y0/Y1 fields and hardware's 1024×1024 surface bounds.
- Only the **PSP GS software mirror**, not the hardware GU's native model output, applies this earlier loop bound to sprite and triangle draws. All in-bounds pixels still use the identical original source vertices, barycentric weights, UV, shade, alpha, depth and PS2 swizzled VRAM writes.
- Fully offscreen primitives do not enter costly texture lookup/shading loops. Partially offscreen stage polygons run no outside-scissor pixel work. Some large geometry can go from ~1,000,000 box candidates down to the visible GS window, without losing any valid pixel.
- Cumulative log `native GS mirror scissor: fully-culled=... pixel-candidates-skipped-k=...` provides evidence of CPU work avoided (counts are candidates, **not measured milliseconds**).
- `test_gs_mirror_scissor.c` compares original per-pixel GS SCISSOR acceptance against optimized bounds for hundreds of rectangles and edge cases. Both PSP build workflows run this test; unchanged `test_gs_feedback_mirror.c` ensures source-memory feedback remains supported.

## Not done / remaining work
This is an **incremental** native PSP optimization, not a port of the proprietary Tag Team engine, not texture/asset preprocessing, not a native replacement for stage399 VU1, not complete GS color/depth render pipeline reconstruction, and not a guarantee of 30/60 FPS. CPU still mirrors framebuffer producers when cross-PSM feedback is required. The current unknown black polygons/0-FPS may involve incorrect geometry, GS display buffers or CPU load. Do not claim improved FPS without PPSSPP evidence.

The test EBOOT retains the baseline `fix/tp3-stage-final-version-composition-20261029`, including earlier PSMCT32/PSMT8 feedback repair, white glyphs from `fe93677`, native VU clipping and framebuffer stage composition. No ISO, assets, or keys added.

## Validate
Version `TP3-NATIVE-GS-SCISSOR-20261029`. Compare the same arena and PPSSPP settings with previous build and provide `log.txt`: `native GS mirror scissor`, `exact GS VRAM feedback mirror`, `PROFILE`, `native stage VU1 program4`, `stage composition version fix`. If avoided candidates rise but real FPS stays 1, prioritize VU1 stage399 native fallbacks and page mirroring volume, not further speculative scissor changes.
