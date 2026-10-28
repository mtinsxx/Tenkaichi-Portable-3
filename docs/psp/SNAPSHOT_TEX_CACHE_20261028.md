# PSP GS snapshot lifetime + texture-cache hot-key repair (experimental)

**Baseline:** PR #5 `TP3-T8-DIFF-FB-READONLY-20261028`, which ran a battle.
This PR **does not** bring back PR #3's unsafe screen ownership/GE restore.
Main remains unchanged.

## New user log observations
- PSMT8 sampled differential decoder showed **0** mismatches over thousands of
  checked new textures; therefore arbitrarily changing PSMT8 swizzle is not justified.
- During a representative last ~3-second battle interval the renderer captured
  20 completed GE framebuffer images, deferred 293 captures and skipped 168
  because all three snapshot allocations were occupied.
- 20,995 cumulative RAM texture-cache evictions and repeated decodes were
  recorded. Every cache eviction previously incremented `sTexSerial`,
  invalidating **all** direct-mapped hot TEX0 keys despite the vast majority
  of descriptors surviving.

## Code changes
1. Retain **exactly** 3 x 512x512xRGB5551 snapshot allocations. If all are
   allocated and a new logical GS FBP needs to preserve completed pixels,
   reuse only an OLD pool allocation whose content is no longer a valid
   version of the same GS page or whose target has been retired. Never
   reuse a pool allocation sampled in the current frame, or enqueued for
   GE copy in the current frame; respect completion via FrameEnd sync.
   Move the raw + aligned pointers together, and reset only the donor's
   snapshot validity fields (preserve its draw/write counter).
   Wipe and flush the padding before reusing as a different GE texture.
2. When removing a RAM texture from `sTex`, invalidate only the one
   matching hot key and decrement cached indices that are shifted by
   `memmove`. Preserve the global serial for unaffected descriptors;
   TEX0/TEXA keys, GS upload epoch and page-generation checks remain.
3. Add counters `recycled-stale` and `erase-index-repairs` to existing
   periodic logs. Both PSP workflows include strict host regression tests.
4. Unique EBOOT build ID: `TP3-SNAPSHOT-TEX-CACHE-20261028`.

## Not a complete GS translation
This does not implement exact PS2 PSMCT32 / indexed framebuffer readback,
depth-as-texture or destination alpha effects. A snapshot budget refusal is
correct if all images are genuinely valid. The policy does not force the
experimental stage-to-fighter copy when the source has lost WORK ownership.
It targets the genuine, repeated memory churn, not a particular screenshot
texture guessed to be faulty.

## PPSSPP validation needed
Compare on the *same* battle screen: visual blocks, crash/no crash, game FPS.
Check `psp: GS framebuffer virtualization` for `recycled-stale` vs
`budget-skipped`, and `psp: texture guarded reuse` for
`erase-index-repairs`, full keys and cache decode time. A green PSP SDK
build is not proof of correct graphics or smooth gameplay.
