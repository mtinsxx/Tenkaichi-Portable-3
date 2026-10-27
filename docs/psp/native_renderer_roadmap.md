# Tenkaichi Portable 3 — native PSP renderer roadmap

Goal: keep BT3's fighter meshes, animation, gameplay and anime outline while reducing the cost of reproducing PS2 GS/VU1 on PSP. Tenkaichi Tag Team is an **efficiency reference**, not an asset or code donor. No proprietary game files belong in this repository.

## Starting evidence (user PPSSPP `log.txt`, October 2026)

- Late battle sample: 4 GS chains took 2234 ms in a 2.46 s interval; estimated VU1 interpreter 1052 ms (292 stage-program 399 runs); estimated GS draw 844 ms (71540 draws). Estimates overlap; do not add them as independent times.
- 48903 texture cache evictions accumulated by that point, and the framebuffer alias counters recorded large fallback/reupload totals. These are cumulative events, **not** proven hardware upload counts.
- Actual visible runtime measured 1.6–1.9 GS FPS in slow battle windows. A green CI build is not evidence of increased FPS.
- Defects remain in stages, UI/menu colour, HUD/gauges, framebuffer composition and effects; `GsGpu_FbUpload` and `GsGpu_Native` are incomplete and must not be described as implemented.

## Renderer path, source ownership

1. `port/src/gs/gs_core.c`: GS packet front end, GIF/VIF dispatch, page hashes and VRAM generations.
2. `port/src/gs/gs_vu1.c`: original VU1 instruction interpreter and native program routing.
3. `port/psp/psp_stage4.h`, `psp_stage4_clip.h`, `psp_fighter_vu1.h`: native transformed stage and fighter vertices.
4. `port/psp/psp_gu.c`: PSP GE triangle/sprite/state submission, framebuffer aliasing and texture cache.
5. `port/psp/psp_gfx.c`, `psp_main.c`: clocks, inputs and performance reports.
6. `port/src/plat_mpeg2.c` and `port/src/plat_movie.c`: separate MPEG opening path.

## Execution steps and proof

### Step A — safe duplicate-work removal (first code iteration)

- Avoid multiplying the same clip matrix twice for validated native stage vertices: native clip input already owns those floats. Keep all visibility, colour, UV and W checks identical.
- Preserve hot texture descriptor indices on append. Only destructive descriptor changes (eviction/memmove) bump the table serial; GS memory updates still invalidate through the upload epoch.
- Host checks assert exact transformed vertex bytes and cache invalidation rules. SDK CI builds EBOOT; real PPSSPP runtime improvement remains unproven until tested.

### Step A3 — regressions and page-local RGB texture keys

- Roll back `target_freshness` default to 0: the previous fullscreen stage corruption could be worsened by treating a cleared screen backing as a valid source. PSP framebuffer capture and cross-fade are **not yet** complete. This is a defensive rollback, not a claimed fix for black backgrounds.
- For only non-indexed GS source textures covering <=16 GS pages, reuse the decoded PSP texture when every source page's generation counter is still identical, even if an unrelated upload changed the global epoch. Indexed CLUTs and larger/unsupported spans retain strict global invalidation. No colour/palette compromises; stale samples remain forbidden by page counter checks.
- Record `unrelated-upload-skips` in `psp: texture guarded reuse`. Compare scene GS time, texture key+CLUT time, native VU1 and actual FPS after installing the EBOOT. No speedup claim without runtime measurements.

### Step B — VU1/GS native geometry

- Count stage-program 399 rejects by reason (invalid W/clip/matrix/buffer) and trace which states force the interpreter.
- Expand exact native clipping only where tested against reference output, preserving shadow, outlines, UV and perspective semantics.
- Reduce redundant GS register and texture calculations across **compatible** adjacent draws. Never reorder alpha-dependent primitives to chase batches.
- Compare VU1 time, GS draw time, native-fallback ratios and GS FPS against the original baseline on the same scene.

### Step C — correct framebuffer/UI composition

- Implement or faithfully adapt missing framebuffer readback/copy and native effect passes; test screens, stage overlays and health/Ki bars individually.
- Retain PR22 / `fe93677` white text texture modulation, GS FIX alpha conversion, palette and stencil checks.
- Do not reduce the fighters' model meshes or disable cel outlines to claim a speedup.

### Step D — movie and sustained gameplay

- Profile MPEG opening separately; avoid confusing video FPS with gameplay.
- Measure 30+ seconds in opening, menu and real fights with same PPSSPP device/settings and compare quality screenshots.
- Performance goals (5, 10, 20–30 FPS) are **targets**, not guarantees. No automatic graphic sacrifice and no claim that the PSP has native PS2 GS capabilities.

## Safety / release gates

- Host invariants and pspdev EBOOT CI must pass; no copyrighted ISO, ELF or extracted game assets committed.
- A render optimization must preserve characters, menus, colour, clipping, alpha and frame order or remain behind an explicit rollback.
- When runtime tests are absent, report code change and build success only — **do not claim measured FPS improvements**.

### Phase 1: physical display FBP aliasing (October 27)

The current PS2 GS display FBP and its paired previous FBP are distinct surfaces. The PSP mapping accidentally put both on the *active* draw buffer, causing sampling to fail the self-alias guard and fall back to possibly stale emulated GS memory (no GPU readback). Use the *other* PSP physical display buffer for the paired FBP. Keep all auxiliary FBPs mapped to WORK. `scene_pair_separate=0` rolls this mapping back for A/B visual tests. Separate logical WORK owners now force a GU batch boundary. Routing tests and PSP compilation do **not** verify the black stage has been fixed at runtime.

