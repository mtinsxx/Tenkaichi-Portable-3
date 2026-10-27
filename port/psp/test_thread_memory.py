#!/usr/bin/env python3
"""Regression for PPSSPP 0x80020190: SCE_KERNEL_ERROR_NO_MEMORY.

Prevents the game thread's 1.5 MiB stack from being allocated only
after a 29 MiB PS2 game heap, ELF buffers and GU allocations. Also
guarantees the native Allegrex/VU1 work stays unchanged by this fix.
Static audit, not a simulation of PPSSPP memory fragmentation.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "port/psp/psp_main.c").read_text()
MEM = (ROOT / "port/psp/psp_mem.c").read_text()
BUILD = (ROOT / "port/psp/build.py").read_text()

def verify(test, message):
    if not test:
        raise AssertionError(message)
    print("OK:", message)

def main():
    verify("PSP_HEAP_SIZE_KB(40 * 1024)" in MAIN,
           "newlib heap no longer reserves 42 MiB")
    verify("#define PSP_GAME_HEAP_MB 29" in MAIN
           and "#define PSP_HEAP_MB 29" in MEM,
           "PS2 game's mandatory 29 MiB block stays unchanged")
    verify("#define GAME_STACK 0x00180000" in MAIN,
           "game stack still 1.5 MiB: never mask OOM with unsafe truncation")
    verify('"MEMSIZE=1"' in BUILD,
           "PSP 64 MiB MEMSIZE=1 flag is retained in PARAM.SFO")
    start = MAIN.split("int __wrap_main(int argc, char **argv) {", 1)[1]
    create = start.index('sceKernelCreateThread("bt3_game"')
    loading = start.index("Psp_LoadGameData(gPspDisc);")
    heap = start.index("Psp_MapMemory();")
    graphics = start.index("Psp_GuInit()")
    run = start.index("sceKernelStartThread(th, 0, NULL)")
    verify(create < loading < heap < graphics < run,
           "game stack is reserved before reading tables, allocating heap, or starting GU")
    verify(start.count('sceKernelCreateThread("bt3_game"') == 1,
           "game thread is created exactly once")
    verify('if (th < 0)' in start[create:loading],
           "thread creation errors are checked immediately")
    verify('log_thread_memory("before-create")' in start
           and 'log_thread_memory("reserved")' in start
           and 'log_thread_memory("before-start")' in start,
           "memory checkpoints are logged for real PPSSPP tests")
    verify("sceKernelTotalFreeMemSize()" in MAIN
           and "sceKernelMaxFreeMemSize()" in MAIN,
           "diagnostics identify kernel memory pressure and block fragmentation")
    verify("int gPspLogAll = 1;" in MAIN
           and "read_settings();" not in MAIN
           and 'fopen(HOME "/bt3.txt"' not in MAIN
           and "static void setting(" not in MAIN,
           "native EBOOT collects diagnostics without opening bt3.txt")
    print("PSP game-thread OOM startup ordering checks passed")
    print("Runtime confirmation requires PPSSPP Android and its log.txt")

if __name__ == "__main__":
    main()
