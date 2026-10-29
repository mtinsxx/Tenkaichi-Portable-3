# PSP VU1 program6 clipping + GS feedback provenance (2026-10-29)

## Why this test exists

The PPSSPP captures of the previous EBOOT (GE vertex lifetime) retain extremely large ground/stage triangles, white/color banding, and ~1 FPS during battles. A clean PSP compiler is not proof of visual correctness.

## Implemented: program6 clipping

`docs/systems/vu1/prog6.md` verifies that the actual PS2 VU1 program6 uses constants 0..3 as world->screen, 4..7 as world->clip, 8..11 as clip->screen, 12..15 as shadow UV projection and 16.x as texture scale. It also emits a triangle strip and extra clipped fans. The previous native PSP `GsGpu_DrawVu6` transformed each vertex but **never clipped against the six planes**.

`PspShadow6_ClipInput` now forms the original clip-space inputs and interpolated shadow UV/colors. `GsGpu_Shadow6CanRender` preflights a complete strip, caches six-plane clipped fans, and refuses unsafe W/overflow/unsupported clipping BEFORE changing GS state. When preflight rejects the strip, `gs_vu1.c` runs the reference program6 interpreter from batch_start PC=21 instead of dropping polygons. All accepted triangles preserve original strip parity and ADC flags. This is an implementation test, not a proven visual fix.

## Added: GS feedback missing-readback provenance

In the PSP GPU path, GPU-rendered FBP RGB5551 pixels are not losslessly stored in the PS2's original 4 MiB GS VRAM. When a recent GPU surface is subsequently sampled through a different TEX0 PSM (especially indexed PSMT8), the 16-bit snapshot is NOT a valid raw GS memory alias; a fallback GS-memory decoder can be stale. Added explicit per-interval accounting:

`psp: GPU-only GS feedback missing cross-PSM readback: draws=... indexed=... last-producer-fbp=... last-FRAME-PSM=... last-TBP=... last-TEX0-PSM=...`

This is **diagnostics**, not a fabricated RGB5551->PSMCT32 readback. No full cross-format GS VRAM reconstruction is implemented in this patch; completing it requires an accurate intermediate GS buffer and synchronization, not a quick lossy pixel conversion. Native post-effects remain incomplete.

## Evidence and next test

- Host regression: `test_shadow6.c` includes a partial +X clipped quad with UV and alpha validation; `test_fb_feedback_provenance.c` covers incompatibility detection.
- Both PSP CI workflows run the tests before EBOOT compilation.
- Build marker `TP3-VU6-CLIP-GS-FEEDBACK-20261029`.
- For A/B, use the same stage, PPSSPP settings and character pose; capture old/new frame screenshots and `log.txt`. Inspect `native ground shadows VU1 program6` (clipped-faces/interpreter-fallbacks) and `GPU-only GS feedback missing cross-PSM readback`. If the shadow counters remain zero while bad stage polygons persist, focus the next geometry investigation on program4/other draw paths.
- This does not purport to fix the ~1 FPS performance bottleneck.
- Original `fe93677` color and text improvements remain unchanged, and game assets/ISO are not included.
