# First native PSP asset-conversion pipeline — indexed BT3 textures

**Status:** PSP EBOOT integration + host converter implemented. This is **only
a real PSMT8 texture-conversion path**, NOT all models/textures/animation/effects.
Native Tag Team performance is not achieved or implied.

## How it works

BT3 uploads PS2 GS texture pages dynamically. A PSP GU indexed texture can
represent the original PSMT8 source index bytes efficiently; its palette
(CLUT) can be updated independently without rewriting the texel indexes.

Previously, the game always decodes the index bytes on a T8 cache miss and
loads any optional native `T8P1` file only when `native/enable.txt` existed,
but there was no integrated way to CREATE files that matched the PSP runtime's
GS page-content hash. This change implements both a user-side batch converter
and a first-run capture from the game's **real GS memory**, without including
copyrighted assets in source control.

A .tp8 file is 32 bytes of little-endian `T8P1` header (magic, version,
TBP, TBW, *source-only GS page hash*, width, height, FNV1a), followed by the
exact tightly packed linear PSMT8 index bytes. The runtime validates header,
content FNV and selected original GS source texels; a mismatch uses the
existing GS memory decoder. Indexes still use the existing PSP GU_T8 swizzle
and CLUT conversion so the cel/white-font fixes are unaffected. This DOES
NOT directly convert all PS2 model and texture file formats.

## Path A — no PC required (PPSSPP or PSP)

1. Keep your original disc-derived `disc/` files; make a **backup** of your
   previous working EBOOT and save files.
2. Create `PSP/GAME/BT3/native/` on your Memory Stick/Android PPSSPP
   memory-stick folder.
3. Create an empty `native/record.txt`. Play a test session **only to
   capture**, in the same stage you want to optimize. The EBOOT stores up
   to 256 PSMT8 sources, at most 64 KiB each, at most one NEW pack per frame.
   Recording introduces storage writes and can reduce FPS.
4. Exit PPSSPP/PSP. Remove `native/record.txt`, then create an empty
   `native/enable.txt`.
5. Run again with the SAME game data. The reader checks the current GS
   source-page hash, header, data checksum and reference texel samples;
   any missing/stale/malformed pack falls back to the GS decoder.
6. Check `log.txt` for `native PSMT8 converter` (captured/failed) and
   `offline native T8 packs` (loaded/not-found/rejected). Compare *real*
   GS FPS and image quality with the previous build.

To disable and A/B test the original decoder, remove `native/enable.txt`.
No configuration through `bt3.txt`, and recording does not affect the
original game archives.

## Path B — offline deterministic batch packager

For users with raw linear GS PSMT8 index dumps (for example generated from
their own texture decoding/capture tools), prepare JSON:

```json
{
  "textures": [
    {
      "source": "raw/stage_001.idx8",
      "tbp": 336,
      "tbw": 8,
      "gen": "0x12345678",
      "width": 128,
      "height": 128
    }
  ]
}
```

The GS TBP, TBW and SOURCE-page `gen` must match the actual current GS VRAM
texture. This example is only the format, not a real BT3 texture. Do not
guess the hash or treat uncompressed AFS archive members as PSMT8 pixels.

```sh
python3 port/tools/convert_psp_t8.py pack manifest.json native
python3 port/tools/convert_psp_t8.py verify native
```

Copy the `native/` output to `PSP/GAME/BT3/native/`. The converter checks
bounds, source file containment, length, header and checksum. It never reads
or commits the user's ISO and does not decode arbitrary BT3 AFS members.

## What remains for a real native BT3 portable port

- Parse each verified game model/material/animation format from the
  original AFS members and emit pretransformed PSP-safe GE vertex buffers.
- Convert *all* static GS PSM and CLUT layouts, including non-indexed RGB,
  alpha and GS framebuffer feedback, without changing the original colors.
- Native animation/transform/lighting for the unsupported VU1 programs;
  GS effects/SCREEN composition must remain correct.
- Replace per-frame PS2 software rasterizer/mirror only when the native
  PSP result matches the current reference under the same battle.
- Measure PPSSPP AND real PSP FPS; currently ~0–2 FPS is reported by the
  user's battle captures. This patch cannot promise Tag Team-level FPS.

This first stage preserves `fe93677`, the PSMCT32/PSMT8 feedback mirror
(which removed the rainbow corruption), and the existing reference GS
decoder, all untouched as a validated fallback.
