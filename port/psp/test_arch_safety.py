#!/usr/bin/env python3
"""Architecture sanity checks for the PSP native BT3 port.

These are source-level invariants, not a renderer/gameplay emulator.
They protect against accidentally enabling no-op VU1 native paths,
silently accepting missing direct PS2 calls, and losing traceability of
unimplemented GS passes or truncated audio.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def load(name):
    return (ROOT / name).read_text()

def require(ok, what):
    if not ok:
        raise AssertionError(what)
    print("OK:", what)

def main():
    gfx = load("port/psp/psp_gfx.c")
    gu = load("port/psp/psp_gu.c")
    build = load("port/psp/build.py")
    snd = load("port/psp/psp_audio.c")
    vu = load("port/src/gs/gs_vu1.c")
    stubs = [x.strip() for x in
             load("port/psp/expected_stubs.txt").splitlines()
             if x.strip() and not x.lstrip().startswith("#")]

    require('strcmp(name, "BT3_VU_INTERP")' in gfx and
            'return "1"; /* keep interpreter ON' in gfx,
            "VU1 interpreter cannot be disabled while PSP native VU draws are empty")
    require('void GsGpu_DrawVu0(' in gu and
            'void GsGpu_DrawVu4(' in gu and
            'void GsGpu_DrawVu6(' in gu,
            "VU drawing entry points remain explicitly tracked")
    require("static unsigned sUnknown[2][128]" in vu,
            "unknown VU instruction diagnostic exists")
    require("if direct:" in build and
            "new unresolved functions not in expected_stubs.txt" in build,
            "no new direct calls to missing PS2 APIs may pass a build")
    require(len(stubs) == 102 and len(set(stubs)) == len(stubs),
            "102 unresolved PS2 callback pointers remain in reviewed manifest")
    require("sDiagSkip16" in gu and "sDiagSkipDepth" in gu and
            "sFbWorkOwnerChanges" in gu,
            "unimplemented GS frame/depth passes are counted")
    require("sDiagUnimplementedFbUpload" in gu and
            "sDiagUnimplementedNativeEffect" in gu,
            "GS native/fb readback no-ops are reported, not claimed complete")
    allocation = gu.split("t = &sTex[sTexCount++];", 1)[1].split(
        "t->tex0 = t0;", 1)[0]
    require("++sTexSerial" not in allocation and
            "Psp_TexReuseRemember" in allocation and
            "++sTexSerial" in gu.split("static int tex_evict_one", 1)[1],
            "cache append preserves indices; eviction invalidates hot descriptors")
    require("PspStage4_TransformClipReady(o, p, p + 4, p + 8" in gu and
            "clip->clip" in gu,
            "native stage reuses already validated clip coordinates")
    require("if (name == NULL) return NULL;" in gfx and
            "if (name[4]" not in gfx.split("char *Psp_GsGetenv(", 1)[1].split("\n}", 1)[0],
            "environment lookup handles short/null names")
    require("gPspAudioOverflowFrames" in snd and
            "return i == frames;" in snd,
            "audio FIFO overflow cannot report complete success")
    for api in ("SDL_SetAudioStreamGetCallback", "SDL_ClearAudioStream",
                "SDL_DestroyAudioStream", "SDL_LockAudioStream"):
        body = snd.split(api + "(", 1)[1].split("\n}", 1)[0]
        require("s == NULL || !sMutexReady" in body,
                api + " guards null/uninitialized streams")
    print("PSP architecture safety source checks passed")
    print("NOT a claim of 100% PS2-to-PSP conversion, correct pixels, or FPS")

if __name__ == "__main__":
    main()
