# Stage + fighter into the same presented PSP framebuffer

**Experiment**. Video observation: in PPSSPP's fast-forward, frames sometimes show
only scenery and others fighters/HUD on black. This is consistent with the
existing logical GS framebuffer routing into different PSP physical surfaces,
but a video alone cannot prove which FBP is responsible. This change is
therefore deliberately limited to native VU1 battle paths and kept off main.

## How
1. Track the physical GE framebuffer which actually received a **successful
   native stage triangle**, the logical GS FBP, its upload generation,
   framebuffer write version and PSP frame number.
2. At the beginning of a native fighter strip, check the proposed fighter
   render target is a PSP screen, the stage came from the current/previous
   PSP frame, its source is still the exact same buffer/version, and a
   WORK source has not changed owner.
3. If distinct, flush queued stage triangles and enqueue a 480×272 RGB5551
   `sceGuCopyImage` from the stage backing into the fighter screen backing.
   *Then* submit model geometry. No CPU readback, extra buffer allocation,
   rerouting of PS2 logical FBPs or forced global framebuffer aliasing.
4. Only one copy per destination per frame. Count successful/rejected
   transfers in periodic PSP logs.

## Limitations
- No copy if the native stage path was not accepted (e.g. VU1 interpreter
  fallback), if the stage image is stale, or if fighter rendering uses a
  non-screen target. No claim that every missing scenery case is fixed.
- A composited RGB5551 stage does not reproduce a full GS 32-bit
  destination-alpha/FBMSK effect and can be overwritten by later GS
  draw/clear operations; PPSSPP runtime validation is required.
- Gameplay and camera state, frame pacing, framebuffer snapshots and
  original paired-screen routing are retained.

To test, compile PR branch through GitHub Actions and compare the same
stage without/with PPSSPP fast-forward using `log.txt`:
`psp: stage-to-fighter background copy: staged=... copied=... guarded-skip=...`
If copied stays at zero, the path is not exercised by that scene and
another approach is needed. Do **not** merge until the PPSSPP image is
verified; no `bt3.txt` is required.