### Phase 2 — cache VU1 program399 accepted clipped fans (October 27)

The native CPU stage path previously performed polygon clipping/projection twice per clipped triangle: preflight, then submission. It now retains the **exact** projected vertices in a fixed 4096-vertex bounded scratch cache and reuses them for GE submission, without re-clipping. Any failure or overflow rejects the entire native strip before changing GS registers, preserving the VU1 interpreter fallback, vertex order, coordinates, colour, and depth. A host parity test checks matching vertex bytes and conservative overflow; runtime FPS improvement is unmeasured, and the bulk of program399 interpreter fallbacks remain for future adaptation.

### Phase 3 — distinct PSP VRAM targets for battle FBPs 336 and 368

LOG_ALL recorded 8,080 overwrites of GS FBP 336 by FBP 368 and 7,971 attempts to sample a no-longer-rendered texture. The PSP previously used a single 512x272 5551 WORK framebuffer for BOTH images. This iteration reserves a second distinct physical WORK buffer in the PSP's 2 MiB VRAM, routes FBP 336 to WORK0 and 368 to WORK1, and invalidates only other logical owners of the same physical surface. Depth remains disabled for both WORK surfaces while preserved for fighters on the screen. Framebuffer texture cache flushing, per-surface alias ownership, and startup clears now cover both. Trade-off: 272 KiB less PSP VRAM for cached textures; benchmark texture evictions as well as successful reuse before accepting runtime gains. Full 512x448 GS readback and Z16-as-texture remain unimplemented; no FPS improvement claimed until tested.

### Phase 4 — texture cache eviction uses actual last-use order

The previous LRU used only `Tex.last` (PSP frame index). During high-churn battles, tens/hundreds of textures tie on the SAME frame, so the eviction scan can discard a just-accessed hot texture while preserving a colder descriptor. New independent `Tex.useTicket` is updated on direct guarded hit, full-key cache hit and new decode; `tex_evict_one` and descriptor-pressure cleanup select least recent use. `Tex.last` is unchanged and still governs the essential current-frame GE fence. A wrap-aware unit test confirms priority even around access ticket overflow. This changes no pixel conversion, geometry, controls or rendering state. The resulting FPS and eviction-count improvement remain unmeasured until tested on PPSSPP.

### Phase 5 — MPEG-2 PSP-only quantizer optimization (October 27)

The software MPEG intra decoder computes `2*level*intraMatrix[pos]*qscale / 32` for every AC coefficient in the opening/ending movies. For PSP only, it now precomputes 64 factors when entering a slice and each time `qscale` changes in a macroblock; coefficients use one multiplication and the **same** final integer division. The matrix, VLC, zigzag, mismatch control, IDCT, decoded planes, YCbCr-to-5551 and audio timing remain unchanged. A C99 host test checks arithmetic parity over every 64 matrix positions and qscale range 0–112. This is a narrowly scoped micro-optimization, not a validated fix for the 96 ms MPEG frame decode bottleneck.

### 2026-10-27 — PSP CPU VRAM cache alias and indexed offscreen feedback

Native PSP CPU accesses now use **0x44000000**, the real uncached VRAM mirror, not 0x04000000 (cached). This repairs invalid PSP hardware assumptions affecting GPU-to-CPU probes, initial VRAM clears and direct texture uploads to GE. GE draw/display/depth commands still use plain offsets, not CPU pointers. The source of the user's black scene remains unresolved: PPSSPP host-side GPU rendering may defer CPU-side VRAM readback even with the correct PSP alias. The battle's dominant `PSMT8H` (`0x13`) source from GS FBP336 must also NEVER bind the 16-bit RGB5551 PSP work framebuffer as if it were an indexed GS high-byte plane. An explicit incompatible-format guard now prevents that incorrect alias, preserving the old GS VRAM fallback. Making GPU-produced offscreen pixels faithfully readable as PSMT8H still requires a correctly synchronized PS2 framebuffer conversion (or CPU/software rendering of that GS target); **this patch does not implement that missing conversion**. Host C99 regression tests cover address aliases and incompatible modes. No promises about black-stage restoration or FPS.

### 2026-10-27 — native indexed PSMT8 texture storage / cache

**Correction to previous diagnosis:** `TEX0.PSM=19` (decimal) is `PSMT8=0x13`, NOT `PSMT8H=0x1b` (decimal 27). The earlier claim that black backgrounds specifically involved the high-byte plane was wrong. Battle logs show repeated GS host uploads to an index-texture region, and full software expansion of every PSMT8 texel to RGBA before uploading to the PSP GU. This release stores genuine GU_PSM_T8 index texels and the exact 256-entry CSM1 palette in one cache-owned allocation; indices retain existing GS swizzle, palette alpha scales 0x80 to PSP 255, swizzle is byte-accurate, and fe93677 baked white-glyph textures remain in the old path. Reduces cached bytes from ~2-4 bytes/texel to 1 byte/texel +1 KiB palette (so reduced cache evictions/decoding possible). Full PS2-to-PSP offscreen blending/readback and depth textures are **still incomplete**; no guaranteed scenario or FPS fix until PPSSPP verification. Native PSP C99 test guards palette mapping and storage sizing.
