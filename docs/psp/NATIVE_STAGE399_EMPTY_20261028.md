# Native PSP VU1 stage399 — bounded geometry-free strips

This experimental EBOOT extends PR #11 without modifying `main`.
Unique build ID: `TP3-STAGE399-NATIVE-EMPTY-20261028`.

## What changed
- An exact PS2 GS `TRISTRIP` command (`PRIM.PRIM == 4`) with
  0, 1 or 2 submitted vertices cannot draw a triangle.
- The stage399 HLE program now checks the native strip tag, the
  `TOP` address and complete VU memory bounds before taking this path.
- For accepted batches, replay the A+D GIF texture register and PRIM
  writes in the **original order**. The existing `GsGpu_DrawVu4`
  produces no triangles, so it is deliberately not called.
- Bypasses a costly fallback to the 399-instruction interpreter **only
  when no triangles can possibly be drawn**, retaining the game's
  render state updates and the original call/kick count.
- Non-TRISTRIP, malformed or ambiguous tags, 3+ vertices, or the
  disabled native-stage flag fall back as before.
- New log: `psp: native stage399 degenerate strips: bypassed=N`.
  **N=0 means no workload was eligible and no performance gain is
  expected from this particular change.**
- Two PSP workflows include a test covering primitive types, counts,
  and bounds at the end of VU memory.

## Unfinished — DO NOT describe this as a completed PSP-native port
The stage399 fallback for **visible** polygons still executes the
original VU interpreter when native projection, clipping or PS2 GS
state equivalence cannot be established. Other VU1 programs, model
skinning, GPU-produced framebuffer feedback, PSMCT32 readback,
CLUT/FBMSK edge cases, depth-as-texture and faithful native effects
remain pending. A full PSP-native content pipeline requires correctly
identifying the original assets and comparing transformed data in
real battle traces; such assets are not present in this repository.

## Validation
PSPSDK and C host tests can confirm compilation and basic packet
validation. They cannot confirm visual correctness, FPS or that a
battle uses any empty strips. Test the same fight on PPSSPP and read
`BUILD-ID`, `native stage399 degenerate strips`, `PROFILE VU1`,
`frame cadence` and `native T8 palette-only refresh`. Preserve the
previous EBOOT and rollback if graphics regress or crashes appear.
