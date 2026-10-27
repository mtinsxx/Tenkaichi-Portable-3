# PSP / PPSSPP: cannot CREATE the game's thread (0x80020190)

Incident: PPSSPP Android screenshot, 25 Oct 2026:
\`\`\`
cannot CREATE the game's thread (error 0x80020190)
Press START to quit.
\`\`\`
This was reproduced by the user on the experimental GS PR #17 and
fighter VU1 PR #18 (not evidence that either VU1 or GS code executed).

## Confirmed meaning

\`0x80020190 = SCE_KERNEL_ERROR_NO_MEMORY\`, per PSPSDK and PPSSPP.
The PSP kernel failed to reserve the game thread's stack during
\`sceKernelCreateThread\`. This is a PSP virtual memory/user partition
problem; Android's available RAM and the PPSSPP FPS counter do not
prove the game has any free PSP thread-stack address space.

## Code condition before this change

\`port/psp/psp_main.c\`:
- requested a **42 MiB** newlib heap at PSP startup;
- loaded/mapped tables from the player's own game files;
- allocated **29 MiB PS2-compatible game heap** through malloc;
- initialized the PSP GU and associated allocations;
- only then requested the \`bt3_game\` thread stack of **1.5 MiB**.

That late allocation can fail due to partition pressure or
fragmentation. The kernel error proves OOM, but before logging
free-memory values we cannot prove the exact split.

## Fix on this branch

1. Reduce only PSP-newlib reserved pool from 42 MiB to **40 MiB**,
   leaving more system partition capacity for kernel threads. PSP
   \`PARAM.SFO\` still requests \`MEMSIZE=1\` for 64 MiB mode.
2. **Keep the 29 MiB game heap and 1.5 MiB game thread stack
   unchanged.** Shortening these arbitrarily could corrupt PS2
   pointers or overflow the game stack.
3. Reserve \`bt3_game\` thread (dormant) **before**
   \`Psp_LoadGameData\`, \`Psp_MapMemory\` and \`Psp_GuInit\`.
   Start it only when the program data, memory and GPU are ready.
4. Emit logs \`psp: thread memory before-create\`, \`reserved\`
   and \`before-start\`, using the available PSP sysmem partition
   totals/largest block. These diagnostics are not necessarily
   equal to the newlib heap's own remaining bytes.
5. If the thread still cannot be created, show a specific
   memory-mode error and refer to \`log.txt\`. No \`bt3.txt\`
   needed.
6. A static regression test \`test_thread_memory.py\` verifies
   ordering and memory constants, executed before PSP EBOOT
   cross-compilation.

No changes in native animation, bones, lighting, texture handling,
GS framebuffer, controls, or game assets.

## PPSSPP Android validation (user has no PC)

1. Back up your previous EBOOT (especially last one that opened
   the game), install the EBOOT built from this experimental PR.
2. Fully close PPSSPP before relaunching so its PSP kernel session
   is initialized fresh.
3. Use PSP-2000/3000/Go or a PPSSPP setting that enables 64 MiB
   of PSP RAM. **A PSP-1000 32 MiB environment is unsupported**
   by the game's unchanged 29 MiB heap and native code.
4. Boot; if \`0x80020190\` is gone, inspect gameplay next.
5. For failure, send \`PSP/GAME/BT3/log.txt\` and the latest
   screenshot showing whether loading, thread reserve or start
   failed. Memory diagnostics allow targeted follow-up.

**CI verifies code/compilation, not whether a particular PPSSPP
memory map can fit the 40 MiB pool and all graphics/stacks.**
If OOM persists, compare the three checkpoints to adjust the
actual memory budget without breaking the game.

Main \`fe93677\` remains untouched. No proprietary game data added.
