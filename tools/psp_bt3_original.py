#!/usr/bin/env python3
"""Direct BT3 resource converters grounded in the game's decompiled types.

Sources: include/battle/bobj_a.h, src/battle/bobj_a.c,
src/sys/bpe.c, src/sys/math3d.c and include/sys/gfxm_b_b.h.

Usage:
  python3 tools/psp_bt3_original.py animation --source motion.bin \
    --model skeleton_model.bin --bones-offset 0x80 --output native/motion.a3p --bpe
  python3 tools/psp_bt3_original.py materials --source texture_file.bin \
    --output native/materials.json

--source may also be an original DATA/*.AFS archive with --index.
--model may also be an archive, with --model-index. The BobjBone offset
inside the model's member MUST be provided: its enclosing file-table
location is not established for all BT3 costume/model variants.

Converts *real original* BPE animation tracks and rest translations to
PSP-ready frame/bone affine matrices (A3P1) for the optional native GU
preview. It does NOT replace gameplay skeleton hierarchy, layer
blending, secondary motion, mesh skin weights, or material passes.
No copyrighted game assets are included.
"""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path
from psp_afs_extract import index_afs

MAX_INPUT = 32 * 1024 * 1024
MAX_DECODED = 8 * 1024 * 1024
BONE_MAX = 71
FRAME_MAX = 128
U16 = struct.Struct("<H")
ANIM_HDR = struct.Struct("<4sIII")


def fail(why):
    raise ValueError(why)


def read_resource(path, afs_index=None):
    path = Path(path)
    if afs_index is None:
        if path.stat().st_size > MAX_INPUT:
            fail("input too large")
        return path.read_bytes()
    records = index_afs(path)
    if not 0 <= afs_index < len(records):
        fail("AFS resource index out of range")
    off, size = records[afs_index]
    if size > MAX_INPUT:
        fail("AFS member too large")
    with path.open("rb") as f:
        f.seek(off)
        data = f.read(size)
    if len(data) != size:
        fail("AFS member truncated")
    return data


def bpe_decompress(data):
    """Bounded byte-pair expand, following src/sys/bpe.c/Bpe_Decode.

    For each block, read the 256-symbol left/right table and then
    expand the block's two-byte big-endian input length.
    """
    if len(data) < 8:
        fail("BPE header truncated")
    raw_size, compressed = struct.unpack_from("<II", data)
    if raw_size > MAX_DECODED or compressed != len(data)-8:
        fail("BPE raw size or compressed range invalid")
    pos = 8
    result = bytearray()
    while pos < len(data):
        left = list(range(256))
        right = [0] * 256
        c = 0
        if pos >= len(data):
            fail("missing BPE dictionary")
        count = data[pos]
        pos += 1
        while True:
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            if c > 256 or pos >= len(data):
                fail("invalid BPE dictionary")
            for _ in range(count + 1):
                if c >= 256 or pos >= len(data):
                    fail("BPE dictionary overrun")
                sym = data[pos]
                pos += 1
                left[c] = sym
                if c != sym:
                    if pos >= len(data):
                        fail("missing BPE dictionary rhs")
                    right[c] = data[pos]
                    pos += 1
                c += 1
            if c == 256:
                break
            if pos >= len(data):
                fail("truncated BPE dictionary")
            count = data[pos]
            pos += 1
        if pos + 2 > len(data):
            fail("missing BPE block size")
        block = (data[pos] << 8) | data[pos+1]
        pos += 2
        if pos + block > len(data):
            fail("truncated BPE block")
        stop = pos + block
        stack = []
        while pos < stop or stack:
            if stack:
                value = stack.pop()
            else:
                value = data[pos]
                pos += 1
            if left[value] == value:
                result.append(value)
                if len(result) > raw_size:
                    fail("BPE output overflow")
            else:
                stack.append(right[value])
                stack.append(left[value])
                if len(stack) > 65536:
                    fail("cyclic/overdeep BPE dictionary")
    if len(result) != raw_size:
        fail("BPE expanded size mismatch")
    return bytes(result)


def quat_unpack(packed):
    """Exact quantization map of src/sys/math3d.c/Quat_Unpack."""
    largest = packed >> 60
    if largest > 3:
        fail("invalid BT3 quaternion largest component")
    q = [(((packed >> (i*20)) & 0xfffff) / 1048575.0 - 0.5)
         * 1.41421356 for i in range(3)]
    s = sum(v*v for v in q)
    if s > 1.0 + 0.00001:
        fail("invalid BT3 packed quaternion")
    q.insert(largest, math.sqrt(max(0.0, 1.0-s)))
    return q


