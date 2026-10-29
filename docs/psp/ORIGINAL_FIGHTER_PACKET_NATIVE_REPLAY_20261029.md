# Original BT3 fighter mesh VU packet capture and native PSP replay — October 29 2026

## What changed
Original BT3 fighter meshes are uploaded to **VU1 programs 0/1** in an
actual, documented layout of three PS2 quadwords per vertex (48 bytes):
position.xyz plus bone-A weight, original normal float4, texture ST float4.
This is after the game has already loaded/decompressed its proprietary
AFS model format. The engine's VU1 header still supplies two GS TEX0
layers, and the VU constants provide bone-A and bone-B matrices,
two pivots, screen/clip matrices and lighting.

The PSP-native fighter code already transforms those original vertices,
skins between two bone matrices, computes lighting/cel shading and
draws through the existing GU path. The integration adds an optional
*real original-mesh packet resource bridge*:
- In **capture mode**, the EBOOT writes bounded (max 24 per session)
  *actual original fighter VU packets* to the user's memory stick.
  It does NOT guess proprietary AFS character formats.
- In **replay mode**, the PSP verifies version, vertex count, byte
  stride, original FNV hash, payload FNV and **every byte against the
  current live original VU packet**, then uses the cached asset as
  the data source for its **existing native 2-bone PSP transform**.
  An incorrect/stale/corrupt asset falls back to the live original
  packet without changing game behavior.
- The **live VU bone matrices, weights, pivots, animation, projection,
  GS PRIM, per-layer TEX0 materials and outline/lighting** remain
  authoritative on every frame. No saved static animation or
  manufactured GS material data can replace the current game state.
- Existing M3P1/A3P1 GU preview is unchanged; optional pack replay is
  in the **real fighter battle path**, not a separate fake fighter.

## On-PSP use (PPSSPP Android supported)

No marker files = zero extra I/O or per-vertex hashing, unchanged
battle behavior. All marker files are intentionally optional and
independent of the old `bt3.txt`.

First, create the user's PSP directory
`PSP/GAME/BT3/native/` and an empty marker:
`enable-fighter-capture.txt`.

Run a battle with the original BT3 data. It will collect original
geometry packets of the native-supported fighter strips in files
`fighter_<hash>_<vertex_count>.f3p` in that directory.
Each file is a 24-byte `F3P1` header plus exact original VU bytes.

After capturing, remove `enable-fighter-capture.txt`; add an
empty `enable-fighter-packets.txt` and restart the game.
The EBOOT will use only verified original packets and retain
**live animation and materials**. Files can be inspected or
independently generated with:

```bash
python3 tools/psp_native_assets.py fighter-vu --source actual_original_vu_packet.bin
```

The `fighter-vu` asset can also appear in a `bundle` manifest,
but **must** be a real 48-byte-per-vertex original VU1 input stream,
not a random AFS entry. The runtime does exact comparison, not just
a hash or loose model ID.

The log contains:
`fighter VU source packet integration enabled`,
`captured actual BT3 fighter VU packet` and
`native BT3 fighter packet replay`.

## Important limits
**This does not fully decode proprietary AFS character-model files,
does not make all original BT3 assets directly portable, and does not
replace the GS stage renderer or VU program-399 interpreter.**
Capturing the already-decompressed VU stream avoids inventing unknown
AFS structures but still **uses the original game to generate
the source**. Hashing and validation can cause a performance
regression when replay mode is enabled; no performance claim is made.

This path is intentionally conservative. It does NOT bake animation
bone matrices into models, discard skin weights, swap materials, or
substitute only loosely similar meshes. The rest of the fighter's
existing native GE path is preserved. The working GS feedback
PSMCT32/PSMT8 reconstruction, `fe93677` UI color fix and the
original AFS source archives are untouched. No proprietary assets
or game binaries are committed to GitHub.

Build ID: `TP3-FIGHTER-LIVE-BONES-20261029`.
