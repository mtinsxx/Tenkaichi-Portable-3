# PSP native translation — VIF1 UNPACK, 8-bit and 16-bit vectors

## Intent

Translate one real piece of the **PS2 VIF1 packet interpreter** into
CPU-native MIPS Allegrex operations, without disabling the VU1
microprogram interpreter or breaking original GS/VU1 behavior.

Source/port: `port/src/gs/gs_vu1.c` (`GsVu1_Unpack`).
PSP code: `port/psp/psp_vif_subword.h`.
Differential test: `port/psp/test_vif_subword.c`.

This branch is based on `audit/psp-architecture-20261025`, after
PR #14, not `main`; the known-good `fe93677` is unchanged.

## The exactly supported subset

- VIF `UNPACK` V1/V2/V3/V4 **16-bit** (`VL=1`) and **8-bit**
  (`VL=2`), signed and unsigned (`USN`).
- Unmasked (`M=0`), no VIF row arithmetic (`MODE=0`),
  `WL <= CL` cycles (regular copies with possible address gaps).
- Destination VU memory remains the original 16 KiB buffer.
- Addresses wrap modulo 1024 16-byte VU vectors.
- Source is read byte/halfword at a time, sign extension replicates
  the reference's 32-bit output. V1 copies its lane to all four
  lanes; V2/V3 set remaining lanes to zero.
- No dependence on alignment of the packet's input address.

Everything else remains on the original generic interpreter:
mask selectors, VIF row arithmetic, fill cycles where `WL > CL`,
`VL=3` packed 5-bit vectors, and any unknown future cases.

The established 32-bit PSP fast path is unchanged. New subword
vectors are counted in existing `fast-unpack` *and* separately
under `native-8/16`.

## Validation

The host test independently executes the generic C reference for
all 16 Vn/VL/USN combinations, CL 1..6, all valid WL, six VU
start addresses including wraparound, and seven packet lengths,
including 256 vectors. It compares the **entire** VU data memory
before/after, including untouched gaps and sentinel bytes.

Both PSP CI workflows must pass those differential checks and
produce an EBOOT without new missing imports. This is evidence of
arithmetic and memory equivalence **for the supported cases only**,
not of full game equivalence.

### Compare on Android PPSSPP

No `bt3.txt` is necessary; the native VIF subword subset is enabled
because differential tests found it equivalent to the reference.

1. Keep a backup of the previous experimental EBOOT.
2. In PPSSPP with Memory Stick enabled, use the same fighter and
   stage where the game already reaches battle.
3. Compare HUD, scenery, character limbs, transitions and attacks.
4. Compare the line `psp: VIF/VU1: fast-unpack=... (native-8/16=...)`
   in `PSP/GAME/BT3/log.txt` and frame timing to the previous EBOOT.
5. If native-8/16 stays zero in that scene, it **cannot explain an
   FPS change there**.
6. Do not merge as stable before PPSSPP and real PSP 64 MB tests.

## What is still PS2-dependent

- VU1 `MSCAL`/microprograms are **still interpreted** instruction
  by instruction. Their rendering is not translated to native GU
  primitives; `GsGpu_DrawVu0/4/6` remain no-op and cannot be enabled.
- GS framebuffer passes, FB16/depth sampling, multi-target surfaces
  and readback are still incomplete.
- CPU PS2 argument evaluation order `r2l` is not strictly enforced
  on missing-libclang CI hosts.
- PS2 SPU/IOP middleware calls, heap semantics and 102 legacy
  callbacks are still handled as documented in architecture audit.

A safe native port progresses by equivalence-tested components,
not by removing PS2 behavior that the game requires.
