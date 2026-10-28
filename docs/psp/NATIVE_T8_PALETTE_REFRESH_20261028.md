# PSP native PSMT8 palette-only refresh (experimental)

Base: PR #10, `TP3-PSP-T8-4WAY-20261028`.
Build ID: `TP3-PSP-T8-CLUT-REFRESH-20261028`.

## Observed problem
The current PSP renderer implements two correct layers of caching for
TEX0 PSMT8 indices, including a four-entry content-based 256 KiB CPU
index cache. However the native GU texture cache is keyed by the
*combined* GS source and CLUT hash. A CLUT-only update creates a new
GU texture entry, with an allocation/swizzle, even though all indexed
pixels in the original texture and its PSP GU swizzled allocation are
identical. This is particularly expensive under the reported battle
memory pressure and repeated cache evictions.

## Implemented
- Record original GS PSMT8 `sourceGen` before adding the live palette
  hash to `Tex.gen`. Only valid GU_PSM_T8 resources carry this extra
  source key.
- On a normal cache miss, search existing texture descriptors for an
  identical effective TEX0/TEXA, source hash, width and height,
  and a distinct *combined* CLUT hash. Require `last < sFrame`,
  which is safe because `GsGpu_FrameEnd` completes/synchronizes
  GE work before incrementing the frame counter.
- If all guards pass, update only the existing 256-entry RGBA8888
  native PSP palette with current PS2 GS CLUT + TEXA conversion.
  Write back PSP data cache for main-RAM resources, invalidate the
  GU texture cache, increment the CLUT ticket and re-register the
  existing direct hot texture key. No new texture allocation or index
  swizzle, and no changes to render primitive order.
- If an otherwise eligible source was drawn in the current frame,
  refuse to update it in place and execute the original full-cache
  miss path, because the GE may still be reading its previous CLUT.
- Keep live PSP-native T8 index-cache and offline asset paths as
  previously implemented. The texture must originate from the same
  source GS pages: never apply this to PSMT4, PSMT8H, GPU framebuffer
  reinterpreted as indexed, or white-font baked RGBA textures.
- Add host regression tests and a new log line:
  `psp: native T8 palette-only refresh: reused=... in-flight-blocked=...`

## Important limits
This is not a full native mesh/animation port. It does not emulate
GS destination alpha precisely, read back arbitrary PSMCT32 or ZBUF
buffers, eliminate framebuffer corruption, or natively compile
`stage399`. A green PSPSDK build proves compilation only. Compare
the same fight and scene visually and measure FPS before calling it
an improvement.
