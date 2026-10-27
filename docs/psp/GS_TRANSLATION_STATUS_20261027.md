# TENKAICHI PORTABLE 3 — PS2 GS → PSP GU translation status
Date: 2026-10-27. Branch: `fix/tp3-psp-gs-fb16s-mask-20261027`.
**This is not a complete GS translator and is not a guaranteed graphics fix.**
The original `port/src/gs/gs_core.c` is the PS2 software reference.

## Improvements on this experimental branch
- Direct 5551 rasterization for **PSMCT16 and PSMCT16S**. This is pixel-format
  compatibility, not equivalence of PS2 GS swizzled VRAM.
- FRAME.FBMSK: GS 32-bit mask packed into GS 16-bit component positions for
  PSMCT16/16S, then projected onto PSP GU pixel-mask channels. PSMCT32/24
  mapping and the existing battle HUD stencil guard remain.
- TEST.ATE/ATST/AFAIL: on non-stencil passes the PSP draws fragments that
  pass the alpha test normally, then replays geometry with inverted ATST
  for FB_ONLY (no Z write), ZB_ONLY (no colour), RGB_ONLY (RGB, no alpha or
  Z). KEEP is one pass; an ALWAYS alpha test never needs a replay. This
  consumes additional GPU operations when the game uses AFAIL.
- Virtual framebuffer snapshots retain the existing **3 × 512×512×2B
  = 1.5 MiB** upper bound, in 64-byte-aligned main RAM. The new
  producer-PSM check rejects incompatible format reinterpretations
  and falls back to GS memory decoding rather than treating a linear
  5551 buffer as native PS2 32-bit or 16S-swizzled memory.
- `GsGpu_FbUpload`: before an incoming CPU-to-GS display upload, reuse
  the existing snapshot system to preserve a compatible prior screen
  image when available, in display/GE submission order. Otherwise report
  the case as unsupported. This does NOT write PSP GE output to emulated
  PS2 swizzled VRAM, and does not replace direct MPEG presentation.
- New host checks cover all 8 ATST modes against their inverses for all
  256×256 alpha/ref pairs, all AFAIL write masks, and all 64×64
  producer/consumer PSM pairs, in addition to framebuffer mask tests.

## Important remaining gaps
| GS facility | PSP state | Why exactness is still missing |
|---|---|---|
| PSMCT32 RGBA and alpha history | **Partial** | GE display buffers are RGB5551, not eight bits per component, and PSP destination alpha is one bit |
| PSMCT24 | **Partial** | PSP sampling/buffer alpha cannot preserve the GS view's TEXA and stored-byte semantics across all aliases |
| PSMCT16S | **Direct colour draw only** | Correct GS VRAM page/block/swizzle reinterpretation still requires software reference conversion |
| GS FBMSK | **Projected** | Low-order per-channel GS masks cannot be represented by a five-bit PSP component |
| AFAIL | **Two-pass approximation** | PSP alpha precision and destination-alpha/stencil interaction differ; no pixel-order software fallback |
| DATE / FBA / TEST | **Special-case HUD** | Full GS destination-alpha and alpha-bit write rules not implemented |
| GS Z24 / Z32 | **Not supported as textures** | GU Z buffer is quantized Z16, shared across logical GS ZBPs |
| GS Z16/16S-as-texture | **Not supported** | Would require coherently preserved depth image with independent GE read/write lifetimes |
| GS framebuffer CPU/GPU coherence | **Partial** | `GsGpu_FbUpload` preserves only a preceding RGB5551 view, not byte-exact GS VRAM |
| Composition of all logical FBPs/ZBPs | **Partial** | Two PSP WORK surfaces plus three snapshots; can't hold every GS target simultaneously |

## What is required for a *complete* translator
The PSP GU cannot directly reproduce this list at its native 5551 output.
A complete solution needs a separate authoritative PS2 GS local-memory
representation with 32-bit colour and depth, correct format-specific
swizzles and overlapping aliases, pixel-accurate blending/TEST/FBMSK,
and synchronized raster/writeback between PSP GE surfaces and GS memory.
The software reference in `gs_core.c` has much of the per-pixel math,
but the PSP GPU path does **not** currently maintain a coherent copy
of all rasterized pixels in that software memory. Naively switching
every pixel to Allegrex software rasterization would severely reduce
FPS, including in menus. A selective precision fallback must be
validated against real GS traces before enabling it.

## Validation, no settings file required
The experimental EBOOT inherits Tenkaichi Portable 3's built-in defaults:
the user does **not** need `bt3.txt`. Compare screens in PPSSPP for
opening, character select, stage, fighter shading, Ki/HP HUD and combat
while collecting `log.txt`; compare both image correctness and FPS.
Github Actions compilation proves an EBOOT was built, **not** that these
graphics paths were reached or performed correctly in PPSSPP/real PSP.
Do not merge this draft PR without runtime evidence.
