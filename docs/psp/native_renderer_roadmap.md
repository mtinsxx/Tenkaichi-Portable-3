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

### 2026-10-27 — VU1 program6 native ground-shadow geometry

The PSP's `GsGpu_DrawVu6` was an unconditional NO-OP even though `gs_vu1.c` HLE program6 calls it for the game's ground-shadow geometry. Port the Vita's `vu_transform(kind=2)` screen matrix and UV transform from the original `consts` and 16-float shadow vertex input; use GS.PRIM context when the caller says `ctx=-1`. Reject only malformed projected vertices and triangle strips flagged through the third vertex, and send each surviving triangle through the existing GS/GU state pipeline (no new framebuffer-depth settings, alpha hacks or extra PS2 game assets). C99 regression tests exercise identity transform, UVs, packed invalid flags, and invalid W. Shadow silhouettes/work-page composition and background framebuffer semantics still require verification; don't claim black stages fixed solely by this path.

### 2026-10-27 — GS-owned screen clearing

Three user PPSSPP images show the 3D arena fully textured and lit in the intro/camera pass, but then only tiny fighters over solid black in the fight transition. Those prove that the scene **can** render and suggest the scene's lifetime/compositing is not being preserved across GS passes. In the PSP backend, `GsGpu_FrameEnd` unconditionally cleared the newly selected PSP screen buffer black every time, *in addition* to the PS2 libgraph `sceGsSwapDBuff` explicit GIF clear sprite. The PSP now leaves colour lifetime to the game's GS packets and only clears PSP Z at frame start (the single shared PSP depth buffer is not the PS2's per-FBP Z semantics). Both physical PSP screen buffers still start zero on init. Includes a source-level regression guard. This may address black scene frames lacking stage submissions, but **has not been validated in-game**; preserving incomplete frames may reveal trails if the game doesn't clear certain passes. No bt3.txt settings or model/depth toggles. If visual regressions appear, revert this commit in code, not by asking the user to change the display settings.

### 2026-10-27 — GS SCISSOR clipping and safe texture-work culling

A code-level translation bug affected all GS drawing paths: when the PS2 scissor projected outside the 480×272 PSP viewport, `state_apply` DISABLED the PSP scissor test, letting otherwise-clipped GS effects overwrite arbitrary regions. The GE scissor now remains enabled even for empty projected regions. `GsGpu_Draw` also conservatively rejects primitives fully outside the source PS2 SCISSOR (inclusive boundaries) before expensive PSM texture decoding, framebuffer lookup, GS-state translation and GE upload. It explicitly preserves ambiguous/nonfinite coordinates, uses a one-pixel tolerance and does not change depth, palettes, HUD shader modes or character geometry. Counters `GS source scissor culling` make any CPU savings measurable; exact FPS and visual correctness await a PPSSPP run.

### 2026-10-27 — eliminate repeated unsupported GS state decoding within native VU1 strips

When the first native stage/fighter triangle is rejected by a *static* PS2 GS format constraint (unsupported FRAME PSMCT16S, FRAME=ZBUF alias, or unsupported texture/depth format), the original renderer repeats exactly the same expensive state checks for every subsequent triangle in the VU1 strip. The native strip now memoizes only these three **state-only** reasons and counts later GS draws/skips without repeated memory lookup, decode or state translation. It does NOT memoize visibility, out-of-scissor geometry, transient allocation/texture failures or any successfully rendered primitive. A C99 unit test verifies the static reason policy. This reduces emulator CPU overhead for uniformly unsupported strips; it does not implement the missing framebuffer/depth effects or guarantee a particular FPS improvement.

### 2026-10-27 — broad GS-to-PSP state translation: scissor semantics and framebuffer write masks

Two engine-wide bugs were proven against the PSPSDK and original GS software reference. **(1)** PSPSDK `sceGuScissor(x,y,w,h)` takes widths/heights; the PSP backend had passed GS clip right/bottom endpoints as `w,h` whenever x0/y0 were nonzero, silently enlarging clip regions and allowing arena/HUD/effect primitives to touch pixels outside GS SCISSOR. It now converts inclusive 512x448 GS coordinates to actual PSP extents, clamps to screen and supplies valid nonzero GU dimensions. **(2)** GS `FRAME.FBMSK` had been ignored for nearly all draws except one special battle HUD stencil path. Every primitive now maps GS framebuffer write-protection bits to PSP RGB5551 channel masks, preserving the stencil-specialized HUD path. PSMCT24 preserves alpha. PSP 5551 cannot represent GS sub-5-bit RGB masks or GS 8-bit alpha masks losslessly, so this is a conservative translation, not full GS bit-accurate framebuffer-as-texture support. Both changes apply across scenes, characters, 2D HUD and menus, not by black-screen heuristics. Added C99 tests and periodic stats; in-game validation still required.

### 2026-10-27 — remove desktop environment lookups from the PSP hot VU1 dispatcher

`GsVu1_Call` runs thousands of times during battle and tested desktop-only `BT3_VU_DUMP`, `BT3_VU_CALLS`, `BT3_VU_INTERP` (multiple times) and `BT3_GS_VERBOSE` through `getenv` on its hot path. The PSP build cannot read external configuration files and has no runtime debug environment used by this port. The PSP now constant-folds only these desktop switches, allowing dead-code elimination of VU debug dumps and getenv branching without altering which nine VU1 microprogram paths can execute or their interpreter fallbacks. Desktop and Vita retain runtime switches. This optimization is only one source of overhead; it does not eliminate heavy interpreted VU instructions or certify improved FPS.

### 2026-10-27 — GS framebuffer reconstruction, phase 1: persistent GE snapshot/feedback

The PSP backend previously kept only two auxiliary physical 512x272 RGB5551 work buffers: **overwriting a physical WORK image lost the preceding logical PS2 framebuffer permanently**. Added a main-RAM-backed 512x512 RGB5551 texture per logical target, **allocated lazily only when needed**. The GE copy is queued between ordered flush/draw commands without CPU pixel reads or GPU fences. Logical write versions, GS upload generations and one-transfer-per-target-per-frame limit prevent sampling stale or overwritten snapshots. This permits later samplers to recover overwritten images instead of always decoding stale PS2 emulated VRAM. Direct framebuffer-as-texture sampling also chooses a padded 512x512 snapshot when available and uses the physical 512x448 GS -> 480x272 PSP UV transformation, repairing the old inverted UV scale and clipped bottom 16px. C99 lifetime/coordinate tests added. No 32-bit GS alpha/PSMT8 bit-plane reinterpretation is claimed (PSP hardware 5551 backing remains lossy); full parity requires a proper software GS path for unsupported feedback effects, plus PPSSPP runtime visual testing. Captures in GE lists need PPSSPP/hardware verification before accepting them as fully functional.

**Memory bound:** at most 3 snapshots (1.5 MiB), so the new virtualization cannot silently consume 3 MiB or more on the real 32 MiB PSP. Any fourth simultaneous logical target falls back to the existing conservative GS memory path and is counted as `budget-skipped` in LOG_ALL. The 3 allocations are reusable across target-slot changes, without freeing data referenced by in-flight GE commands.

### Framebuffer coherency follow-up — 2026-10-27

Audited snapshot read/write hazards. Fixed a genuine lifecycle error: logical PS2 FBPs previously retained `rendered=1` after their physical PSP drawbuffer changed because `gPspTargetFreshness` was disabled to avoid an earlier black-screen regression. Target rebinding now copies an existing drawable image (when permitted), invalidates the live alias, and keeps the snapshot for later feedback. A snapshot can be updated again within one frame **only before being referenced by the GPU**; after a same-frame read, it cannot be overwritten until the frame fence. Up-to-date snapshots are reused across frames, avoiding redundant 480x272 transfers. Removed the unsafe fallback that bound bare 512x272 PSP VRAM as a 512x256 texture; GPU targets must have a valid padded 512x512 snapshot or conservatively use the existing GS VRAM decoder. Tests now cover rebinding, nonredundant transfers, same-frame writes and GPU read pins. This improves target coherency but does **not** reconstruct lost GS 32-bit alpha, indexed-byte reinterpretation or Z-buffer-as-texture; their missing semantics can still cause absent battle scenes.

## PSP-native conversion and repository cleanup gate — 2026-10-29

**Scope correction:** The current executable is native PSP *game code*, but the
graphics/data path is only partially converted. The GS reference renderer,
VU1 interpreter fallback, on-demand framebuffer CPU replay, runtime texture
decode, and many compatibility branches are still active. The latest GS
scissor optimization does not convert characters, stages or textures in
advance. Do not claim completion or Tag Team-level performance on CI alone.

### Conversion order (no graphics-quality sacrifice)

1. **Offline data pipeline:** parse legally obtained BT3 archive resources
   without committing game assets; build repeatable converters for original
   mesh layout, material/CLUT/textures, animation, particles, shadow and UI.
   Validate counts, indices, palette bytes, UV and animation skin transforms
   against the source data. Emit PSP-friendly assets plus a versioned manifest
   and cache key; keep existing archive loader as a reference until verified.
2. **True native runtime:** load preconverted resources directly; submit
   meshes/skyboxes/sprites in durable PSP GU vertex buffers; preserve original
   strip winding, clipping, depth, alpha, cel outlines and the original
   gameplay input/animation. Replace proven VU1 program 399/4/6 workloads
   gradually; never silently drop unsupported triangles.
3. **GS feedback elimination:** replace repeated CPU replay of 4 MiB GS
   framebuffers with native PSP render passes or precomputed effects, but
   first prove cross-PSM PSMCT32/PSMT8 and HUD alpha outcomes are equivalent.
   Preserve the current working feedback implementation as an A/B oracle.
4. **Proof:** host structural/pixel tests, PSP SDK CI, same-fight PPSSPP
   before/after video, stable hardware PSP performance counters, and
   no black-frame/oversized-polygon regressions. 30 FPS is an initial goal,
   not an achieved result; 60 FPS and Tag Team parity are unproven.

### Cleanup: remove only after the new dependency graph is verified

- **Generated outputs:** never version EBOOT.PBP, ELF/ISO, logs, cache,
  expanded discs, object files. The root gitignore excludes these.
- **Historical branches, source files and tests:** a branch or source path
  being unused by today's PSP compiler is not proof it is useless. PC/Vita
  sources contain format knowledge and attribution; tests guard color/font,
  VU1, GS layout and regressions that previously broke. Do not bulk-delete
  or rewrite them merely to reduce visible file count.
- **Candidate removals after native cutover:** superseded runtime fallback
  flags, duplicate texture decoders/CLUT conversions, all per-triangle
  interpreter dispatch paths proven unused, debug-only logging built into
  release, obsolete GS readback copies and broken prototype renderers.
  Require an exact file/function caller audit plus a green PSP build and
  identical visual test before deletion. Keep copyright/license notices.
- **Repository truth:** existing README and this roadmap are the canonical
  status documents. Avoid adding a new progress note for every tiny commit;
  retain technical tests and replace outdated status statements instead.

No runtime subsystem has been removed or newly converted by this audit.
