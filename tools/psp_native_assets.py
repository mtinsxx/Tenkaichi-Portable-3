#!/usr/bin/env python3
"""Offline PSP-native resource conversion. No game content is bundled.

Run locally on assets you already lawfully extracted. Outputs to
ms0:/PSP/GAME/BT3/native/ (copy manually). The running PSP renderer
currently consumes only --type t8; models/animations are prepared as
portable intermediate buffers for a later native mesh/animation path.
"""
import argparse
import json
import math
import struct
from pathlib import Path

T8_HDR = struct.Struct("<4s7I")


def fnv(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def size_ok(w, h):
    if not (1 <= w <= 512 and 1 <= h <= 512):
        raise ValueError("textures must be between 1 and 512 texels per dimension")


def write_t8(args):
    size_ok(args.width, args.height)
    src = Path(args.source).read_bytes()
    if len(src) != args.width * args.height:
        raise ValueError(f"expected {args.width * args.height} linear PSMT8 indices, got {len(src)}")
    if not (0 <= args.tbp <= 0x3FFF and 0 <= args.tbw <= 63
            and 0 <= args.gen <= 0xFFFFFFFF):
        raise ValueError("TBP, TBW and GS generation are out of range")
    out = Path(args.output) if args.output else Path(
        f"t8_{args.tbp:04x}_{args.gen:08x}_{args.width}x{args.height}.tp8")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(T8_HDR.pack(b"T8P1", 1, args.tbp, args.tbw, args.gen,
                                args.width, args.height, fnv(src)) + src)
    return out


def write_rgba5551(args):
    """Convert RGBA8888 to PSP GU RGB5551, linear; preserve 1-bit alpha.

    Reject intermediate alpha values: the conversion is not equivalent to
    normal transparent/bloom effects, which must remain in RGBA8888.
    """
    size_ok(args.width, args.height)
    raw = Path(args.source).read_bytes()
    if len(raw) != args.width * args.height * 4:
        raise ValueError("expected tightly packed RGBA8888 input")
    packed = bytearray()
    for i in range(0, len(raw), 4):
        r, g, b, a = raw[i:i+4]
        if a not in (0, 255):
            raise ValueError("partial alpha requires RGBA8888 on PSP; refusing lossy 5551")
        px = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a != 0) << 15)
        packed.extend(struct.pack("<H", px))
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(packed)
    return out


def write_mesh(args):
    """Prepack scene meshes into fixed endian geometry with full floats.

    This is a portable *intermediate resource*, not yet connected to
    PSP/GU stage rendering: vertex clip/matrix/texture bindings are still
    runtime GS-derived and must be validated before binding a native mesh.
    """
    data = json.loads(Path(args.source).read_text(encoding="utf-8"))
    verts = data["vertices"]
    indices = data["indices"]
    if not verts or len(verts) > 65535 or len(indices) > 196605:
        raise ValueError("invalid mesh size")
    if len(indices) % 3:
        raise ValueError("indices must form triangles")
    out = bytearray(struct.pack("<4sIII", b"M3P1", 1, len(verts), len(indices)))
    for vert in verts:
        pos, uv, rgba = vert["position"], vert["uv"], vert["rgba"]
        if (len(pos) != 3 or len(uv) != 2 or len(rgba) != 4
                or not all(isinstance(x, (int, float)) and math.isfinite(x)
                           for x in pos + uv)
                or not all(isinstance(x, int) and 0 <= x <= 255 for x in rgba)):
            raise ValueError("invalid mesh vertex data")
        out.extend(struct.pack("<5f4B", *map(float, pos+uv), *rgba))
    for idx in indices:
        if not isinstance(idx, int) or not (0 <= idx < len(verts)):
            raise ValueError("out-of-range mesh index")
        out.extend(struct.pack("<H", idx))
    result = Path(args.output)
    result.parent.mkdir(parents=True, exist_ok=True)
    result.write_bytes(out)
    return result


def write_animation(args):
    """Prepack bone-frame matrices without changing interpolation order.

    12 floats per bone per frame form a 3x4 affine transform. No
    quantization of rotations or keyframe interpolation is assumed.
    """
    data = json.loads(Path(args.source).read_text(encoding="utf-8"))
    frames = data["frames"]
    if not frames or len(frames) > 65535:
        raise ValueError("invalid number of animation frames")
    bones = len(frames[0])
    if not (1 <= bones <= 255):
        raise ValueError("invalid bone count")
    out = bytearray(struct.pack("<4sIII", b"A3P1", 1, bones, len(frames)))
    for frame in frames:
        if len(frame) != bones:
            raise ValueError("inconsistent bone count")
        for affine in frame:
            if len(affine) != 12 or not all(isinstance(x, (int, float))
                                          and math.isfinite(x) for x in affine):
                raise ValueError("each bone transform must have 12 finite floats")
            out.extend(struct.pack("<12f", *affine))
    result = Path(args.output)
    result.parent.mkdir(parents=True, exist_ok=True)
    result.write_bytes(out)
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("type", choices=("t8", "rgba5551", "mesh", "animation"))
    p.add_argument("--source", required=True, help="user's own extracted raw/JSON input")
    p.add_argument("--output")
    p.add_argument("--width", type=int)
    p.add_argument("--height", type=int)
    p.add_argument("--tbp", type=lambda v: int(v, 0))
    p.add_argument("--tbw", type=lambda v: int(v, 0))
    p.add_argument("--gen", type=lambda v: int(v, 0))
    args = p.parse_args()
    if args.type in ("t8", "rgba5551") and (args.width is None or args.height is None):
        p.error("texture conversions require --width and --height")
    if args.type == "t8" and (args.tbp is None or args.tbw is None or args.gen is None):
        p.error("t8 requires GS --tbp, --tbw and --gen from a matching texture")
    if args.type != "t8" and not args.output:
        p.error("--output is required for non-T8 conversions")
    fn = {"t8": write_t8, "rgba5551": write_rgba5551,
          "mesh": write_mesh, "animation": write_animation}[args.type]
    print("Wrote:", fn(args))


if __name__ == "__main__":
    main()
