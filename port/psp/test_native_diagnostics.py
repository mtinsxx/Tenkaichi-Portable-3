#!/usr/bin/env python3
"""Ensure the PSP diagnostic build never uses external bt3.txt settings."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
main=(root/"port/psp/psp_main.c").read_text()
gfx=(root/"port/psp/psp_gfx.c").read_text()
wf=(root/".github/workflows/portable-3-test.yml").read_text()
assert "int gPspLogAll = 1;" in main
assert "int gPspReport = 1;" in gfx
assert "int sLogFlush = 0;" in main
assert "int gPspGsDepth = 2;" in main
assert "int gPspControlsProfile = PSP_PROFILE_DEFAULT;" in gfx
assert 'fopen(HOME "/bt3.txt"' not in main
assert "read_settings();" not in main
assert "static void setting(" not in main
assert "> dist/PSP/GAME/BT3/bt3.txt" not in wf
assert "TP3-GE-PIXELS-LOGGUARD-20261027" in main
print("EBOOT embedded LOG_ALL, portable controls, depth=2; no bt3.txt dependency")
