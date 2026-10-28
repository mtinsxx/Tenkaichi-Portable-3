# Native PSP migration — first integration of assets, VU1 and GU passes
**Experimental**. Base: PR #7 predecoded VU1 (itself based on PR #6 framebuffer/cache work).
Main and the already-tested EBOOT remain untouched.

## 1. Offline asset preparation
`tools/psp_native_assets.py` accepts **only local, legally extracted data**:
- `t8`: already decoded linear PSMT8 pixel indices (one byte/pixel);
  validates width/height, packs 32-byte versioned T8P1 header with
  TBP, TBW and the runtime GS generation/page+CLUT hash, plus FNV-32
  and native PSP indexed bytes. Runtime integration is **implemented**
  for this type alone.
- `rgba5551`: converts raw RGBA8888 into PSP 16-bit RGB5551. It
  refuses semi-transparent alpha, which cannot be represented without
  visual changes in 5551.
- `mesh`: validates JSON triangles/positions/UV/RGBA, packs a portable
  little-endian mesh intermediate M3P1.
- `animation`: validates per-frame 3x4 affine transforms per bone,
  packs little-endian A3P1 preserving floating point precision.

The **mesh/animation formats do not yet have a PSP runtime loader**:
replacing the game's model animation and skinning must be done only
after comparing transform output to the PS2 VU1 path. This tool is
the first step of asset conversion, not a game extractor, nor a
complete resource replacement. It contains no game assets.

### Example: locally prepared T8
First, obtain a lawful *linear, already decoded* PSMT8 index buffer
for the exact texture. If the game is using different backing data,
do not force a pack for the same TBP. Obtain the TBP/TBW,
dimensions and matching `gen` from the PSP's T8 pack diagnostics
when the opt-in marker is present.

```bash
python3 tools/psp_native_assets.py t8 --source ./my_texture.indices \
  --tbp 336 --tbw 8 --gen 0x12345678 --width 128 --height 128 \
  --output ./t8_0150_12345678_128x128.tp8
```
(The generation number above is an **example only**, not the
real value for the user's battle.)

Copy the generated file to
`ms0:/PSP/GAME/BT3/native/t8_0150_12345678_128x128.tp8`.
To explicitly enable resource loading create
`ms0:/PSP/GAME/BT3/native/enable.txt` (an empty file is fine).
If the marker is absent, no file lookup is attempted during gameplay.

At a cache miss the PSP checks the file header (generation, TBP,
TBW, dimensions, version), exact byte count and FNV; then compares
64 tile-boundary pixel samples with the CURRENT scalar GS texture
before using the data. If the content differs it reconstructs
from the original GS decoder. The CLUT is **still read live from
the game**, since the game can animate palettes. No unchecked
preconverted texture is trusted. Without supplied assets this
new path is idle and should not claim an FPS improvement.

## 2. Native Allegrex VU1 optimization
VIF MPG now performs an exact 32-bit comparison. If the program word
has not changed, skip the upload-time micro-op decoding; all
MSCAL/MSCNT execution, Q/CLIP state, branches and XGKICK stay intact.
The log now reports changed vs identical words and the interpreted
instruction count. This is only an incremental speed improvement;
**stage399's remaining 390k+ instructions per 3 combat frames are not
recompiled to standalone native basic blocks**. Doing that safely
requires instruction-level differential traces and state comparison.

## 3. GU-native ordered render-pass boundaries
Existing validated native stage and fighter VU1 strip paths now mark
stage/fighter batches; legacy GS sprite draws mark sprite batches.
The queued GE triangle batch is flushed **on transitions only**,
ensuring no native stage, actor or sprite batch is incorrectly
combined. The original GS primitive order is preserved exactly:
reordering stage, alpha, HUD and framebuffer feedback by category
would violate GS depth/alpha semantics and could reproduce corruption.
No special PSMCT32/GS framebuffer readback is faked.
The periodic log reports stage/fighter/sprite submissions and barriers.

## Checks and limits
- Host regression tests for the exact T8 header/FNV, offline converter
  (including rejecting semitransparent 5551), and pass transition
  policy, as well as earlier GS/VU1 regression tests.
- PSP SDK CI build must pass before distributing EBOOT.
- Verify no new crash, compare identical stage/character/camera scene
  against previous build and inspect VU1 changed/identical counters,
  GU transition count, new decoded textures and FPS.
- This does NOT complete a native PSP game. Native model/animation
  loading, full per-program VU1 native compilation, robust GS
  framebuffer-as-texture readbacks, and 30/60 FPS stable are **unfinished**.
