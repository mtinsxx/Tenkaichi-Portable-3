# PSP GE eDRAM alias correction — 2026-10-28

## Repro and scope
The uploaded battle log from build `TP3-STAGE399-NATIVE-EMPTY-20261028` still contains indexed framebuffer feedback rejections (GS PSMT8 0x13 reading framebuffer data), zero stage-to-fighter copies (primarily same-surface rejects), and ~1–7 actual battle GS FPS. These are independent remaining problems.

This narrowly scoped test corrects an address-domain mismatch in `port/psp/psp_gu.c`: CPU reads/writes use uncached `0x44000000` eDRAM, but GE commands must receive the physical `0x04000000` VRAM addresses. PSP SDK's `sceGuCopyImage` encodes pointer high bits directly into transfer registers, so the CPU-only uncached high bit should not be passed through. Keep RAM pointers (including decoded textures and framebuffer snapshots) unchanged.

Changed operations:
- GE copy source of coherent framebuffer snapshots
- GE stage-to-fighter copy source **and** destination
- GU texture source, including GPU-backed RGB5551 textures
- GU CLUT source when allocated in eDRAM

Unit test: `port/psp/test_vram_ge_pointer.c`; the regular `psp` Actions workflow runs it before building `EBOOT.PBP`. Existing white glyph treatment (`fe93677`), models, native VU1, and input mappings are unchanged.

## Verification limits
A green compiler or host-test result does not establish that the flickering visuals or 1–7 FPS have been fixed. The current PSP renderer still lacks faithful GS PSMCT32 -> PSMT8 cross-format framebuffer feedback, exact GPU-to-GS-VRAM readback, and some native post-effects. The stage-to-fighter copy is not supposed to run if the stage and fighter already occupy the **same physical target**, so `copied=0` alone does not prove an error.

Install only the Action artifact for this branch and compare the same PPSSPP fight. Confirm build marker `TP3-GE-VRAM-ADDR-20261028`, capture images, FPS, and `LOG_ALL PIXELS`, `GS framebuffer virtualization`, `cross-PSM aliases rejected`, and `fb-alias` statistics. No game ISO, ELF or assets are included in this change.
