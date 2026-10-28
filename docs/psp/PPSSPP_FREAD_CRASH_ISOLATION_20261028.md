# PPSSPP _fread_r crash isolation – 2026-10-28

## Incident
User's PPSSPP screenshot after PR #3 GE screen-coherence experiment:
`Invalid Memory Access`, write to `0x00000000`, PC `08A4FD80`,
function `_fread_r`, current thread `bt3_game`, PPSSPP 1.20.4
(release JIT), Android. The embedded executable's ELF symbol table
confirmed `_fread_r` at `0x08A4FC94`; fault location near +0xec.

The null address is not evidence of a framebuffer clear or of a
missing specific file. It may be a NULL `FILE*`, invalid destination,
or earlier memory corruption. The archived user log from an older
run is not enough to identify which.

## Risk containment
- Base this recovery experiment on the previously built PR #2 commit
  `858576cd51f6e1654d925a5945894999d93c679d`.
  **No** PR #3 screen-owner/GE-restore modifications are present.
  Do not merge the crash-associated change before runtime validation.
- Link-level `--wrap=fread`: before entering PSP newlib, reject
  nonempty reads with NULL stream, NULL destination or count overflow.
  Log both pointers and the caller address to `log.txt`, then display
  a controlled fatal screen rather than letting PPSSPP report a raw
  illegal address. Valid reads are forwarded unchanged.
- Link-level `--wrap=fopen`: log the first 16 failed file paths
  and error numbers. A missing optional settings/save file is not
  automatically fatal.
- Host tests validate zero-sized reads, valid reads, NULL operands and
  integer multiplication overflow. Both PSP CI workflows build EBOOT.

## How to interpret a fresh test
- If the old EBOOT crashes and this build reaches the same battle,
  PR #3 screen ownership/restoration caused a regression or exposed
  an existing memory problem. We can bisect its changes without
  overwriting the user's working PSP build.
- If the diagnostic build displays `invalid file read`, inspect
  `psp: IO-CRASH-GUARD ...` and preceding `psp: IO fopen failed`
  lines in its **new** `log.txt`. Do not assume a missing file
  if a preceding failed fopen is unrelated.
- If PPSSPP still hard-crashes without the guard firing, the write
  is occurring inside a code path not reached through linked `fread`
  references, or memory was corrupted before the call. A crash log
  with the new build ID and PC is needed.

This is a **crash investigation build**, not an unverified promise
that the background, VU1 FPS, or GS readbacks are fixed.
