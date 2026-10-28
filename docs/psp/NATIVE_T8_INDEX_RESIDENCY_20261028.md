# PSP-native indexed texture residency — experimental T8 phase 2

Baseline: PR #8 `TP3-NATIVE-ASSET-GU-VU1-20261028`.
Build ID: `TP3-NATIVE-T8-INDEX-REUSE-20261028`.

## Why this is needed
Logs from real BT3 PSP fights showed thousands of RAM texture cache
evictions and hundreds of milliseconds spent decoding GS textures
every three game frames. The PS2 streams PSMT8 texture images and
updates CLUT independently; the existing PSP texture cache uses a
COMBINED key of source GS memory page hashes and palette (CLUT) hash.
That is correct, but a palette-only change can force the same costly
PSMT8 swizzle/index decode again, even though GPU indices themselves
have not changed.

## Implemented
- Preserve a single bounded **256 KiB linear PSMT8 source index
  cache**, indexed by original TBP, TBW, full source dimensions and
  an FNV-derived chain of content-based `Gs_PageHash` values for
  every source page. Record the source-only hash *before* combining
  the CLUT hash for normal decoded-texture cache identity.
- On a new PSP texture cache miss, reuse the previously decoded
  linear indices only if ALL source metadata and the original
  source-only hash match. The palette is still read fresh with the
  existing PSP CLUT code, preserving animated palettes.
- On a source miss, continue the opt-in offline native T8 pack or the
  original GS swizzle reader, check 64 sample points against the
  scalar GS reference and only then save the linear indices.
  Never enable direct framebuffer-to-index aliases, and never treat
  old GS RAM as a full GPU image readback.
- A change to original GS source memory invalidates the index cache,
  even if TBP and CLUT remain unchanged. Neither white-sprite
  variants nor 4-bit / upper-byte GS formats are cached here.
- New periodic log line: `native T8 source index reuse: hits=...
  misses=... stored=...`; zero hits means this scene did not benefit.
  Source hashing was already performed by the original texture cache.
- Host regression test in both PSP workflows validates all cache
  invalidation properties.

## What this does NOT solve
- Remaining **stage399** instructions still use the existing VU1
  interpreter when native clipping is not equivalent. The first
  native predecode and identical-MPG bypass from PR #7/#8 remain.
  Full native VU1 replacement is not implemented.
- Full PS2 GS page feedback, depth buffer as texture, PSMCT32
  framebuffer reconstruction, true CLUT writes by GPU, and
  order-sensitive alpha-mask effects are not implemented.
- Offline mesh and animation formats from PR #8 are not yet attached
  to matching runtime game asset IDs.
- PSP CI success proves compilation, not graphical correctness or
  any real FPS increase. Must test same battle in PPSSPP.

## Verification
Check `psp: native T8 source index reuse` after running the
same battle and compare per-interval texture decode time and
FPS. If hits are zero, this optimization does not solve the
user's current texture bottleneck; do not claim an improvement.
