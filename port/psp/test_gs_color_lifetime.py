#!/usr/bin/env python3
"""Guard PS2-owned color clearing vs PSP frame-start depth behavior."""
from pathlib import Path
root=Path(__file__).resolve().parents[2]
gu=(root/"port/psp/psp_gu.c").read_text()
sony=(root/"port/src/gs/gs_sony.c").read_text()
start=gu.index("int Psp_GuInit(void)")
frame=gu[gu.index("void GsGpu_FrameEnd(void)"):start]
startup=gu[start:]
assert "sceGuClear(GU_DEPTH_BUFFER_BIT);" in frame
assert "sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);" not in frame
assert "sceGuClearColor(0);" not in frame
assert 'memset(sVram + FB0_OFF, 0, FB_SIZE);' in startup
assert 'memset(sVram + FB1_OFF, 0, FB_SIZE);' in startup
assert "sceGsSwapDBuff(void *p, int id)" in sony
assert "set(&c->xyz2a" in sony and "set(&c->xyz2b" in sony
assert "return sceGsPutDrawEnv" in sony
assert "int gPspGsDepth = 2;" in (root/"port/psp/psp_main.c").read_text()
print("GS owns screen colour clear; PSP owns depth-only frame-start clear")
