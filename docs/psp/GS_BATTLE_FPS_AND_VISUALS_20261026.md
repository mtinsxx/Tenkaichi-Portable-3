# BT3-PSP battle: GS performance and missing visuals (2026-10-26)

Source: user PPSSPP log built 2026-10-26 15:47:27 from PR #28. Verified depth conversion marker, but `gs_depth=0` in that run. Battle intervals typically complete **2 GS frames in ~3.3 s** and submit ~50k triangles, with ~7k interpreter calls and >2k native-stage fallbacks. Source texture page/CLUT hashing takes ~600–750ms; GUfinishSync ~0ms. Some high-index and special effect passes are skipped; multiple GS FBPs are mapped into one PSP WORK target. These are separate performance and graphical problems. **No visual gameplay success is claimed.**

This experimental patch adds:
1. A guarded 8-slot recent TEX0 cache using an epoch incremented at every GS upload and CPU palette paint. The full content/page/CLUT hash path remains whenever an epoch changes, TEX0/TEXA/LOD/white flag changes, or cache descriptor insertion/eviction invalidates indices. No changes to texture pixels, colour, order or number of draws.
2. Logs `guarded texture reuse [texepoch-20261026]` and `texture guarded reuse: hits/full-key`.
3. Default `gs_depth=2` on **screen only**, to exercise corrected 24→16 Z. Opt out with `gs_depth=0` in `bt3.txt`.
4. Default `gs_hi_tex=1` only for the implemented PSMT8H/4HL/4HH decoder. Opt out with `gs_hi_tex=0` in `bt3.txt`. Depth-as-texture and native GS effects are **still unsupported**.

Validate the same PPSSPP battle/camera as PR #28; compare first FPS number (not virtual %), scene texture, Goku overlap and Kamehameha; verify `GS depth: mode=2`, `GS high-index textures`, `texture guarded reuse`, `PROFILE interval`, `VIF/VU1`. If there is a regression, isolate `gs_hi_tex=0` then `gs_depth=0` one at a time. Do not merge until runtime validation.

## Native VU1 admission improvement

The PSP backend already sends **affine** per-vertex ST/Q texture coordinates through GU_TRANSFORM_2D on interpreted batches. The native stage/fighter prepare routines previously rejected entire strips if W (Q) varied by more than ±0.5%, even though both routes submitted identical affine semantics to this same backend. Removed this extra W-flatness filter **only** for otherwise fully accepted strips. Non-finite/invalid vertices, negative W, off-clip vertices and unsafe fans still fall back to the original interpreter. Regression test with two different W values checks ST/Q preservation and clipping rejection. This is intended to reduce the thousands of conservative fallbacks in the uploaded battle log, not to claim native clipping or genuine PS2 perspective-correct rasterization.
