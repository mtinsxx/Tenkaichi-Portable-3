# PSP performance phase — optional reduced-resolution texture copies

Branch: `perf/psp-texture-lod-20261025`, based on
`fix/psp-gs-indexed-hi-20261025`. Do **not** merge into `main` or
overwrite the known-good `fe93677` until PPSSPP and PSP hardware tests pass.

## What the user's screenshot proves and does not prove

The overlay `1/1 (100.4%)` is the emulator running around
100% speed at the captured instant, **not 100 fps**. The rest of
the image (grey bands, old debug-screen text and HUD over a wrong
framebuffer) continues to show unresolved GS composition, not
necessarily insufficient CPU/GPU speed.

The PSP cannot natively render the whole PS2 GS pipeline at PS2
texture quality with 2 MiB of VRAM. The port already uses RGB5551
for opaque/masked copies, 32-bit only when intermediate alpha is
needed, swizzled textures and an overflow cache in main RAM.

## New optional `tex_limit` configuration

| Setting | Triangle/3D texture cap | Screen/HUD sprites |
|---|---|---|
| `tex_limit=0` | original decoded copy, at most 512x512 | unchanged |
| `tex_limit=256` (**experimental default, no bt3.txt needed**) | downsample sides larger than 256 (e.g. 512x512 to 256x256) | unchanged |
| `tex_limit=128` | downsample sides larger than 128 (e.g. 512x512 to 128x128) | unchanged |

The feature only acts on 3D triangle draws (GS primitive type 3).
Framebuffers sampled as textures, 2D sprites and the original GS
VRAM are **never modified**.

- Source textures are still read from the original GS VRAM
  layout (including CLUT/PSMT8H/4HL/4HH changes in parent branch).
- The *cached PSP copy* is decimated in place before 16-/32-bit
  conversion and swizzling. No new intermediate full-size output
  buffer is allocated.
- The cache keys include **both texture dimensions**, avoiding an
  accidental full-size/low-res cache hit for the same TEX0.
- Original FST UV coordinates are rescaled to the new dimensions;
  ST/Q normalized UV coordinates automatically use smaller dimensions.
- Texture cache stats include `reduced-new`, `cache-hits`
  on downsampled copies and `cache-pixels-saved` (in 1024s).

**Memory use:** a single 512×512 RGBA5551 GPU copy takes 512 KiB;
a 256×256 copy takes 128 KiB; a 128×128 copy takes 32 KiB.
These savings apply to cached copies, **not** to the full PS2 disc,
game RAM, GS emulated VRAM, or transient full-resolution source read.

**CPU cost:** the source GS VRAM decoder still reads original pixels;
the conversion/swizzle and GPU transfers operate on fewer pixels.
Whether the change increases FPS depends on the real bottleneck.
C tests and CI do not establish a runtime speedup.

## Controlled comparison on Android PPSSPP

Copy the newly built experimental `EBOOT.PBP` while retaining
a backup of the previous known-working EBOOT.

**You do not need to create `bt3.txt`** for this experimental build. It starts with `tex_limit=256` even if the file is absent. For a controlled baseline A/B test, you may optionally create `ms0:/PSP/GAME/BT3/bt3.txt` with:

```ini
report=1
gs_fix=0
gs_clut=0
gs_depth=0
gs_hi_tex=0
tex_limit=0
```

Use the **same** battle and camera action for every experiment,
then close/reopen the EBOOT between runs:
1. `tex_limit=0` — baseline frame time, speed%, visible errors.
2. `tex_limit=256` — check scenery/characters and comparison.
3. `tex_limit=128` — check if the extra quality loss brings any
   measurable improvement. Do not prefer it if gain is negligible.

Capture and compare `ms0:/PSP/GAME/BT3/log.txt` fields:
- `psp: frame cadence` — real presented GS FPS;
- `psp: PROFILE interval` — CPU/GE/VBlank timing;
- `psp: PROFILE textures` — hashing/decode/swizzle time;
- `psp: texture cache` — RAM/VRAM, hits, misses, evictions;
- `psp: texture LOD` — numbers downsampled;
- `psp: GS menu passes` and `psp: fb-alias` — how much
  visible corruption is attributable to unimplemented GS paths.

If texture LOD counters are all zero during battle, no 3D
texture reduction happened; texture LOD cannot explain FPS for
that scene. In that case, focus on VU1, texture-key hashing,
framebuffer routing, or CPU rather than lowering image quality
further. Do **not** bypass combat or skip physics updates.

## Required before approving

- [ ] Both GitHub EBOOT workflows green on HEAD (including default 256 when no file exists).
- [ ] Controlled log FPS/time comparison, no crash/black screen.
- [ ] HUD, button prompts, fight animation, effects, face textures.
- [ ] Test real PSP 64 MiB; quality modes disabled by default.
- [ ] Investigate incorrect framebuffer composition separately.
