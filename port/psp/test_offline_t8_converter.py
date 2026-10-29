#!/usr/bin/env python3
"""Fixture-only native PSMT8 pack validation, no game files required."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

TOOL = Path(__file__).resolve().parents[1] / "tools" / "psp_t8_convert.py"
spec = importlib.util.spec_from_file_location("psp_t8_convert", TOOL)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def run():
    assert mod.VRAM_BYTES == 4194304
    assert mod.fnv32(bytes((0, 1, 2, 3))) == 0xc3aa51b1
    assert mod.K_COL8_FIRST[0][:4] == (0, 4, 16, 20)
    data = bytearray(mod.VRAM_BYTES)
    # Fill block 0, several rows, and page 336 with recognizable bytes.
    # The exact GS swizzle is NOT a flat raster: compare to a known
    # first four horizontal texels in GS block 0.
    for i in range(256):
        data[i] = i & 255
    data[336 * 8192 : 336 * 8192 + 256] = bytes(range(256))
    linear = mod.decode_psmt8(bytes(data), 0, 8, 16, 16)
    assert linear[:4] == bytes((0, 4, 16, 20))
    assert linear[16:20] == bytes((8, 12, 24, 28))
    assert linear[32:36] == bytes((33, 37, 49, 53))
    g = mod.source_generation(bytes(data), 336 * 32, 8, 16)
    assert isinstance(g, int) and 0 <= g <= 0xffffffff
    with tempfile.TemporaryDirectory() as td:
        root = Path(td) / "native"
        p = mod.convert(bytes(data), None, 336 * 32, 8, 16, 16, None, root)
        b = p.read_bytes()
        assert p.name == f"t8_2a00_{g:08x}_16x16.tp8"
        assert b[:4] == b"T8P1" and len(b) == 32 + 16 * 16
        magic, version, tbp, tbw, gen, w, h, checksum = struct.unpack("<4s7I", b[:32])
        assert (magic, version, tbp, tbw, gen, w, h) == (b"T8P1", 1, 336 * 32, 8, g, 16, 16)
        assert checksum == mod.fnv32(b[32:])
        assert (root / "enable.txt").is_file()
        assert p == mod.convert(bytes(data), None, 336 * 32, 8, 16, 16, None, root)
        try:
            mod.convert(bytes(data), None, 336 * 32, 8, 16, 16, g ^ 1, root)
        except ValueError:
            pass
        else:
            raise AssertionError("Mismatched exact GS content hash must fail")
        # Test consuming already decoded index planes via the same
        # standard Python CLI that a user runs on their own dump.
        inp = Path(td) / "owner_indices.bin"
        inp.write_bytes(linear)
        dst = Path(td) / "linear"
        result = subprocess.run([
            sys.executable, str(TOOL), "--indices", str(inp),
            "--tbp", "0x2a00", "--tbw", "8", "--width", "16",
            "--height", "16", "--gen", f"{g:08x}",
            "--out-root", str(dst)
        ], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert (dst / p.name).read_bytes() == b
        corrupt = bytearray(b)
        corrupt[-1] ^= 1
        p.write_bytes(corrupt)
        try:
            mod.convert(bytes(data), None, 336 * 32, 8, 16, 16, None, root)
        except ValueError:
            pass
        else:
            raise AssertionError("Converter must not overwrite conflicting pack")
    print("PSP T8P1 offline GS swizzle/hash/CLI/native-pack tests passed")


if __name__ == "__main__":
    run()
