# PSP GS framebuffer coherence – experimental recovery, 2026-10-28

## User-observed issue
In PPSSPP fast-forward, stage backgrounds flicker into view independently of
the fighters/HUD, while normal speed shows fighters on black. The previous
native-only stage→fighter copy does not fix the battle. This PR addresses
lifetime management rather than guessing a stage FBP, and does not depend on
the native VU1 acceptance path.

## Actual bugs addressed in `port/psp/psp_gu.c`
1. `target_get(fbp, create=0)` was mutating `Target.color` on a **TEX0
   framebuffer lookup** whenever `sceGuSwapBuffers` changed the desired
   render backing. Such a lookup is a read, not a new render; rebind could
   invalidate an otherwise valid preceding display image.
2. When the desired physical backing *did* change on a real GS write,
   `target_get` attempted to snapshot the old backing, then marked it
   unrendered. It **never restored** the saved RGB5551 pixels into the
   new drawbuffer, although the GS retains contents between partial draws
   and screen swaps.
3. The WORK backing tracked its logical FBP owner, but FB0 and FB1 did not.
   Reusing screen VRAM for another logical GS FBP could silently erase a
   previous logical image and still leave its `rendered` flag set.

## Changes
- Read-only TEX0 lookup preserves the previous physical source backing.
- Maintain owners for both physical screen FBs, snapshot GE pixels
  *before* another FBP can overwrite a shared screen, and invalidate
  replaced screen aliases.
- When a GS FBP must resume on a different screen backing, restore its
  valid source-generation and write-version-matched 480×272 RGB5551
  snapshot to the new drawbuffer *before* its next primitive (GE copy,
  no CPU framebuffer roundtrip, no added image allocation).
- Never restore stale, different-generation snapshots. WORK ownership
  and its existing pair/snapshot policy remain separate.
- Probe **both** screen buffers as well as WORK0/WORK1 in a bounded
  diagnostic sample: the renderer will report where the nonblack pixels
  live after `sceGuSync`, even when the presentation is black.
- Reduce intensive full-buffer GPU-probe frequency (first six,
  thereafter every 60 PSP frames).
- Host tests for lookup, ownership and eligibility, and a PSP Actions
  EBOOT build.

## Not fixed yet
- Full pixel-exact GS PSMCT32/24 readback, Z24/32 aliasing, alpha
  destination feedback and swizzle reinterpretation. A 5551 snapshot
  must not be misrepresented as byte-exact 4 MiB GS local memory.
- Work-FBP recovery is unchanged.
- Software VU1 program4 remains expensive; this patch is graphics
  correctness and does not claim new real PPSSPP FPS.
- A host unit test or successful PSPSDK build is **not** a PPSSPP battle
  image/fps validation.

## Needed observation after EBOOT test
Obtain a new log from the test branch and search:
`psp: GS screen coherence: source-lookup-preserved=...`
`screen-owner-changes=...`
`snapshot-restored=...`
`restore-unavailable=...`
`psp: LOG_ALL PIXELS post-GE ... screen=... other-screen=...`

A displayed-black frame with nonzero `other-screen` pixels will
confirm a presentation-target mismatch. A black physical screen with
nonzero stage input means a rasterization/depth/alpha or GS upload path
is the next target. This is a **diagnostic discriminator**, not
proof all rendering issues are already resolved.
