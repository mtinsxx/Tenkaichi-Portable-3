#!/usr/bin/env python3
"""Tests for BT3 original ObjMdlMesh -> F3P1 conversion, with no game assets."""
import importlib.util
import json
import struct
import tempfile
from pathlib import Path

p=Path(__file__).with_name("psp_bt3_mesh.py")
spec=importlib.util.spec_from_file_location("bt3mesh",p)
mesh=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mesh)

# Exact original model structure: 0x6c meshOfs; part has next, last,
# enabled, node 0x0A; chain begins at +0x60.
geometry=[]
for i in range(3):
    geometry.extend([float(i),1.,2.,0.5,
                     0.,1.,0.,0.,
                     0.125,0.75,0.,0.])
raw=struct.pack("<36f",*geometry)
assert mesh.valid_fighter_vertices(raw)
materials=bytearray(5*16)
tex0=(19<<20)|(7<<26)|(7<<30)
tex1=(0<<20)|(4<<26)|(4<<30)
struct.pack_into("<Q",materials,16,tex0)
struct.pack_into("<Q",materials,32,tex1)
vif=(struct.pack("<I",0x6c058000)+materials+
     struct.pack("<I",0x6c098005)+raw+
     struct.pack("<I",mesh.VIF_END))
mat,parts=mesh.decode_vif_chain(vif)
assert len(mat)==1 and mat[0]["base_tex0"]==f"0x{tex0:016x}"
assert mat[0]["shade_tex0"]==f"0x{tex1:016x}"
assert len(parts)==1 and parts[0]["data"]==raw
assert parts[0]["count"]==3

original=bytearray(0xe0+len(vif))
struct.pack_into("<i",original,0x6c,0x80)
struct.pack_into("<iHHHH",original,0x80,0,0,1,1,0x33)
original[0xe0:0xe0+len(vif)]=vif
listing=list(mesh.original_parts(original))
assert len(listing)==1 and listing[0]["node"]==0x33
with tempfile.TemporaryDirectory() as tmp:
    folder=Path(tmp)
    output=folder/"native"
    result=mesh.export_original_mesh(original,output)
    assert result["total_parts"]==1
    assert result["exported_packets"]==1
    name=f"fighter_{mesh.fnv(raw):08x}_3.f3p"
    exported=(output/name).read_bytes()
    assert exported[:24]==struct.pack("<4s5I",b"F3P1",1,3,48,
                                      mesh.fnv(raw),mesh.fnv(raw))
    assert exported[24:]==raw
    assert result["parts"][0]["node"]==0x33
    assert result["parts"][0]["materials"][0]["base_psm"]==19
    assert result["parts"][0]["status"]=="supported"
    assert len(json.loads((output/"original_mesh_manifest.json").read_text())["parts"])==1

    # Raw source directly from a real-format AFS partition directory.
    afs=folder/"PZS3US0.AFS"
    afs.write_bytes(struct.pack("<4sIII",b"AFS\0",1,0x20,len(original))+
                    b"\0"*16+original)
    assert mesh.read_resource(afs,0)==original
    direct=mesh.export_original_mesh(mesh.read_resource(afs,0),
                                     folder/"from-afs")
    assert direct["exported_packets"]==1

    # Wrong node links do not become invented vertices.
    invalid=bytearray(original)
    struct.pack_into("<i",invalid,0x6c,99999999)
    try:
        mesh.export_original_mesh(invalid,folder/"bad")
        raise AssertionError("invalid source mesh offset accepted")
    except ValueError:
        pass
    # Unsupported packed VIF uses no false-positive output.
    masked=bytearray(original)
    masked[0xe0+4+80:0xe0+4+84]=struct.pack("<I",0x7c098005)
    unsupported=mesh.export_original_mesh(masked,folder/"fallback")
    assert unsupported["exported_packets"]==0
    assert unsupported["parts"][0]["status"]=="unsupported_vif_or_disabled"
    # A malformed VIF chain is refused, not overread.
    truncated=bytearray(original)
    truncated[-4:]=b"abcd"
    try:
        mesh.export_original_mesh(truncated,folder/"truncated")
        raise AssertionError("missing DMA end tag accepted")
    except ValueError:
        pass

print("Real BT3 model records + materials + VIF fighter streams -> F3P1 from AFS: PASS")
