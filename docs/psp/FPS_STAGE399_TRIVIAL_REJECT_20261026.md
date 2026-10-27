# FPS root cause: repeated PS2 VU1 program 399 interpreter on outside-camera stage strips

## Measurement
User's EBOOT from Oct 26 19:36, report=1, battle intervals around
the end of log(20261026-194546).txt:
- **3 GS frames / ~2.18 seconds = 1.3 real GS FPS**.
- **GSchain ~= 1967 ms / 3 frames**, with GUfinishSync ~= 0.
- **Program 399**: 1800 interpreter invocations, 883,707 instructions,
  ~1102 ms sampled estimate (biased timing; not additive to GSchain).
- **Fighter programs 127/124**: 0 interpreter calls; native fighter
  batches 2,766 with 0 rejected.
- **Stage native program 4**: 1,391 accepted batches, 900 rejected.
- ~73,789 GS draw calls in same window, ~680 ms sampled GS time.
- Texture lookup ~135 ms, decode ~106 ms; substantial but below the
  stage microprogram estimate.

This proves the largest *measured* CPU cost is stage VU1 program 399,
not fighter model skinning. It does NOT prove that every rejected strip
is offscreen or predict the speed improvement from a safe cull.

## Root cause from code
`Psp_Stage4Prepare` rejected entire stage strips when it found one
invalid projected vertex, a behind-eye W, or unsupported preflight.
`GsVu1_Call` then executes the PS2 VU1 399-instruction program
(`run(0)`, then `run(21)`) on the CPU. This path is called hundreds
of times per rendered stage frame.

## Patch
For a rejected stage strip **only**, run a mathematical view-frustum
test on homogeneous clip coordinates from the same original PS2
matrix (PSP VU1 program-4 constants, matrix +16):
- For each three consecutive strip vertices, require all three to
  violate the SAME clip plane strictly beyond a numerical margin.
- Crucial compatibility with the actual PS2 interpreter:
  VU1 `CLIP` compares the coordinates against **+/-ABS(W)**.
  Do not discard a triangle just because `W<0`; the game may project it
  differently in its own microcode. Test each vertex against |W|.
- If EVERY triangle satisfies one of the above, no triangle can
  generate pixels. Preserve the original GIF texture and PRIM register
  writes, but skip the original VU interpreter and skip GU draws.
- Otherwise call the original interpreter with no change.

This is NOT a draw-distance cut, lower polygon limit, shortcut for
visible geometry, or generic performance preset. Visible strips,
textures, player models, animations, effects, lighting, HUD and stage
UV calculation are untouched.

Host regression tests cover:
- strips wholly outside left plane and 4-vertex triangle strips
- mixed visible/outside strips
- negative-W geometry within the PS2 ABS(W) clip volume (must fall back)
- W crossing that might contain visible pixels (MUST fallback)
- NaN input / invalid counts
- triangles with vertices outside different planes but overlap view

## Validation
The port now logs
`psp: stage full-frustum trivial reject: strips=... vertices=...`.
If this is zero, the optimization does not affect the user's scene;
further work needs a per-reason rejection histogram and a new native
clip implementation, not claiming that FPS is fixed.
If nonzero, compare the SAME fight scene and current EBOOT against
`native_stage_cull=0`. Tests/CI alone do not prove an FPS gain.

Existing PR #35 HUD masks and PR #36 program-specific VU1 reporting
are preserved. Branch remains unmerged for user-side validation.
