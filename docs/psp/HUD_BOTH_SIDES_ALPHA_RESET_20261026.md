# Missing second-side HUD bar: global GS alpha reset was skipped

## Reproduction and source evidence
After PR #34 the screen-space life gauge improved, but on the other side
its backing/ki still had missing layers. Crucially, original
`src/battle/hud_a.c:Hud_Draw` invokes `HudGfx_ClearAlpha()` **twice**
around the two players' `HudGauge_SelectSide(0)` and
`HudGauge_SelectSide(1)` sequences, even on the same rendered frame.

`HudGfx_ClearAlpha` (`src/battle/hud_c_c.c`) is **not** a small
TRIANGLE_STRIP mask. It clears the alpha byte across all 512x448 pixels
as **sixteen full-height 32x448 GS SPRITES**:
- PRIM=0x46 (untextured, blended)
- RGBAQ=(0,0,0,0), ALPHA=0x44, TEST=0x30000
- Dest RGB unchanged, dest alpha reset to zero
- Executed once before player 1 HUD and again before player 2 HUD.

PR #34 added a stencil-backed mask for top-HUD `type=3` triangles
only. It did **not** translate the sixteen `type=6` sprites into
stencil resets. The first gauge's stencil therefore survived into
the second gauge, potentially rejecting its white backing / fill
when TEST.DATE was enabled.

## Surgical fix in PR #35
Detect only the sixteen specifically patterned untextured full-screen
alpha-clear strips in `GsGpu_Draw(type=6)` and translate each to
`PSP_HUD_MASK_CLEAR`: GU pixel mask `0x00FFFFFF` leaves RGB
unchanged while stencil replace 0 resets alpha. Leave regular
2D sprites, character triangles, text/CLUT, animations and stage
geometry untouched. `hud_mask_fix=0` still reverts both PR #34
and this branch's behavior for A/B tests.

The original HUD still clears its local bar silhouette via
`HudSprite_DrawAlphaClear`, previously implemented by PR #34.
The new full-screen clear is a separate operation, not a replacement.

## Validation
Host unit tests recognize all 16 strips, tolerate 12.4 fixed-point
rounding, and reject ordinary sprites/nonzero color/wrong TEST and
nonaligned rectangles. Both PSP CI workflows compile and test.
Runtime log now includes `full-frame-alpha-clear-strips` and
`sweeps`; for normal fighting these should be roughly 32 strips and
2 completed sweeps per frame (not necessarily exactly in timed 2-second
logging windows). If they stay zero, the matching logic is not
reached. If both sides still fail, inspect source-alpha and stencil
interaction and framebuffer 5551 limitations; don't claim visual
fix from CI alone.

No changes to `tex_limit=256`, native VU1, HP logic, fighter models or
texture resources. The scene/background work stays separate.
