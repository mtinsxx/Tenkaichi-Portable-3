# BT3-PSP: 2D life / ki / timer HUD depth isolation (experimental)

## Symptom
Screenshot after PR #32: characters/menu text and some bright green life and
blue ki fragments render; there are long dark stretches and apparent missing
parts in the top horizontal HUD. The screenshot alone cannot distinguish
intentionally depleted life from depth test/target-induced missing pixels.
**No proof of a visual fix yet.**

## Reason to isolate this path
PS2 GS HUD primitives are sprites, normally in the top of the 512x448
coordinate space. The PSP port already reproduces most sprite alpha/texture
blending but uses ONE PSP physical depth surface for alternating GS display
and work FBPs. With `gs_depth=2`, Z testing/writing for HUD sprites
can therefore depend on 3D geometry rendered before them.

Only TOP HUD 2D sprites (screen target; original PS2 coords in
x:-1..513,y:-1..78) are classified. If `hud_top_overlay=1`:
* Disable depth testing and depth writing for these sprites only.
* Preserve original draw order, TEX0, alpha test, blending, clip/scissor,
  UV, RGBA, 3D texture LOD, and font/character/stage implementation.
* No changes to native fighter VU1, character textures, stage geometry,
  animation or lighting.
* `hud_top_overlay=0` restores the previous exact logic.

## Diagnostics
`psp: top HUD sprite depth` logs sprite count, requests for GS ZTE/Zwrite,
the number depth-isolated, along with TEX0 TFX and CLAMP-S distributions
for top HUD sprites. If GS depth is NOT requested for these sprites,
this change cannot affect the bars; follow-up should inspect sprite CLAMP
REGION_CLAMP/REGION_REPEAT or alpha test and their original GS registers.

## Validation needed
Run the exact same scene with `hud_top_overlay=1` and then 0; use
`report=1`, `gs_depth=2`. Compare green life/blue ki fill, white
HUD borders/portraits and pause menu. Compare only the visual HUD, not
life fraction after different damage events. The new code has mathematical
classifier tests and PSP build CI; a PPSSPP visual test is required.

**Remaining limitations**: background still black and some scene polygons
are absent; framebuffer composition is an independent issue. No FPS
improvement is implied by this HUD-specific depth change.
