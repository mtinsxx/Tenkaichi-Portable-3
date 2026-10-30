#!/usr/bin/env python3
"""Synthetic binary tests based on exact BT3 bobj_a.h and TexEntry layouts."""
import importlib.util
import json
import math
import struct
import tempfile
from pathlib import Path

here=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location("bt3orig",
                                        here/"psp_bt3_original.py")
bt3=importlib.util.module_from_spec(spec)
spec.loader.exec_module(bt3)

# Source quaternion Quat_Pack skips largest W, stores three 20-bit fields.
q=(3<<60) | (0x7ffff<<40) | (0x7ffff<<20) | 0x7ffff
quat=bt3.quat_unpack(q)
assert len(quat)==4 and abs(quat[3]-1)<1e-5
assert all(math.isfinite(v) for v in quat)

# Original BobjAnim layout: 0x94 header, 71 x u16 track offsets,
# each track offset in 4-byte units; actual sampled key data.
raw=bytearray(0x94)
raw[1]=0 # no events
struct.pack_into("<H",raw,2,2) # last frame == 2
struct.pack_into("<H",raw,6,0x94//4) # node 0 (rot only)
raw.extend(struct.pack("<HHQH",1,1,q,0))
while len(raw)%4:
    raw.append(0)
struct.pack_into("<H",raw,8,len(raw)//4) # node 1 (full)
raw.extend(struct.pack("<HHfffIQ",0,1,1.,2.,3.,0,q))
length,tracks=bt3.parse_bobj_animation(raw)
assert length==2 and len(tracks)==2
assert tracks[0][0] and not tracks[1][0]

# BobjBone: 0x40 linked structs, node ids 0,1,2, parent-first,
# each with rest xyz at +0x30. Model entry location is explicit.
model=bytearray(0x40*4)
for i,rest in enumerate(((0.,0.,0.),(4.,0.,0.),(0.,6.,0.))):
    off=0x40+i*0x40
    struct.pack_into("<iHHHH",model,off,0x40,0,i==2,0,i)
    struct.pack_into("<fff",model,off+0x30,*rest)
skel=bt3.parse_skeleton(model,0x40)
assert sorted(skel)==[0,1,2]
data,info=bt3.convert_animation(raw,model,0x40)
assert data[:16]==struct.pack("<4sIII",b"A3P1",1,3,3)
assert info["node_ids"]==[0,1,2] and info["frame_count"]==3
assert len(data)==16+3*3*48
# node1 is full-key position (1,2,3); node2 is no-track rest (0,6,0)
frame0_node1=struct.unpack_from("<12f",data,16+48)
frame0_node2=struct.unpack_from("<12f",data,16+2*48)
assert (frame0_node1[3],frame0_node1[7],frame0_node1[11])==(1.,2.,3.)
assert (frame0_node2[3],frame0_node2[7],frame0_node2[11])==(0.,6.,0.)
assert abs(frame0_node1[0]-1.)<1e-4
assert abs(frame0_node1[5]-1.)<1e-4

# The same exact animation bytes packed in Philip Gage BPE literal block.
# Dictionary: skip 127, set literal 127, skip 127, set literal 255.
block=bytes([254,127,254,255])+struct.pack(">H",len(raw))+raw
packed=struct.pack("<II",len(raw),len(block))+block
assert bt3.bpe_decompress(packed)==raw
try:
    bt3.bpe_decompress(packed[:-1])
    raise AssertionError("truncated BPE accepted")
except ValueError:
    pass
try:
    bt3.parse_bobj_animation(raw[:-2])
    raise AssertionError("truncated original full-track accepted")
except ValueError:
    pass

# Real 0x40-byte TexEntry file header (TexFile + TEX0 @ +0x30).
texfile=bytearray(0x80)
struct.pack_into("<II",texfile,0,1,16)
tex0=(19<<20)|(7<<26)|(7<<30)
struct.pack_into("<Q",texfile,0x40+0x30,tex0)
materials=bt3.parse_texture_file(texfile)
assert len(materials)==1 and materials[0]["psm"]==19
assert materials[0]["width"]==128
assert materials[0]["height"]==128
texfile[4]=0xff
try:
    bt3.parse_texture_file(texfile)
    raise AssertionError("invalid material table accepted")
except ValueError:
    pass

with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp)
    afs=p/"PZS3US0.AFS"
    # AFS count 2; first is BPE animation, second is skeleton model.
    base=0x20
    second=base+len(packed)
    afs.write_bytes(struct.pack("<4sI",b"AFS\0",2)+
      struct.pack("<4I",base,len(packed),second,len(model))+
      b"\0"*(base-24)+packed+model)
    assert bt3.read_resource(afs,0)==packed
    assert bt3.read_resource(afs,1)==model
    try:
        bt3.read_resource(afs,2)
        raise AssertionError("out-of-range AFS index accepted")
    except ValueError:
        pass
    clip,meta=bt3.convert_animation(
        bt3.bpe_decompress(bt3.read_resource(afs,0)),
        bt3.read_resource(afs,1),0x40)
    (p/"real_clip.a3p").write_bytes(clip)
    assert clip==data
print("Direct original BT3 BPE/animation/quaternion/skeleton/TexEntry/AFS decode -> PSP A3P1: PASS")
