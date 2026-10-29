# Tenkaichi Portable 3 — PSMT8 native asset preparation (2026-10-29)

This is the **first implemented conversion stage**, not a claim that the full
BT3 scene, meshes, animations, or effects were converted. The game and
original legally obtained disc files remain external to the repository.

## New converter and input format

`port/tools/psp_t8_convert.py` is an offline, Python-stdlib-only converter.
It turns an owner's **exact 4 MiB PS2 GS memory snapshot** into an
uncompressed T8P1 native PSP PSMT8 index pack. This is a texture INDEX-plane
conversion, not a full game-data extractor and not a PNG/RGBA converter.

The converter reproduces the actual GS 256-byte block/page and 16x16 PSMT8
column mapping from `port/src/gs/gs_core.c`, as well as `Gs_PageHash` and
`texture_get()`'s content-generation key. It uses the same verified
32-byte little-endian header, checksum, file naming and size constraints as
`psp_native_t8_try_read`. The PSP then reads this prepared input instead of
re-decoding the PS2 GS PSMT8 tiles on a valid cache miss. Color palettes
(CLUT) are still read live from GS memory so palette animation, alpha and
the existing fe93677 white-font fix are preserved.

Example on a computer with an **actual user-produced GS memory snapshot**
and TEX0 metadata:

```sh
python3 port/tools/psp_t8_convert.py --vram my-own-gs-vram.bin \
  --tbp 336 --tbw 8 --width 256 --height 256 --out-root native
```

If an index texture has *already* been decoded to width×height raw bytes
with a known 8-digit GS source generation hash, it can be packaged without
a 4 MiB dump:

```sh
python3 port/tools/psp_t8_convert.py --indices my-index-plane.bin \
  --tbp 336 --tbw 8 --gen a1b2c3d4 --width 128 --height 128 \
  --out-root native
```

This does not invent a raw GS memory snapshot. If the user only has a disc
ISO, these commands cannot automatically determine live framebuffer
content, GS TEX0 TBP/TBW, or the runtime page hash. Extraction from the
disc's original model/texture assets is a separate development phase.

## Mobile-only workflow: local first-run asset preparation

Because not everyone has a desktop or a GS memory dumper, the EBOOT can
optionally create the same validated packs from **textures decoded from
the user's own game**:

1. Before launching PPSSPP, create
   `PSP/GAME/BT3/native/` in the game's virtual Memory Stick folder.
2. Create two zero-length files in that folder:
   `enable.txt` (allow pack **loading**) and
   `create.txt` (allow **local pack creation**).
   Neither is `bt3.txt`, and there is no external gameplay configuration.
3. Start the game and visit a scene. The EBOOT writes up to **8 distinct
   PSMT8 index packs per process run** from GS data that was already
   verified against the reference decoder. Creating packs can cause
   one-time filesystem stalls; the cap limits this.
4. Close the game, delete `create.txt`, **keep `enable.txt`**, then
   restart. The PSP reads matching packs. The live GS source checksum
   and a 64-sample reference comparison reject wrong or stale data.
5. If any visual regression occurs, remove `enable.txt`; the original
   GS decoding path is preserved and runs as before. Do NOT delete the
   working `EBOOT.PBP` or your game files.

If the native folder/markers do not exist, normal players suffer no
filesystem writes or per-texture pack reads.

## Correctness and limitations

- Only **PSMT8 / TEX0.PSM=0x13** indices are preconverted in this stage.
  PSMT8H, 4-bit formats, RGB, character meshes, bones, animation and
  screen effects are not converted.
- Packs are content-addressed by the actual PS2 GS page hashes, not by
  ambiguous filenames, and include TBP/TBW, dimensions and FNV checksum.
  Never export game files into GitHub.
- A cache-hit still needs exact GS content-generation checking; this is a
  correctness check, not needless pollution. Native texture already uses
  GU_PSM_T8 and the correct PSP CLUT. This optimization avoids expensive
  GS index-plane decoding on cold misses, **not** every later conversion.
- Packs stored by the EBOOT intentionally hold **linear index data**:
  texture LOD/subsampling and PSP GU swizzling occur using the same tested
  code as without packs. This does not change geometry or UI.
- Actual speed gains are unmeasured; local storage may be slower than a hot
  RAM cache, especially during capture. This does not guarantee Tag Team
  compatibility, visual fidelity or 30/60 FPS.
- CI runs Python conversion/deterministic swizzle/hash tests as well as
  the existing `test_native_t8_pack.c` header checks, then builds EBOOT.

## Next work (not falsely counted as complete)

1. Understand original archive texture/model formats and create legal
   extraction+conversion stages for offline **RGB/CLUT4, scene meshes and
   animations** from an owner's original assets.
2. Version the resulting native resource manifests; load them directly
   in the EBOOT without repeated GS texture uploads where equivalent.
3. Eliminate proven-unused runtime GS/VU1 fallback code only after
   visual/animation parity and gameplay testing on PPSSPP and PSP hardware.
4. Profile stage399 and CPU GS mirror; removing those bottlenecks is
   necessary before claiming Tag Team-like performance.
