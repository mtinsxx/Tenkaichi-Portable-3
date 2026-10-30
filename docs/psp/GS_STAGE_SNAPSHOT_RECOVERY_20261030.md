# PSP GS scene recovery — experimental test 2026-10-30

Build ID: `TP3-GS-STAGE-SNAPSHOT-20261030`

## Evidence driving the patch

In the supplied gameplay video the sky and 3D fighters can survive while arena geometry disappears or flashes as the scene changes; menu layers also intermittently vanish. The existing `log.txt` recorded hundreds of thousands of framebuffer-alias fallbacks and repeated `stage compositing rejects: owner-lost=1988` in some intervals; `stage-to-fighter background copy: copied=0`. These symptoms **do not prove** the compositor is the only cause.

## Code changes

1. When a PSP work buffer is re-owned, the prior logical GS target is copied into a 512x512-padded RGB5551 texture in GE-addressable main RAM by the existing `snapshot_target` mechanism.
2. Stage/fighter composition now accepts that versioned snapshot as its source if the physical WORK buffer was reused or the saved logical framebuffer moved to another physical backing. It verifies exact logical FBP, version, GS page generation, and snapshot validity. Compositing still obeys the original PSP-frame age/target/duplicate guards.
3. A new direct microsecond timer around `Gs_PspMirrorDraw` reports actual accumulated mirror CPU time per log interval as `GS exact mirror CPU measured elapsed`, rather than multiplying a small sample by 16/32. This distinguishes raster work from other GS/VU1 paths.
4. `test_stage_compose.c` checks acceptance only when snapshot/version/generation are valid, rejection for mismatches, and the existing GE composition guards.

## PSP limits retained

- Two physical 480x272 RGB5551 work surfaces support the 336/368 GS framebuffers; the first-party framebuffer snapshot pool is capped at three 512x512x2-byte images (1.5 MiB) in main RAM. No new full-size GPU framebuffer is allocated.
- PSP hardware GU cannot losslessly represent the 32-bit PS2 framebuffer, indexed PSMT8 view, destination alpha and PS2 swizzle simultaneously; retain the exact 4 MiB PS2 GS CPU memory mirror and fallback for these cases.
- No separate performance mode, ISO, proprietary game assets, bt3.txt dependency or different character/control layout. Keep the `fe93677` font/colour legacy fixes.

## What this does NOT establish

A successful GitHub Actions build verifies compilation and host tests, not real-game FPS or GPU output. Snapshot restore is a targeted repair for work-buffer lifetime; full GS reconstruction, efficient hardware-native feedback and the stage/overlay flicker are NOT proven fixed. Confirm in PPSSPP with comparison of the previous screenshots and `GS stage owner recovery: snapshot-composed`, `GS exact mirror CPU measured elapsed`, `PROFILE interval`, `stage compositing rejects` and `fb-alias` in a fresh game log.
