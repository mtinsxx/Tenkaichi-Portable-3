#!/usr/bin/env python3
"""Static PSP boot-screen regression test (no game data, PPSSPP or SDK).

Proves the code clears both buffers before starting GU, does not erase
live frames afterwards, and keeps readable fatal screens. This cannot
prove that all framebuffer-composition bugs in battle are fixed.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GU = (ROOT / "port/psp/psp_gu.c").read_text()
MAIN = (ROOT / "port/psp/psp_main.c").read_text()
MEM = (ROOT / "port/psp/psp_mem.c").read_text()

def check(condition, description):
    if not condition:
        raise AssertionError(description)
    print("OK:", description)

def main():
    init = GU.split("int Psp_GuInit(void) {", 1)[1].split("\n}", 1)[0]
    live = GU.split("void GsGpu_FrameEnd(void) {", 1)[1].split("\n}", 1)[0]
    init_gu = init.index("sceGuInit();")

    for region, size in [("FB0_OFF", "FB_SIZE"),
                         ("FB1_OFF", "FB_SIZE"),
                         ("WORK_OFF", "WORK_SIZE")]:
        snippet = f"memset(sVram + {region}, 0, {size});"
        check(snippet in init and init.index(snippet) < init_gu,
              f"{region} cleared once before GU starts")
        check(snippet not in live,
              f"{region} never erased while presenting a frame")

    check('#error "PSP GU frame/depth/work regions overlap"' in GU,
          "overlapping VRAM regions forbidden at compile time")
    check('#error "PSP GU texture region is outside physical VRAM"' in GU,
          "2 MiB VRAM limit checked at compile time")

    boot = MAIN.split("int __wrap_main(int argc, char **argv) {", 1)[1]
    check(boot.index("Psp_LoadGameData(gPspDisc);") <
          boot.index("pspDebugScreenClear();") <
          boot.index("if (!Psp_GuInit())"),
          "boot text dismissed only after loading the game's data")
    check("void Psp_Fatal(const char *fmt, ...)" in MAIN and
          'pspDebugScreenPrintf("\\n%s\\n\\nPress START' in MAIN,
          "fatal errors remain visible and require START to quit")
    check("PSP memory too low for 1.5 MB game thread" in MAIN and
          "cannot START the game's thread" in MAIN and
          "(unsigned)th" in MAIN and "(unsigned)started" in MAIN,
          "thread create OOM and start failures report their actual values")
    check("No bt3.txt is required" in MEM,
          "missing-disc error does not wrongly require an optional file")
    print("PSP startup and diagnostics regression checks passed")

if __name__ == "__main__":
    main()
