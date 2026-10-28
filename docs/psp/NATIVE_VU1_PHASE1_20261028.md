# PSP-native VU1 migration — phase 1: upload-time microcode decoding

## Objective
Replace repeated PS2 VU1 ISA decoding in the running game with predecoded
fixed-size MIPS-friendly native fields while **retaining** the exact original
semantics of VU1 register operations, XGKICK, CLIP pipeline delays,
branch-delay slot behavior and Q latency. Future phases can translate
frequently executed stage399 basic blocks to native routines and compare
with this unchanged execution reference.

## Latest observed baseline
The user-provided log from TP3-SNAPSHOT-TEX-CACHE-20261028 showed
approximately 1 FPS in several battles, and stage399's residual VU1
interpreter took an estimated 739 ms in one three-frame interval.
The game also spent over a second in repeated GS texture decoding,
so changing micro-op decoding cannot fix rendering artifacts or FPS alone.

## Implementation
- VIF MPG upload writes the authoritative `vu.micro[]` bytes exactly as
  before. Immediately predecode the touched 64-bit instruction pair,
  including partial single-word writes, into a PSP-only 2048-slot
  `PspVu1Instruction` table (max 48 KiB).
- Interpreter run() loads `lo/hi` from the predecoded representation;
  shifts for upper and lower register fields, opcode and immediate
  fields are not repeated in its per-instruction hot loop.
- All existing VU1 arithmetic, flags, CLIP, delay slots, Q scheduling
  and GIF submission are UNCHANGED. A rejected native vertex batch
  continues through the original interpreter path.
- New log counter: `psp: VU1 uploaded micro-op predecode` shows
  uploaded words and interpreted instructions for actual battle
  comparison. Build ID `TP3-PSP-NATIVE-VU1-DECODE-20261028`.
- Both PSP workflows include bit-exact decoder tests with 100,000
  randomized word pairs and 12,000 partial/wrapping MPG sequences.
  This is source-native bytecode **predecoding**, not complete PS2→PSP
  static recompilation, and not a performance guarantee.

## Next engineering stages (not completed)
1. Measure the same battle interval against the previous EBOOT.
   If predecoded VU1 is slower, revert this phase before attempting
   aggressive instruction-level optimization.
2. Identify the exact 399-instruction fallback path(s), capture their
   inputs/outputs and build a differential-testable PSP native micro-block.
3. Preserve GPU-produced framebuffer pixels when the GS treats them as
   textures; implement missing compatible GPU texture/CLUT operations,
   avoiding PS2 framebuffer reinterpretation aliases.
4. Once runtime GS correctness is established, prepare authorized
   game assets offline for PSP-native geometry and PSP-compatible
   texture/animation buffers, without adding game data/ISO to GitHub.

## Testing caveat
GitHub Actions compiling an EBOOT proves the PSP SDK accepts this code,
not that 30/60 FPS or correct battle graphics have been achieved.
