#!/usr/bin/env python3
"""Build and verify native PSP T8P1 texture-index packs from *user-owned*
linear GS PSMT8 index planes. No game assets or firmware are bundled.

The existing PSP renderer binds these indices with a live CSM1 CLUT read
from GS VRAM. Palettes therefore remain dynamic and are not baked here.
A pack can be generated from a source plane exported by the PSP itself;
the index-gen identifier is the source-only GS page hash reported at
capture time (NOT the palette-dependent final texture key).

Usage:
  python3 port/psp/tools/convert_t8.py pack --indices plane.raw \
      --tbp 336 --tbw 8 --index-gen 0xa1b2c3d4 \
      --width 64 --height 64 --out native/
  python3 port/psp/tools/convert_t8.py verify native/t8_0150_a1b2c3d4_64x64.tp8
"""
import argparse
import pathlib
import struct

HEADER = struct.Struct("<4s7I")
MAGIC = b"T8P1"
MAX_SIDE = 512
MAX_PIXELS = MAX_SIDE * MAX_SIDE
FNV_BASIS = 2166136261
FNV_PRIME = 16777619


def fnv32(data: bytes) -> int:
    h = FNV_BASIS
    for value in data:
        h = ((h ^ value) * FNV_PRIME) & 0xFFFFFFFF
    return h


def number(value: str) -> int:
    return int(value, 0)


def ensure_fields(tbp: int, tbw: int, gen: int, width: int, height: int) -> None:
    if not (0 <= tbp <= 16383 and 0 <= tbw <= 63 and 0 <= gen <= 0xFFFFFFFF):
        raise ValueError("invalid GS TBP, TBW or index-generation value")
    if not (1 <= width <= MAX_SIDE and 1 <= height <= MAX_SIDE):
        raise ValueError("T8 source must be at most 512 x 512")
    if width & (width - 1) or height & (height - 1):
        raise ValueError("GS texture dimensions must be powers of two")


def filename(tbp: int, gen: int, width: int, height: int) -> str:
    return f"t8_{tbp:04x}_{gen:08x}_{width}x{height}.tp8"


def build_pack(raw: bytes, tbp: int, tbw: int, gen: int,
               width: int, height: int) -> bytes:
    ensure_fields(tbp, tbw, gen, width, height)
    if len(raw) != width * height:
        raise ValueError(f"expected exactly {width*height} index bytes, got {len(raw)}")
    return HEADER.pack(MAGIC, 1, tbp, tbw, gen, width, height, fnv32(raw)) + raw


def parse_pack(data: bytes) -> tuple[dict, bytes]:
    if len(data) < HEADER.size:
        raise ValueError("truncated T8P1 header")
    magic, version, tbp, tbw, gen, w, h, stored_hash = HEADER.unpack_from(data)
    if magic != MAGIC or version != 1:
        raise ValueError("bad T8P1 magic or version")
    ensure_fields(tbp, tbw, gen, w, h)
    raw = data[HEADER.size:]
    if len(raw) != w * h or fnv32(raw) != stored_hash:
        raise ValueError("truncated, oversized or corrupted T8P1 index payload")
    return dict(tbp=tbp, tbw=tbw, index_gen=gen, width=w, height=h,
                fnv=stored_hash), raw


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    commands = p.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack", help="convert linear GS PSMT8 indices into T8P1")
    pack.add_argument("--indices", type=pathlib.Path, required=True)
    pack.add_argument("--tbp", type=number, required=True)
    pack.add_argument("--tbw", type=number, required=True)
    pack.add_argument("--index-gen", type=number, required=True)
    pack.add_argument("--width", type=number, required=True)
    pack.add_argument("--height", type=number, required=True)
    pack.add_argument("--out", type=pathlib.Path, required=True)
    verify = commands.add_parser("verify", help="check metadata, dimensions and checksum")
    verify.add_argument("pack", type=pathlib.Path)
    args = p.parse_args()
    if args.command == "pack":
        raw = args.indices.read_bytes()
        payload = build_pack(raw, args.tbp, args.tbw, args.index_gen,
                             args.width, args.height)
        output = args.out / filename(args.tbp, args.index_gen,
                                     args.width, args.height)
        args.out.mkdir(parents=True, exist_ok=True)
        # Never overwrite a different texture/source with the same name.
        if output.exists() and output.read_bytes() != payload:
            raise ValueError(f"pack exists with different content: {output}")
        output.write_bytes(payload)
        metadata, _ = parse_pack(output.read_bytes())
        print(f"native PSP PSMT8 pack: {output} ({len(payload)} bytes) {metadata}")
    else:
        metadata, _ = parse_pack(args.pack.read_bytes())
        print(f"valid native PSP PSMT8 pack: {args.pack} {metadata}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
