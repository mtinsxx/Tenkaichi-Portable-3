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
                                  tbp=336,tbw=8,gen=0x12345678,output=None))
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
print("Offline PSP T8/5551, mesh and animation preparation tests passed")
