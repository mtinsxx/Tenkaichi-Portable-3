# Tenkaichi Portable 3: PSP native PSMT8 asset conversion

## Scope of this test build

**Implemented:** a genuine, byte-preserving converter for PS2 GS PSMT8
indexed source pixels to reusable, versioned `T8P1` packs. The PSP can
capture the indices of real textures while the player's own copy of the
game is running and reuse them next launch without running the PSMT8
GS address-deswizzle again. Each pack keeps a live GS CLUT: changing
characters, animated palettes and the white-font special case do not
become stale/baked. Normal GU_T8 allocation and packing/swizzle are
still done on cache misses; this is NOT a preconverted final GPU vertex
or fully-swizzled texture bundle.

**Not implemented:** conversion of all meshes, character animation,
shadow/effects, music, movie data, PSMT4 textures, original disc AFS
entries, or the rest of the PS2 GS render state. No game archives,
extracted proprietary assets, firmware or ISOs are committed.

## Android / PPSSPP: capture your own game textures, no PC necessary

1. Make a backup of the existing `EBOOT.PBP` and install the EBOOT with
   build ID `TP3-NATIVE-T8-CONVERTER-20261029`.
2. Create the folder `PSP/GAME/BT3/native/` alongside the EBOOT.
   Inside that folder, create **two empty files**:
   `enable.txt` and `capture.txt`.
3. Run the same battle. On the first run, the PSP writes up to 16
   distinct PSMT8 source planes in files such as
   `t8_0150_a1b2c3d4_64x32.tp8`. Pack filenames include the GS
   framebuffer texture base, **source-only** page generation hash, and
   dimensions. Header also validates the source texture buffer width
   (TBW) and checks all bytes with FNV32. If storage access fails,
   loading the game falls back to the original GS decoder.
4. **Before any performance comparison, remove `capture.txt`** but
   keep `enable.txt` and the generated `.tp8` files. A cold capture
   deliberately uses Memory Stick writes and may REDUCE FPS. In later
   runs, a matching pack skips the expensive GS PSMT8 decode; existing
   GU texture caching, palette refresh and memory mirror still work.
5. Inspect `log.txt` for `native T8 packs: loaded=...` and
   `captured=...`. A zero load count means no pack matched and this
   change should NOT be credited with a performance improvement.
   A green GitHub build is not a measured improvement.

These files do **not** replace the player's `disc/` contents. There is
no external `bt3.txt` setting.

### Limits and safeguards

- A matching pack is accepted only when its magic/version, TBP, TBW,
  **source-only GS page hash**, size, checksum and 64 sampled pixels
  match the GS reference decoder. A mismatch falls back to the real
  GS memory and never silently substitutes wrong colored pixels.
- Palette changes do not invalidate the **index** pack; the live
  CSM1 CLUT still comes from the emulated GS. PSMT8H, PSMT4,
  high-byte and white-font baked textures keep their old paths.
- Capture never overwrites existing packs and is limited to **16 new
  textures per launch** to bound storage writes. The first observation
  of a dynamic framebuffer texture may be valid only for one GS state,
  so do not assume every generated pack will be reusable later.
- This implementation does not guarantee 30 FPS, 60 FPS or the
  performance of Tenkaichi Tag Team. The VU1/GS mirror and distorted
  scene geometry remain independent bottlenecks.

## Offline converter (Termux/PC with Python 3)

A developer with a linear, row-major PSMT8 index plane extracted from
their *own* PS2 resources or a diagnostic export can generate the exact
pack required by the runtime:

```sh
python3 port/psp/tools/convert_t8.py pack \
  --indices my_indices.raw --tbp 336 --tbw 8 \
  --index-gen 0xa1b2c3d4 --width 64 --height 32 \
  --out native/
python3 port/psp/tools/convert_t8.py verify \
  native/t8_0150_a1b2c3d4_64x32.tp8
```

The pack contains a 32-byte little-endian `T8P1` header and exactly
`width * height` indices. It is rejected if any byte is missing,
modified, extra or mismatched. This tool **does not** parse ISO/AFS or
invent the runtime source hash: for correct playback, metadata and
index bytes must correspond to the same actual GS memory content.
The PSP capture path is the practical path without a PC.

## Development and release gate

Run `python3 port/psp/test_convert_t8.py`, the existing C
`test_native_t8_pack.c` and both PSP GitHub workflows. Compile with
`python3 port/psp/build.py`. Compare the same scene before and after,
with capture disabled in the benchmark run. Preserve the existing
4 MiB GS feedback mirror that repaired the rainbow artifacts and
the `fe93677` color/font correction.

Full conversion still requires archive-specific resource parsing,
geometry validation, animation/skinning equivalence and direct GU
playback before deleting older GS/VU1 fallbacks. Do not delete
third-party license notices, Vita reference implementations or
regression tests as mere “pollution.”