def slerp(a, b, t):
    dot = sum(x*y for x,y in zip(a,b))
    if dot < 0:
        b = [-x for x in b]
        dot = -dot
    if dot > 0.9995:
        q = [x+(y-x)*t for x,y in zip(a,b)]
        length = math.sqrt(sum(x*x for x in q))
        return [x/length for x in q]
    dot = max(-1.0, min(1.0, dot))
    theta = math.acos(dot)
    s = math.sin(theta)
    wa = math.sin((1-t)*theta)/s
    wb = math.sin(t*theta)/s
    return [x*wa+y*wb for x,y in zip(a,b)]


def parse_bobj_animation(data):
    if len(data) < 0x94:
        fail("truncated BobjAnim header")
    events = data[1]
    length = U16.unpack_from(data, 2)[0]
    if length >= FRAME_MAX:
        fail("clip exceeds 128 native GU preview frames; refusing truncation")
    if 0x94 + events*16 > len(data):
        fail("BobjAnim events exceed file size")
    tracks = {}
    for bone in range(BONE_MAX):
        offset = U16.unpack_from(data, 6+bone*2)[0] * 4
        if not offset:
            continue
        if offset < 0x94+events*16 or offset+4 > len(data):
            fail(f"bone {bone} track offset invalid")
        flags, count = struct.unpack_from("<HH", data, offset)
        if count == 0 or count > 4096 or flags & ~1:
            fail(f"bone {bone} track flags/count invalid")
        rot_only = bool(flags & 1)
        stride = 8 if rot_only else 24
        end = offset+4+count*stride
        if rot_only:
            end += count*2
        if end > len(data):
            fail(f"bone {bone} track data truncated")
        keys = []
        for i in range(count):
            at = offset+4+i*stride
            if rot_only:
                quat64 = struct.unpack_from("<Q", data, at)[0]
                frame = U16.unpack_from(data, offset+4+count*8+i*2)[0]
                pos = None
            else:
                x,y,z,frame,quat64 = struct.unpack_from("<fffIQ", data,at)
                pos = (x,y,z)
                if not all(math.isfinite(v) for v in pos):
                    fail("non-finite full-track translation")
            if frame > length or (keys and frame <= keys[-1][0]):
                fail(f"bone {bone} nonmonotonic/outside clip keyframe")
            keys.append((frame, pos, quat_unpack(quat64)))
        tracks[bone] = (rot_only, keys)
    return length, tracks


def parse_skeleton(data, offset):
    """BobjBone (0x40 bytes) linked records; no guessed parent mapping."""
    if offset < 0 or offset+0x40 > len(data):
        fail("skeleton first bone outside model file")
    result = {}
    seen = set()
    while True:
        if offset in seen or offset + 0x40 > len(data):
            fail("cyclic/truncated skeleton")
        seen.add(offset)
        next_off = struct.unpack_from("<i",data,offset)[0]
        pop,last,unk,node = struct.unpack_from("<HHHH",data,offset+4)
        if node >= BONE_MAX or node in result:
            fail("skeleton node id out of range or duplicate")
        rest = struct.unpack_from("<fff",data,offset+0x30)
        if not all(math.isfinite(v) for v in rest):
            fail("invalid bone rest translation")
        result[node] = {"rest":rest,"pop":pop}
        if last:
            break
        if next_off < 0x40:
            fail("invalid next BobjBone relative byte offset")
        offset += next_off
        if len(result) > BONE_MAX:
            fail("skeleton exceeds 71 BT3 nodes")
    return result


def sample(keys, rot_only, t, rest):
    if len(keys) == 1:
        _, pos, quat = keys[0]
        return rest if rot_only else pos, quat
    if t < keys[0][0] or t > keys[-1][0]:
        return (rest if rot_only else (0.0,0.0,0.0)), (0.0,0.0,0.0,1.0)
    a = keys[0]
    for b in keys[1:]:
        if t <= b[0]:
            alpha = (t-a[0])/(b[0]-a[0])
            pos = rest if rot_only else tuple(
                x+(y-x)*alpha for x,y in zip(a[1],b[1]))
            return pos, slerp(a[2],b[2],alpha)
        a=b
    return rest if rot_only else keys[-1][1], keys[-1][2]


def matrix_3x4(pos, q):
    """Row-major rotation from Quat_ToMtx in the decompiled BT3."""
    x,y,z,w=q
    return (
        1-2*y*y-2*z*z, 2*x*y+2*w*z, 2*x*z-2*w*y, pos[0],
        2*x*y-2*w*z, 1-2*x*x-2*z*z, 2*y*z+2*w*x, pos[1],
        2*x*z+2*w*y, 2*y*z-2*w*x, 1-2*x*x-2*y*y, pos[2]
    )


