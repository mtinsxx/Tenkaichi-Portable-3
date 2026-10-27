# PSP native VU1 — fighter models: animation, bone skinning and lighting

## Source basis and scope

This work is a *partial, testable translation*, not a claim that the full PS2
VU1 was removed. It is based on the repository's
`src/vu1/prog0.vsm`, `src/vu1/prog1.vsm`,
`docs/systems/vu1/prog0_prog1.md` and
`port/src/gs/shaders/vu0.vert`.
Branch: `port/psp-native-fighter-vu1-20261025`, derived from the
experimental PR #17; main `fe93677` remains unchanged.

## Implemented in native Allegrex C

For batches accepted by strict geometry checks:
- Program **0** (127 VU1 instructions): interpolate transformed positions
  between bone A and bone B, including their pivots and per-vertex weight.
- Compute `screen * (skinned_position,1)`, `clip * (...) `,
  ST/Q, integer depth, vertex colour.
- Render layer 0 with original model UV/material colour and layer 1
  with normal-based toon/ramp UV and colour from VU constant 23.
- Cel/ramp lighting combines normal transforms by the bone rotation
  matrices with direction/intensity from VU constants 10–13,
  blending them with the original bone weight.
- Program **1** (124 VU1 instructions): use the camera-aligned normal
  basis (constant 10..13) to compute both UV components of the fade
  texture. Keep the original first-vertex fade marker, GS TEX0
  selection and shader-layer duplication rules.
- The original GS primitive setup and texture cache are used by
  `GsGpu_Draw(3, ctx, ...)` — *not* empty VU draw calls.
- Output is sent using static storage of max **38 strip vertices**,
  with no allocations per batch. Original VU0 helpers, IOP/SPU and
  the gameplay's timing and animation states are untouched.

## Strict fallback

`Psp_FighterPrepare` inspects the **entire** strip before any layer
updates GS TEX0 or PRIM. Rejects / runs original VU1 microcode when:

- A vertex weight is outside [0,1], position/normal/UV is not finite,
  or the model/lighting/clip matrix produces NaN/Inf.
- Screen W or clip W is non-positive; any vertex needs the VU1
  clipper; computed Z is invalid.
- The perspective `1/W` changes by more than 0.5% across the
  strip, because the PSP GU transform-2D interpolates UVs affinely.
- Vertex count is outside 3..38 (the PS2 output GIF packet bound).
- Input buffer pointers or rendering state are unavailable.

**Fallback detail:** for rejected program-0 or program-1 batches, code
reconstructs original setup from MSCALF 0, then resumes at
the interpreter's appropriate batch entry (36 or 15), matching the
existing HLE/interpret fallback strategy. No partially submitted
geometry is allowed.

`native_fighter=1` is enabled in the experimental EBOOT without
requiring `bt3.txt`. `native_fighter=0` is an optional troubleshooting
setting if you use `bt3.txt`. The previously compiled EBOOT from
PR #17 is the primary safe comparison.

The **programs 2, 6, 7, 8** (shadow/silhouette, special effects
and other geometry), complicated mixed-perspective triangles and
clip fans still require the original interpreter.

## Verification

`port/psp/test_fighter_vu1.c` checks independent two-bone matrix
reference at several animation weights, different bone translations
and pivots, cel/ramp lighting, fade UVs, screen perspective and
fallback of invalid animated vertices. CI also continues to run
the earlier VIF differential tests, GS PSMCT16 tests, architecture
safety tests and PSP cross-compiler.

**Limits of automated verification:** test math is not a complete
VU1 cycle/instruction-level replay. Floating-point round-off,
perspective, GS ALPHA.FIX, clipping and GPU interpolation can still
make visual differences. Tests do not establish runtime FPS gains.

## PPSSPP Android test protocol (without bt3.txt)

1. Keep the old working EBOOT and the PR #17 experimental EBOOT.
2. Download the new `BT3-PSP-EBOOT` artifact. Keep Memory Stick active.
3. Use the same Goku/Vegeta battle and stage for A/B screenshots or
   a short recording: model visibility, bone joints (elbows/knees),
   camera movement, shading/toon outlines, colour ramps,
   transformation and Ki attacks.
4. From `PSP/GAME/BT3/log.txt`, compare:
   `psp: native fighter VU1: batches=... vertices=... rejected=...`
   and the `VIF/VU1` estimated interpreter milliseconds.
5. If `batches=0`, no fighter batch took the native route in that
   session, so **do not claim an FPS gain** attributable to it.
6. If the fighter is distorted or disappears, restore the old
   EBOOT and submit the screenshot and log. Never merge experimental
   code into main based solely on a green PSP build.

## Still needed for a complete VU1/GS port

- Validate shadow/silhouette programs 2a/2b and program 6, and
  other original VU microprograms with disassembly-level equivalence.
- Reproduce per-triangle clipping and perspective-correct texture
  interpolation beyond the conservative fast path.
- Implement GS multi-target framebuffer preservation and
  depth-as-texture/readback; the grey-screen bug is not yet fixed.
- Enforce reliable r2l semantics for PS2 C expression evaluation.
- Test prolonged gameplay and memory behavior on actual 64 MB PSP.

No game ISO, ELF, copyrighted assets or microprogram binary has been
added by this branch.
