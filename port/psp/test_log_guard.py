#!/usr/bin/env python3
"""Source guard for diagnostic capture; the PSP SDK validates linking."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
main=(root/"port/psp/psp_main.c").read_text()
gu=(root/"port/psp/psp_gu.c").read_text()
assert "sceKernelCreateSema" in main
assert "sceKernelWaitSema" in main
assert "sceKernelSignalSema" in main
assert "Psp_LogAcquire();" in main
assert "Psp_LogRelease(held);" in main
assert "TP3-GE-PIXELS-LOGGUARD-20261027" in main
assert "sPixelProbeScreen=a;" in gu
assert "sPixelProbeSampleCount=0;" in gu
assert "LOG_ALL GPU-PROBE last-post-GE" in gu
assert "LOG_ALL PIXELS post-GE" in gu
assert "int gPspLogAll = 1;" in main
assert "read_settings();" not in main
print("PSP log concurrent-writer guard + periodic post-GE pixel snapshot report passed")
