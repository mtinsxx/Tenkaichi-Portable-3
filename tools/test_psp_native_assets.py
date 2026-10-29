#!/usr/bin/env python3
import json
import tempfile
import struct
from pathlib import Path
from argparse import Namespace
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import psp_native_assets as conv

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    src = root / "test.idx"
    src.write_bytes(bytes(range(16)))
    out = conv.write_t8(Namespace(width=4,height=4,source=str(src),
                                  tbp=336,tbw=8,gen=0x12345678,output=root / "t8_0150_12345678_4x4.tp8"))
    assert out.name == "t8_0150_12345678_4x4.tp8"
    header = out.read_bytes()
    assert len(header) == 32+16
    assert conv.T8_HDR.unpack(header[:32]) == (
        b"T8P1",1,336,8,0x12345678,4,4,conv.fnv(src.read_bytes()))
    assert header[32:] == src.read_bytes()
    rgba = root / "test.rgba"
    rgba.write_bytes(bytes([255,0,0,255,0,255,0,0]))
    result = root / "test.5551"
    conv.write_rgba5551(Namespace(source=str(rgba),width=2,height=1,output=result))
    assert result.read_bytes() == struct.pack("<HH",0x801f,0x03e0)
    mesh = root / "mesh.json"
    mesh.write_text(json.dumps({"vertices":[{"position":[0,0,0],
                          "uv":[0,0],"rgba":[255,255,255,255]}],
                          "indices":[0,0,0]}))
    packed = root / "mesh.m3p"
    conv.write_mesh(Namespace(source=str(mesh),output=packed))
    assert packed.read_bytes()[:4] == b"M3P1"
    anim = root / "anim.json"
    anim.write_text(json.dumps({"frames":[[[0]*12]]}))
    packed = root / "anim.a3p"
    conv.write_animation(Namespace(source=str(anim),output=packed))
    assert packed.read_bytes()[:4] == b"A3P1"
    try:
        rgba.write_bytes(bytes([0,0,0,127,0,0,0,255]))
        conv.write_rgba5551(Namespace(source=str(rgba),width=2,height=1,output=result))
        raise AssertionError("partial alpha unexpectedly converted")
    except ValueError:
        pass
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    source = root / "expanded_gs.rgba"
    # GS units: opaque 0x80 and 0xff (both pack to 1-bit alpha),
    # transparent 0x00; retain full exact colour components.
    source.write_bytes(bytes([255, 0, 0, 128,
                              0, 255, 0, 0,
                              0, 0, 255, 255,
                              123, 44, 9, 128]))
    args = Namespace(source=str(source), output=None, width=2, height=2,
                     tbp=336, tbw=8, psm=0, gen=0x12345678,
                     texa=0x1020)
    import os
    os.chdir(root)
    out = conv.write_color(args)
    assert out.name == "c_0150_12345678_00_2x2_0000000000001020.n4p"
    raw = out.read_bytes()
    assert conv.COLOR_HDR.size == 52
    h = conv.COLOR_HDR.unpack(raw[:52])
    assert h[:9] == (b"N4P1", 1, 336, 8, 0, 0x12345678, 2, 2, 0x1020)
    assert h[9:11] == (0, 1)
    assert raw[52:] == struct.pack("<4H", 0x801f, 0x03e0,
                                    0xfc00, (123 >> 3) |
                                    ((44 >> 3) << 5) |
                                    ((9 >> 3) << 10) | 0x8000)
    assert conv.fnv(raw[52:]) == h[-1]
    # Intermediate PS2 alpha cannot be quantized to RGBA5551;
    # conversion must preserve it in GU8888 after alpha_scale().
    source.write_bytes(bytes([17, 30, 63, 64] * 4))
    packed = conv.write_color(args).read_bytes()
    h = conv.COLOR_HDR.unpack(packed[:52])
    assert h[-2] == 2
    assert packed[52:] == bytes([17, 30, 63, 128] * 4)
    # Incorrect GS PSM is refused rather than guessing GPU layout.
    args.psm=0x30
    try:
        conv.write_color(args)
        raise AssertionError("depth texture erroneously packaged as color")
    except ValueError:
        pass
print("Offline PSP T8/5551, GU color pack, mesh and animation preparation tests passed")
