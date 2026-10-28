# PSP PSMT8 correctness + read-only GS framebuffer lookups
## Context
PPSSPP battle screenshot of stage shows valid landscape geometry with
white regions and multi-colour indexed-looking horizontal blocks. The
user's Crash Guard log shows PSMT8 (PSM 0x13) reads from TBP 336,
large fallback counts for unsupported reinterpretation of an FB as an
indexed GS texture, 0 copies made by the old stage-to-fighter experiment,
and several thousand image-cache evictions in fight intervals.

An indexed-texture format **mismatch** is not proof that the fast PSMT8
block reader is broken. Nor should PSMT8 ever alias a 5551 GPU
framebuffer without conversion; the rejection is legitimate.

## Changes isolated from the crash-associated PR #3
1. A **TEX0 source lookup** `target_get(fbp,0)` no longer changes the
   source target's physical PSP draw backing or clears its rendered
   status when sceGuSwapBuffers changes. Only GS DRAW writes
   `target_get(fbp,1)` are allowed to rebind. The PR #3 GE screen
   restore + ownership changes have **not** been reapplied.
2. On **each newly decoded native PSMT8 image**, verify 64 locations
   including tile boundaries against scalar `Gs_VramRead`. If the
   block-wise decoded indices differ, reconstruct all indices via
   the scalar reference before uploading to PSP GU. This is bounded
   verification on cache misses only, not each primitive. This
   preserves existing swizzle and CLUT code for matching images.
3. A new detailed `psp: stage compositing rejects` log accounts for
   all reject causes: no screen destination, same physical backing
   (which requires **no** transfer), stale stage, WORK owner lost,
   source invalid, already copied, other. Never force unsafe
   framebuffer copies based solely on the old overall rejection count.
4. Regression tests compile in both PSP Actions workflows.

## What to check in PPSSPP
- Confirm no illegal-memory crash.
- Report `psp: PSMT8 differential decode: checked=... mismatched=...`.
  A nonzero mismatch triggers automatic scalar reconstruction;
  if zero, the *fast index decoder* was not the cause of the
  pixel corruption in that sample and the investigation proceeds
  to GS memory coherence, CLUT, TEX0 CSM/CSA and alpha.
- Report `psp: stage compositing rejects: ...`. If same-surface
  dominates, no cross-screen copy is needed; copying it could
  corrupt the scene.
- Compare screenshot and FPS in same stage.
- Do not call successful PSPSDK compilation proof of runtime
  graphics correctness.

## Unimplemented
This does NOT implement pixel-perfect PS2 32-bit GS framebuffer
readbacks, CLUT feedback from GPU writes, depth-as-texture or
the full VU1 performance rewrite. In particular a scalar reference
reading the stale *simulated* GS RAM cannot fix pixels that were
rendered only to PSP VRAM. Further differential capture is needed
if the stage blocks remain corrupt.
