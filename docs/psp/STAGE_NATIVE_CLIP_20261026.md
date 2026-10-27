# Experimental native stage VU1 frustum clipping (2026-10-26)

## Evidence / why
The Oct 26 screenshot shows cleaner fighter geometry from PR #29, but **the stage is still black**. The most recent log attached before PR #29, from PR #28, contains 2,000–2,400 "native stage VU1 ... conservative-fallbacks" per combat window, ~6,000–7,000 VU1 interpreter calls, 0–11 native stage batches. Those counters do not isolate black stage causality.

Before this change, \`Psp_Stage4Prepare\` returned 0 for the WHOLE strip if a single stage vertex crossed the 6-plane view volume, rerunning the VU1 interpreter for every polygon. Program 4 already has a software VU clipper and a third matrix at VU constants 8..11 for clip-to-screen projection (see \`docs/systems/vu1/prog4_prog5.md\`).

## Implementation
* For valid front-facing W, preserve the original fast native strip transform for wholly inside triangles.
* For strips containing offscreen vertices, preflight each triangle with the 6 PS2 clip planes, interpolate clip coordinates/colour/ST at their edges, reproject by the third VU matrix, triangulate the resulting polygon, and submit GS triangles natively.
* Entire strip is rejected BEFORE modifying GS state if any clipping preflight is invalid, negative or zero W, NaN, or unsupported case. The original VU1 interpreter remains authoritative.
* \`native_stage_clip=1\` enables; \`native_stage_clip=0\` restores old stage fastpath with whole-strip fallback; \`native_stage4=0\` disables native stage processing entirely. Does not change native fighter shader, PSP fonts, cache palette math, PS2 game logic, or game assets.
* Diagnostic counters \`stage native CPU clipping\`: triangles, wholly-outside, preflight-rejected.

## Limitations
This is a conservative PSP 2D GU geometry path. It does not magically implement GS depth-as-texture, multi-FBP composition, all VU perspective-correct fill or the original program4 0.5% fan expansion. A black background after this fix must be investigated independently via framebuffer/render-state tracing; avoid claiming resolved scenery. The only recent visual result verified by the user is improved character rendering in PR #29.

## Validation
CI runs host clip tests and compiles PSP EBOOT. Compare same PPSSPP arena with PR #29 vs PR #30: stage screen, real FPS, Goku geometry, UI/font colours and VU1 fallback counts. Optionally revert \`native_stage_clip=0\` while keeping all other settings unchanged.
