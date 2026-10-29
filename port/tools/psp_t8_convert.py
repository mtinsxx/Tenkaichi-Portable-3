#!/usr/bin/env python3
"""Convert an owner's PS2 GS memory snapshot or linear PSMT8 indices into
verified Tenkaichi Portable 3 native packs (T8P1).

No ROM, ISO, copyrighted game data, or converted resources ship in the repo.
The PSP's existing texture reader matches source *content hashes*, validates
each pack, and verifies samples against current emulated GS memory.

Examples:
  python3 port/tools/psp_t8_convert.py --vram own-gs-vram.bin \
    --tbp 336 --tbw 8 --width 256 --height 256 --out-root psp-native
  python3 port/tools/psp_t8_convert.py --indices own-index-plane.bin \
    --tbp 336 --tbw 8 --gen a1b2c3d4 --width 128 --height 128 \
    --out-root psp-native

The output directory contains the files to place in
ms0:/PSP/GAME/BT3/native/ . No external bt3.txt configuration.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct
import sys

VRAM_BYTES = 4 * 1024 * 1024
MASK = 0xFFFFFFFF
K_BLOCK32 = (
    (0, 1, 4, 5, 16, 17, 20, 21),
    (2, 3, 6, 7, 18, 19, 22, 23),
    (8, 9, 12, 13, 24, 25, 28, 29),
    (10, 11, 14, 15, 26, 27, 30, 31),
)
K_COL8_FIRST = (
    (0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54),
    (8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62),
    (33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23),
    (41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31),
)


def fnv32(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & MASK
    return h


def page_hash(data: bytes, page: int) -> int:
    if len(data) != VRAM_BYTES:
        raise ValueError("GS VRAM snapshot must contain exactly 4194304 bytes")
    start = (page & 511) * 8192
    h = 2166136261
    for (word,) in struct.iter_unpack("<I", data[start:start + 8192]):
        h = ((h ^ word) * 16777619) & MASK
        h ^= h >> 15
    return h


def source_generation(data: bytes, tbp: int, tbw: int, height: int) -> int:
    """Identical to texture_get()'s PSMT8 source-page key.

    gs_core.c Gs_PageHash and the loop inclusive of 'pages' are mirrored
    literally; the CLUT hash deliberately does NOT enter this key.
    """
    width_in_blocks = tbw or 1
    pages = ((width_in_blocks * 64 * height) + 8191) // 8192
    gen = 0
    for i in range(pages + 1):
        gen = ((gen ^ page_hash(data, tbp // 32 + i)) * 16777619 + i) & MASK
    return gen


def decode_psmt8(data: bytes, tbp: int, tbw: int, width: int, height: int) -> bytes:
    """Exact pixel layout from gs_core.c vram_rw() for PSM=0x13.

    GS block = 256 bytes, page = 32 blocks, and the same K_BLOCK32 and
    S_COL8 tables are used by the PSP runtime. The input is a raw GS 4 MiB
    byte image, NOT a normal RGBA picture or a 32-bit RGBA linear buffer.
    """
    if len(data) != VRAM_BYTES:
        raise ValueError("GS VRAM snapshot must contain exactly 4194304 bytes")
    if not 0 <= tbp < 16384 or not 0 <= tbw <= 63:
        raise ValueError("Invalid GS TBP/TBW")
    if not (1 <= width <= 512 and 1 <= height <= 512):
        raise ValueError("Only 1..512 texture dimensions are supported")
    bw = tbw or 1
    out = bytearray(width * height)
    for y in range(height):
        column = (y & 15) >> 2
        row = K_COL8_FIRST[y & 3]
        for x in range(width):
            sx = x ^ 4 if column & 1 else x
            col8 = row[sx & 15] + column * 64
            block = (tbp + ((y >> 1) & ~31) * (bw >> 1) +
                     ((x >> 2) & ~31) + K_BLOCK32[(y >> 4) & 3][(x >> 4) & 7]) & 16383
            out[y * width + x] = data[block * 256 + col8]
    return bytes(out)


def native_pack(tbp: int, tbw: int, gen: int, w: int, h: int, linear: bytes) -> bytes:
    if not (0 <= tbp < 16384 and 0 <= tbw <= 63 and
            0 <= gen <= MASK and 1 <= w <= 512 and 1 <= h <= 512):
        raise ValueError("T8 pack identifiers or dimensions out of range")
    if len(linear) != w * h:
        raise ValueError("Linear PSMT8 input must be exactly width * height bytes")
    header = struct.pack("<4s7I", b"T8P1", 1, tbp, tbw, gen, w, h, fnv32(linear))
    assert len(header) == 32
    return header + linear


def convert(vram: bytes | None, indices: bytes | None,
            tbp: int, tbw: int, w: int, h: int,
            gen: int | None, root: Path) -> Path:
    if (vram is None) == (indices is None):
        raise ValueError("Specify either --vram or --indices, not both")
    if vram is not None:
        expected_gen = source_generation(vram, tbp, tbw, h)
        if gen is not None and gen != expected_gen:
            raise ValueError("Provided --gen differs from exact GS page hash")
        gen = expected_gen
        linear = decode_psmt8(vram, tbp, tbw, w, h)
    else:
        if gen is None:
            raise ValueError("The --indices path requires --gen from a GS runtime log")
        linear = indices
    pack = native_pack(tbp, tbw, gen, w, h, linear)
    # The existing PSP reader selects files by TBP+source generation+size
    # and validates TBW and data hash inside the 32-byte header.
    root.mkdir(parents=True, exist_ok=True)
    name = f"t8_{tbp:04x}_{gen:08x}_{w}x{h}.tp8"
    target = root / name
    if target.exists() and target.read_bytes() != pack:
        raise ValueError(f"Refusing to overwrite a different pack: {target}")
    target.write_bytes(pack)
    (root / "enable.txt").touch(exist_ok=True)
    return target


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--vram", type=Path, help="Exact 4 MiB raw GS memory dump from your own game")
    g.add_argument("--indices", type=Path, help="Already-decoded, row-major raw PSMT8 texels")
    p.add_argument("--tbp", type=lambda v: int(v, 0), required=True,
                   help="PS2 GS TEX0.TBP0, 256-byte units (decimal or 0xHEX)")
    p.add_argument("--tbw", type=lambda v: int(v, 0), required=True,
                   help="PS2 GS TEX0.TBW in 64-pixel units")
    p.add_argument("--width", type=int, required=True)
    p.add_argument("--height", type=int, required=True)
    p.add_argument("--gen", help="PSP source generation as 8 hexadecimal digits; mandatory for --indices")
    p.add_argument("--out-root", type=Path, required=True,
                   help="Output native/ directory, copy into PSP/GAME/BT3/native/")
    a = p.parse_args(argv)
    try:
        gen = int(a.gen, 16) if a.gen is not None else None
        target = convert(a.vram.read_bytes() if a.vram else None,
                         a.indices.read_bytes() if a.indices else None,
                         a.tbp, a.tbw, a.width, a.height, gen, a.out_root)
    except (OSError, ValueError) as exc:
        p.error(str(exc))
    print(f"Created validated T8P1 pack: {target} ({target.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
