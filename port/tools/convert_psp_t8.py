#!/usr/bin/env python3
"""Build/verify PSP-native indexed PSMT8 texture packs from OWN extracted data.

The converter consumes already decoded, tightly packed PSMT8 indices (.idx8),
NOT raw AFS, compressed game models, or PSP/PS2 executable files. Index data
may be exported from a running BT3 PSP renderer using native/record.txt.
The input manifest is JSON: {"textures":[{"source":"stage/0001.idx8",
"tbp":336,"tbw":8,"gen":"0xabcd1234","width":128,"height":128},...]}.
"gen" is the GS SOURCE PAGE hash (before CLUT), not a palette hash.
Output matches the EBOOT's T8P1 native pack reader. No game content goes into git.

Usage:
  python3 port/tools/convert_psp_t8.py pack manifest.json OUT_DIRECTORY
  python3 port/tools/convert_psp_t8.py verify OUT_DIRECTORY
"""
import json
import pathlib
import struct
import sys

MAGIC = b"T8P1"
VERSION = 1
HEADER = struct.Struct("<4s7I")
LIMIT = 512 * 512


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def value(x, name):
    if isinstance(x, bool):
        raise ValueError(f"{name} must be an integer")
    result = int(x, 0) if isinstance(x, str) else int(x)
    if result < 0 or result > 0xFFFFFFFF:
        raise ValueError(f"{name} outside u32 range")
    return result


def pack_name(tbp, gen, width, height):
    return f"t8_{tbp:04x}_{gen:08x}_{width}x{height}.tp8"


def make_pack(row, data):
    tbp = value(row["tbp"], "tbp")
    tbw = value(row["tbw"], "tbw")
    gen = value(row["gen"], "gen")
    width = value(row["width"], "width")
    height = value(row["height"], "height")
    if tbp >= 16384 or tbw > 63 or width == 0 or height == 0 or width > 512 or height > 512:
        raise ValueError("invalid GS TBP/TBW or PSP texture dimensions")
    if len(data) != width * height or len(data) > LIMIT:
        raise ValueError("input must be exactly width*height linear GS PSMT8 indices")
    return pack_name(tbp, gen, width, height), HEADER.pack(
        MAGIC, VERSION, tbp, tbw, gen, width, height, fnv1a(data)) + data


def check_pack(blob):
    if len(blob) < HEADER.size:
        raise ValueError("truncated T8P1")
    magic, version, tbp, tbw, gen, width, height, hashval = HEADER.unpack_from(blob)
    if magic != MAGIC or version != VERSION or tbp >= 16384 or tbw > 63:
        raise ValueError("invalid T8P1 header")
    if width == 0 or height == 0 or width > 512 or height > 512:
        raise ValueError("invalid texture dimensions")
    raw = blob[HEADER.size:]
    if len(raw) != width * height or fnv1a(raw) != hashval:
        raise ValueError("length/checksum mismatch")
    return pack_name(tbp, gen, width, height), len(raw)


def convert(manifest_file, out_dir):
    manifest_file = pathlib.Path(manifest_file).resolve()
    out_dir = pathlib.Path(out_dir)
    obj = json.loads(manifest_file.read_text(encoding="utf-8"))
    rows = obj["textures"]
    if not isinstance(rows, list):
        raise ValueError("textures must be a list")
    prepared = {}
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("every texture must be an object")
        source = pathlib.Path(row["source"])
        if source.is_absolute() or ".." in source.parts:
            raise ValueError("texture source must stay inside manifest directory")
        infile = (manifest_file.parent / source).resolve()
        if not infile.is_relative_to(manifest_file.parent):
            raise ValueError("source escapes manifest directory")
        data = infile.read_bytes()
        name, blob = make_pack(row, data)
        if name in prepared and prepared[name] != blob:
            raise ValueError(f"conflicting TBP/gen/size pack: {name}")
        prepared[name] = blob
    # Validate all before modifying destination; disk full can still leave
    # partial output, so each pack is written atomically via rename.
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, blob in sorted(prepared.items()):
        tmp = out_dir / (name + ".tmp")
        tmp.write_bytes(blob)
        tmp.replace(out_dir / name)
    # Activation is explicit: leave this file out when PSP should use
    # its reference GS decoder during diagnostics.
    (out_dir / "enable.txt").write_text("Enable verified native PSMT8 pack reads.\n", encoding="utf-8")
    print(f"Built {len(prepared)} packs in {out_dir}; copy to PSP/GAME/BT3/native/")


def verify(directory):
    paths = sorted(pathlib.Path(directory).glob("*.tp8"))
    if not paths:
        raise ValueError("no .tp8 textures found")
    for p in paths:
        name, nbytes = check_pack(p.read_bytes())
        if p.name != name:
            raise ValueError(f"filename/header mismatch: {p.name} != {name}")
        print(f"OK {name}: {nbytes} indices")
    print(f"Verified {len(paths)} packs")


def main(args):
    if len(args) == 3 and args[0] == "pack":
        convert(args[1], args[2])
    elif len(args) == 2 and args[0] == "verify":
        verify(args[1])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except (OSError, KeyError, ValueError, TypeError, json.JSONDecodeError) as ex:
        raise SystemExit(f"PSP T8 converter error: {ex}")
