#!/usr/bin/env python3
"""PSP VU1 debug env is constant-folded, while PC/Vita keep switches."""
from pathlib import Path
source=(Path(__file__).resolve().parents[2]/"port/src/gs/gs_vu1.c").read_text()
assert "#ifdef PORT_PSP" in source
assert "#define TP3_VU_ENV(name) ((const char *)0)" in source
assert "#define TP3_VU_ENV(n) getenv(n)" in source
assert 'getenv("BT3_' not in source
for name in ("BT3_VU_DUMP","BT3_VU_CALLS","BT3_VU_INTERP","BT3_GS_VERBOSE"):
    assert f'TP3_VU_ENV("{name}")' in source
assert "run(addr < 0 ? vu.pc : (uint32_t)addr);" in source
assert "GsGpu_DrawVu4" in source and "GsGpu_DrawVu0" in source
print("PSP VU dispatcher has no runtime getenv in hot path; interpreter and host diagnostics retained")
