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
COLOR_HDR = struct.Struct("<4s7IQ3I")
COLOR_PSMS = (0x00, 0x01, 0x02, 0x0A)  # PS2 GS color only; no depth aliases

def write_color(args):
    """Preconvert expanded GS colour bytes to PSP-native 5551/8888.

    Source layout is tightly packed expanded GS R,G,B,A (as returned by
    Gs_Expand) with GS alpha units; NOT generic PNG sRGB conversion.
    Runtime validates the page hash/TEXA and 17 source pixels.
    White-font texture variants and textures with changed GS data
    retain the live decoder, never the offline pack.
    """
    size_ok(args.width, args.height)
    if args.psm not in COLOR_PSMS:
        raise ValueError("color packs only support PSMCT32/24/16/16S")
    if (not (0 <= args.tbp <= 0x3FFF and 0 <= args.tbw <= 63
             and 0 <= args.gen <= 0xFFFFFFFF
             and 0 <= args.texa <= 0xFFFFFFFFFFFFFFFF)):
        raise ValueError("invalid GS page identity or TEXA")
    raw = Path(args.source).read_bytes()
    if len(raw) != args.width * args.height * 4:
        raise ValueError("expected exactly width*height*4 expanded GS RGBA bytes")
    gs = struct.unpack("<" + "I" * (len(raw) // 4), raw)
    # Match the existing texture_get() alphaNeed condition exactly.
    opaque = all(((c >> 24) & 255) in (0, 128, 255) for c in gs)
    fmt = 1 if opaque else 2
    if fmt == 1:
        converted = b"".join(struct.pack("<H",
            ((c >> 3) & 31) | (((c >> 11) & 31) << 5) |
            (((c >> 19) & 31) << 10) | ((int((c >> 24) != 0)) << 15))
            for c in gs)
    else:
        def scaled(c):
            a = (c >> 24) & 255
            a = 255 if a >= 128 else a * 2
            return (c & 0xFFFFFF) | (a << 24)
        converted = b"".join(struct.pack("<I", scaled(c)) for c in gs)
    out = Path(args.output) if args.output else Path(
        f"c_{args.tbp:04x}_{args.gen:08x}_{args.psm:02x}_"
        f"{args.width}x{args.height}_{args.texa:016x}.n4p")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(COLOR_HDR.pack(b"N4P1", 1, args.tbp,args.tbw,
        args.psm,args.gen,args.width,args.height,args.texa,0,fmt,
        fnv(converted)) + converted)
    return out



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


def write_fighter_vu(args):
    """Pack a REAL original-game VU1 3-quadword fighter vertex stream.

    The packet is captured from active BT3 VU program 0/1, not guessed
    from unparsed AFS files. It preserves positions, normals, UVs and
    two-bone weights bit-for-bit. The PSP uses LIVE animation matrices
    and current GS TEX0/material when drawing the saved packet.
    """
    src = Path(args.source).read_bytes()
    stride = 48
    if len(src) % stride:
        raise ValueError("fighter VU packet must contain exactly 3 quadwords per vertex")
    count = len(src) // stride
    if not 3 <= count <= 38:
        raise ValueError("fighter VU packet requires 3..38 vertices")
    for i in range(count):
        vertex = struct.unpack_from("<12f", src, i*stride)
        if (not all(math.isfinite(vertex[j]) for j in
                    (0,1,2,3,4,5,6,8,9)) or
            not (0.0 <= vertex[3] <= 1.0)):
            raise ValueError(f"invalid position, weight, normal or UV at vertex {i}")
    signature = fnv(src)
    out = Path(args.output) if args.output else Path(
        f"fighter_{signature:08x}_{count}.f3p")
    out.parent.mkdir(parents=True, exist_ok=True)
    # Header: F3P1 / version / count / stride / original packet FNV /
    # payload FNV. The PSP compares the ENTIRE payload against the
    # live VU buffer before using a file, not only this identifier.
    out.write_bytes(struct.pack("<4s5I", b"F3P1", 1, count, stride,
                                signature, signature) + src)
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


def write_bundle(args):
    """Convert a manifest of separately extracted assets without bundling game data.

    Texture packs have runtime integration. Meshes and animations are only
    PSP-portable intermediate files until the GU mesh loader is implemented.
    enable.txt is created LAST, after every requested conversion succeeds.
    """
    manifest = Path(args.manifest).resolve()
    root = Path(args.output).resolve()
    entries = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(entries, dict) or not isinstance(entries.get("assets"), list):
        raise ValueError("expected JSON object containing an assets array")
    if not entries["assets"]:
        raise ValueError("bundle must contain at least one extracted asset")
    root.mkdir(parents=True, exist_ok=True)
    generated = []
    for item in entries["assets"]:
        if not isinstance(item, dict) or item.get("type") not in (
                "color", "t8", "mesh", "animation", "fighter-vu"):
            raise ValueError("unsupported asset type")
        kind = item["type"]
        relative = Path(item["source"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError("asset input path must be relative to manifest")
        source = (manifest.parent / relative).resolve()
        if not source.is_file():
            raise ValueError(f"missing input asset: {relative}")
        if kind == "color":
            stem = (f"c_{item['tbp']:04x}_{item['gen']:08x}_{item['psm']:02x}_"
                    f"{item['width']}x{item['height']}_{item.get('texa',0):016x}.n4p")
        elif kind == "t8":
            stem = (f"t8_{item['tbp']:04x}_{item['gen']:08x}_"
                    f"{item['width']}x{item['height']}.tp8")
        elif kind == "fighter-vu":
            payload = source.read_bytes()
            if len(payload) % 48 or not (3 <= len(payload)//48 <= 38):
                raise ValueError("fighter-vu source must contain 3..38 live VU vertices")
            stem = f"fighter_{fnv(payload):08x}_{len(payload)//48}.f3p"
        else:
            stem = item.get("output")
            if not stem:
                raise ValueError("mesh/animation requires a destination filename")
            ext = ".m3p" if kind == "mesh" else ".a3p"
            if Path(stem).suffix != ext:
                raise ValueError(f"{kind} needs {ext} output extension")
        relative_out = Path(stem)
        if relative_out.is_absolute() or ".." in relative_out.parts:
            raise ValueError("bundle output path must remain inside output folder")
        destination = root / relative_out
        if destination.exists():
            raise ValueError("duplicate or existing output: " + str(relative_out))
        keys = ("width","height","tbp","tbw","gen","psm","texa")
        options = {key:item.get(key,0 if key=="texa" else None) for key in keys}
        options.update(source=str(source),output=str(destination))
        ns = argparse.Namespace(**options)
        converters = {"t8":write_t8,"color":write_color,
                      "mesh":write_mesh,"animation":write_animation,
                      "fighter-vu":write_fighter_vu}
        out = converters[kind](ns)
        generated.append({"type":kind,"file":relative_out.as_posix(),
                          "bytes":out.stat().st_size,
                          "fnv":f"{fnv(out.read_bytes()):08x}"})
    (root/"native_manifest.json").write_text(
       json.dumps({"format":1,"assets":generated},
                  indent=2,sort_keys=True)+"\n",encoding="utf-8")
    (root/"enable.txt").write_text("native-asset-packs-v1\n",encoding="ascii")
    return root

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("type", choices=("t8", "rgba5551", "color", "mesh", "animation", "fighter-vu", "bundle"))
    p.add_argument("--source", help="user's own extracted raw/JSON input")
    p.add_argument("--manifest", help="JSON list of already extracted assets")
    p.add_argument("--output")
    p.add_argument("--width", type=int)
    p.add_argument("--height", type=int)
    p.add_argument("--tbp", type=lambda v: int(v, 0))
    p.add_argument("--tbw", type=lambda v: int(v, 0))
    p.add_argument("--gen", type=lambda v: int(v, 0))
    p.add_argument("--psm", type=lambda v: int(v, 0))
    p.add_argument("--texa", type=lambda v: int(v, 0), default=0)
    args = p.parse_args()
    if args.type == "bundle":
        if not args.manifest or not args.output:
            p.error("bundle needs --manifest and --output directory")
        print("Converted asset bundle:", write_bundle(args))
        return
    if not args.source:
        p.error("asset conversion requires --source")
    if args.type in ("t8", "rgba5551", "color") and (args.width is None or args.height is None):
        p.error("texture conversions require --width and --height")
    if args.type == "t8" and (args.tbp is None or args.tbw is None or args.gen is None):
        p.error("t8 requires GS --tbp, --tbw and --gen from a matching texture")
    if args.type == "color" and (args.tbp is None or args.tbw is None or
                                 args.gen is None or args.psm is None):
        p.error("color requires --tbp, --tbw, --gen and --psm")
    if args.type not in ("t8", "color", "fighter-vu") and not args.output:
        p.error("--output is required for non-T8 conversions")
    fn = {"t8": write_t8, "color": write_color, "rgba5551": write_rgba5551,
          "mesh": write_mesh, "animation": write_animation,
          "fighter-vu": write_fighter_vu}[args.type]
    print("Wrote:", fn(args))


if __name__ == "__main__":
    main()