def convert_animation(data, model, bones_offset):
    length, tracks = parse_bobj_animation(data)
    skeleton = parse_skeleton(model, bones_offset)
    # A3P1 bones in ascending BT3 node-id order. The game already
    # handles *hierarchy, blending, constraints and secondary motion*.
    # This is a per-track LOCAL pose clip, not a replacement for the
    # game's final world-space skeletal animation.
    order = sorted(skeleton)
    if not order:
        fail("no model bones")
    out = bytearray(ANIM_HDR.pack(b"A3P1",1,len(order),length+1))
    for frame in range(length+1):
        for node in order:
            rest=skeleton[node]["rest"]
            if node in tracks:
                rot_only, keys=tracks[node]
                pos,quat=sample(keys,rot_only,frame,rest)
            else:
                pos,quat=rest,(0.0,0.0,0.0,1.0)
            mat=matrix_3x4(pos,quat)
            if not all(math.isfinite(v) for v in mat):
                fail("invalid decoded animation transform")
            out.extend(struct.pack("<12f",*mat))
    return bytes(out), {"frame_count":length+1,"bone_count":len(order),
                        "node_ids":order,"animated_node_ids":sorted(tracks),
                        "type":"local_3x4_poses_no_world_hierarchy"}


def parse_texture_file(data):
    """TexFile + TexEntry, include/sys/gfxm_b_b.h: real GS material table."""
    if len(data)<0x14:fail("truncated TexFile")
    count,words=struct.unpack_from("<II",data)
    base=words*4
    if not 0<count<=2048 or base<0x10 or base+count*0x40>len(data):
        fail("TexFile entry table invalid")
    entries=[]
    for i in range(count):
        at=base+i*0x40
        pix,clut,pix_size,clut_size=struct.unpack_from("<IIii",data,at)
        tex0=struct.unpack_from("<Q",data,at+0x30)[0]
        tbp_step,cbp_step=struct.unpack_from("<ii",data,at+0x10)
        if pix_size<0 or clut_size<0:fail("negative original GS packet size")
        if (pix_size and pix*4+pix_size>len(data)) or (
                clut_size and clut*4+clut_size>len(data)):
            fail("texture/CLUT packet references outside source")
        entries.append({"index":i,"tex0":f"0x{tex0:016x}",
             "psm":(tex0>>20)&63,"width":1<<((tex0>>26)&15),
             "height":1<<((tex0>>30)&15),
             "pix_packet_offset":pix*4,"pix_packet_bytes":pix_size,
             "clut_packet_offset":clut*4,"clut_packet_bytes":clut_size,
             "tbp_step":tbp_step,"cbp_step":cbp_step})
    return entries


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("type",choices=("animation","materials"))
    p.add_argument("--source",required=True)
    p.add_argument("--index",type=lambda v:int(v,0))
    p.add_argument("--model")
    p.add_argument("--model-index",type=lambda v:int(v,0))
    p.add_argument("--bones-offset",type=lambda v:int(v,0))
    p.add_argument("--bpe",action="store_true")
    p.add_argument("--output",required=True)
    args=p.parse_args()
    source=read_resource(args.source,args.index)
    if args.type=="animation":
        if not args.model or args.bones_offset is None:
            p.error("original animation conversion needs --model and --bones-offset")
        model=read_resource(args.model,args.model_index)
        raw=bpe_decompress(source) if args.bpe else source
        output,info=convert_animation(raw,model,args.bones_offset)
        meta={"input_sha256":hashlib.sha256(source).hexdigest(),
              "model_sha256":hashlib.sha256(model).hexdigest(),
              **info}
        dest=Path(args.output)
        if dest.suffix.lower()!=".a3p":
            p.error("animation output must end in .a3p")
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(output)
        dest.with_suffix(".json").write_text(
            json.dumps(meta,indent=2,sort_keys=True)+"\n",encoding="utf-8")
        print("Converted original BT3 animation:",dest,info)
    else:
        meta={"source_sha256":hashlib.sha256(source).hexdigest(),
              "source_format":"BT3 TexFile and 0x40-byte TexEntry",
              "materials":parse_texture_file(source)}
        dest=Path(args.output)
        if dest.suffix.lower()!=".json":
            p.error("material metadata output must be JSON")
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_text(json.dumps(meta,indent=2,sort_keys=True)+"\n",
                        encoding="utf-8")
        print("Decoded original BT3 material table:",dest,
              len(meta["materials"]),"entries")


if __name__=="__main__":
    main()
