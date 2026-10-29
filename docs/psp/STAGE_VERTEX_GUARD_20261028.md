# PSP stage vertex explosion guard — 2026-10-28

## Captured problem
The player's PPSSPP image still shows very large grey/white polygons across the arena, coloured framebuffer strips and actual fighting speed around 1 FPS. These symptoms may have multiple independent causes; a still image alone cannot prove which triangles were produced by bad VU1 projections.

## Experiment implemented
Based on `fix/tp3-psp-gu-vram-address-20261028`, with the original colour/font fix `fe93677` preserved, the stage VU1 program4 path now conservatively checks:
- Input screen coordinates before XYOFFSET fit the GS XYZ2 16-bit 12.4 fixed-point domain. Unrealistic projected coordinates do **not** become enormous floating-point PSP triangles; the entire stage strip returns to the reference VU1 interpreter.
- For triangles requiring native six-plane clipping, the normal-strip `screen_matrix * position` and clipped-fan `clip_to_screen * (clip_matrix * position)` homogeneous projections agree within one GS screen pixel. The reference interpreter handles ambiguous camera state rather than committing inconsistent native strips.
- Any clipped fan whose computed GS screen position is outside the 16-bit domain is rejected during the preflight cache construction, before `hle_program4()` writes GS registers. No partial native draw or triangle deletion.

`port/psp/test_stage_vertex_projection_guard.c` is executed by `.github/workflows/psp.yml` along with existing rendering tests. The runtime report adds `stage vertex projection guard: dual-matrix-rejected`.

## Limits and test criteria
This is a safety/fallback fix for inconsistent or out-of-range stage geometry, **not** a full rewrite of the PS2 GS renderer. Its fallback may **decrease** FPS if many strips have incompatible matrices. Cross-format PSMCT32/PSMT8 framebuffer readback, effects and screen-wide colour corruption are still unresolved. No claim that flicker or FPS was fixed until a real PPSSPP run.

Test `TP3-STAGE-VERTEX-GUARD-20261028` on the identical stage, characters and PPSSPP settings. Compare giant triangles and colored bands separately. Collect actual FPS and `native stage VU1 program4`, `stage vertex projection guard`, `PROFILE VU1 programs` and `GS framebuffer virtualization` from log.txt. If the screen glitches are identical and the dual-matrix-rejected counter is zero, this experiment did not address the visible cause; focus next on indexed framebuffer feedback and GS primitive conversion. No ISO, proprietary ELF or game assets are included.
