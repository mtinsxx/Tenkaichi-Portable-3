#!/usr/bin/env python3
"""Regression for the native PSP PSMT8 converter, with synthetic data only."""
import importlib.util
import pathlib
import struct
import subprocess
import sys
import tempfile

MODULE = pathlib.Path(__file__).parent / "tools" / "convert_t8.py"
spec = importlib.util.spec_from_file_location("psp_t8_converter", MODULE)
assert spec and spec.loader
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def run() -> None:
    indices = bytes((x * 41 + 13) & 255 for x in range(64 * 32))
    assert mod.HEADER.size == 32
    assert mod.fnv32(bytes([0, 1, 2, 3])) == 0xC3AA51B1
    payload = mod.build_pack(indices, 336, 8, 0xA1B2C3D4, 64, 32)
    fields, raw = mod.parse_pack(payload)
    assert raw == indices
    assert fields == dict(tbp=336, tbw=8, index_gen=0xA1B2C3D4,
                          width=64, height=32, fnv=mod.fnv32(indices))
    assert mod.filename(336, 0xA1B2C3D4, 64, 32) == (
        "t8_0150_a1b2c3d4_64x32.tp8")
    try:
        mod.build_pack(indices[:-1], 336, 8, 0xA1B2C3D4, 64, 32)
    except ValueError:
        pass
    else:
        raise AssertionError("short source indices were accepted")
    for wrong in (payload[:-1], payload + b"\0", payload[:3] + b"X" + payload[4:]):
        try:
            mod.parse_pack(wrong)
        except ValueError:
            continue
        raise AssertionError("bad pack was accepted")
    with tempfile.TemporaryDirectory() as temp:
        root = pathlib.Path(temp)
        source = root / "source.raw"
        source.write_bytes(indices)
        out = root / "native"
        cmd = [sys.executable, str(MODULE), "pack",
               "--indices", str(source), "--tbp", "336",
               "--tbw", "8", "--index-gen", "0xa1b2c3d4",
               "--width", "64", "--height", "32", "--out", str(out)]
        subprocess.run(cmd, check=True, stdout=subprocess.PIPE, text=True)
        pack = out / mod.filename(336, 0xA1B2C3D4, 64, 32)
        assert pack.read_bytes() == payload
        subprocess.run([sys.executable, str(MODULE), "verify", str(pack)],
                       check=True, stdout=subprocess.PIPE, text=True)
        # A different source must not replace an existing pack with the
        # same GS source hash and FBP. Failed operation leaves it intact.
        source.write_bytes(bytes([1]) * len(indices))
        result = subprocess.run(cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
        assert result.returncode != 0
        assert pack.read_bytes() == payload


if __name__ == "__main__":
    run()
    print("PSP native PSMT8 pack conversion/verification tests passed")
