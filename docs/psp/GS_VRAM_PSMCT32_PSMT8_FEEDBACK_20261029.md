# PSP GS 4 MiB coherent memory / PSMCT32 to PSMT8 feedback (2026-10-29)

## What changed

The PSP GU only renders RGB5551 colour; a GPU-produced RGB5551 image cannot be losslessly converted back to PS2 GS PSMCT32 (8-bit RGBA, destination-alpha, 32-bit FBMSK, depth and byte aliases). In particular, using that GU snapshot as a PSMT8 GS source could return the wrong index bytes, or the old `gs_core.c` 4 MiB RAM image could contain stale source pixels.

This patch introduces **demand-driven software mirroring of actual GS draw operations into the existing 4 MiB PS2 GS memory**, using `gs_core.c`'s reference `pixel`, `draw_sprite`, `draw_triangle`, `shade`, `vram_rw` and address/swizzle tables. Rather than reverse-convert GU pixels, it rasterizes the SAME GS source primitive with its full GS `FRAME/TEST/ZBUF/ALPHA/TEX0` state, writes original PSMCT32, PSMCT24, PSMCT16/16S and Z formats into the same VRAM used by `Gs_VramRead`, `Gs_VramReadRect` and `Gs_VramReadIdx8`. PSMT8 then naturally indexes the actual bytes in the same 4 MiB space.

When the GPU backend first sees an incompatible `FRAME.PSM -> TEX0.PSM` framebuffer alias, it enrolls that logical FBP for subsequent reference draws. The normal `GsGpu_Draw` and native triangle fast batch BOTH mirror enrolled producers, using original GS state and vertices. Writes track the precise 8 KiB GS pages; one epoch/generation bump per touched page per draw invalidates indexed texture caches, including alpha-only GS writes. No 5551 readback or CPU read of unsynchronized GE pixels is needed; no additional GS framebuffer allocation.

**The regular PSP GE renderer still draws the visible screen.** This patch adds a correctness path only for producers whose feedback was detected, to avoid duplicating every draw and destroying performance.

## Tests

- `test_gs_feedback_mirror.c`: 4 MiB blocks/pages, per-FBP enrollment, per-draw page stamps, raw RGBA32 component bytes vs lossy 5551 reconstruction.
- `test_fb_feedback_provenance.c`: incompatible PSM readback detection.
- Both PSP GitHub Actions workflows run those tests before building the EBOOT.
- Logs: `psp: exact GS VRAM feedback mirror: tracked-fbps=... CPU-rasterized-draws=... dirty-8KB-pages=...`.

## Boundaries — do not claim complete GS emulation

1. **First-observation gap:** a framebuffer created *before* its first detected incompatible read is not reconstructed retroactively. It becomes coherent as the producer is redrawn; persistent one-shot targets require an earlier enrollment or a separate replay mechanism.
2. The reference software rasterizer is not proven bit-for-bit identical to GS hardware for every exotic GIF and VU1 operation, and there is still a separate PSP GE RGB5551 visible image. GPU-only post-effects in `GsGpu_Native` are not mirrored.
3. This path can be CPU-expensive. Observe `CPU-rasterized-draws`, actual game FPS, crash/heap conditions, and before/after visuals. If lots of producers are enrolled it can REDUCE FPS. No claim of 60 FPS or zero flicker without PPSSPP tests.
4. This is *not* a blanket GPU-to-CPU 32-bit readback; it is source-command mirroring for supported GS primitive types 0/1/3/6. Native VU1 triangles are covered when emitted through `GsGpu_Draw` or `Psp_NativeEmitTriangle`.
5. Other cases (reused work-target owners, unmirrored passes, indirect transfers, exact destination-alpha display composition) still require follow-on reconstruction.

## Validation in PPSSPP

Test EBOOT `TP3-GS-VRAM-EXACT-FEEDBACK-20261029` on the same battle and graphics settings. Compare corrupted colour bands and giant polygons independently, and upload a log with `exact GS VRAM feedback mirror`, `GPU-only GS feedback missing cross-PSM readback`, `GS framebuffer virtualization`, `FB alias`, and time/FPS statistics. If mirror count is 0 while cross-PSM readbacks are high, adjust producer detection. If mirror counts rise but colours are still corrupt, compare PSP output and the 32-bit GS software framebuffer at the first offending draw.

Preserves existing colour/font corrections including `fe93677`, prior clipping fixes, and old EBOOT builds (no merge into main). No proprietary game assets or game ISO included.
