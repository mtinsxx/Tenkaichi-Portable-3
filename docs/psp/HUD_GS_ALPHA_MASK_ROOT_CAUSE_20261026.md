# Root cause: BT3 life gauge requires GS framebuffer alpha masks

## Source-level evidence, not just screenshot
`src/battle/hud_c_c.c:HudSprite_DrawAt` draws an HP/ki HUD texture as
PRIM=0x154, a four-vertex **triangle strip**, which `gs_core.c:vertex`
sends to the PSP backend as two type-3 triangles. Thus PR #33's
`topHudSprite` detection for only type 6 never reaches the health bar.

The original PS2 health-bar implementation in `hud_b.c:HudGauge_DrawHp`
and `hud_a_d.c:HudGauge_GsBeginMask` repeats:
- alpha-clear a rectangle, preserving its RGB pixels
- `FRAME_2.FBMSK=0x00FFFFFF` (do not write RGB);
  `FBA_2=1` (set destination alpha high bit on passing fragments)
- `TEST_1.DATE=1,DATM=0` (draw colour only where destination alpha is 0)
- return to ordinary framebuffer state.

The old PSP `GsGpu_Draw` honored **none** of the FRAME.FBMSK/FBA/DATE
operations: `state_apply` did not call `sceGuPixelMask` or any
`sceGuStencil*` functions. It consequently drew alpha-mask rectangles
into RGB, rather than clipping colour layers to the intended silhouette.
This is the verified missing GPU feature relevant to the bars.
The underlying game calculates the HP fraction itself: hp % 10000,
width = fraction * 160 / 10000. Do NOT force any life to full.

## Patch
Only on triangles covering a conservative screen-top HUD region:
- transparent black untextured alpha-clear strip -> preserve RGB and set
  PSP stencil bit 0 in the covered region;
- GS FRAME RGB-only mask + FBA_2 -> preserve RGB and replace stencil bit
  1 on fragments surviving GS source alpha test;
- GS TEST.DATE -> compare PSP destination stencil to requested 0/1,
  preserving the stencil while drawing normal RGB.
This uses the alpha bit of the existing PSP GU_PSM_5551 framebuffer;
no 32-bit display mode and no changes to character/stage textures or VU1.
Restore pixel mask and stencil state on every switch and every new frame.
`hud_mask_fix=0` fully disables the change.

New log: `GS HUD alpha mask: clear/write-1/DATE-alpha0/DATE-alpha1`.
Test these counters during the same battle with `report=1`. If all zero,
the new HUD classification missed the actual GS draws and must be
investigated, not claimed fixed.

## Important limitations
The PSP's 1-bit alpha/stencil cannot preserve the full 8-bit PS2 FB alpha
mask. This is suitable for **binary** gauge masks; partially transparent
soft edges may require a separate 8-bit alpha texture in main RAM.
Other areas with complex FBA/FBMSK are still unsupported. This CI test
checks GS state classification and EBOOT compilation, **not** visual
correctness or health values.
