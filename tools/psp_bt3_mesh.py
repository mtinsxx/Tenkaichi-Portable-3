#!/usr/bin/env python3
"""Extract original BT3 ObjMdlMesh/VIF submeshes directly from player-owned AFS.

Source: include/sys/gfxm_e.h (meshOfs +0x6c, linked mesh records,
VIF chain +0x60), src/sys/gfxm_e_c.c (5Q material/TEX0 upload),
port/src/gs/gs_vu1.c (fighter 3Q vertex format at TOPS+5).

Only standard V4-32 TOPS+5 blocks whose complete vertex data passes
strict sanity checks are exported as F3P1 for the existing PSP GU fighter
path. Unsupported and ambiguous VIF encodings are reported, not guessed.
"""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path
from psp_bt3_original import read_resource, parse_texture_file

MAX_PARTS=2048
MAX_CHAIN=512*1024
VIF_END=0x70000000
VIF_MATERIAL=0x6c058000


def u32(data,offset):
    return struct.unpack_from("<I",data,offset)[0]


def original_parts(model):
    if len(model)<0x70:raise ValueError("ObjMdlFile truncated")
    offset=struct.unpack_from("<i",model,0x6c)[0]
    if offset<0x70 or offset+0x64>len(model):
        raise ValueError("ObjMdlFile.meshOfs invalid")
    seen=set()
    for index in range(MAX_PARTS):
        if offset in seen or offset+0x64>len(model):
            raise ValueError("cyclic/truncated original ObjMdlMesh")
        seen.add(offset)
        next_byte,unknown,last,enabled,node=struct.unpack_from(
            "<iHHHH",model,offset)
        if node>0xa8:raise ValueError("invalid original mesh node ID")
        if last:
            limit=min(len(model),offset+0x60+MAX_CHAIN)
        else:
            if next_byte<0x64 or offset+next_byte>len(model):
                raise ValueError("invalid ObjMdlMesh.next")
            limit=offset+next_byte
        start=offset+0x60
        if limit-start>MAX_CHAIN:
            raise ValueError("model mesh VIF stream exceeds safety bound")
        vif=model[start:limit]
        end=None
        for i in range(0,len(vif)//4*4,4):
            if u32(vif,i)==VIF_END:
                end=i+4
                break
        if end is None:
            raise ValueError("missing original VIF chain DMA end tag")
        yield {"index":index,"byte_offset":offset,"node":node,
               "enabled":bool(enabled),"last":bool(last),
               "vif":vif[:end]}
        if last:return
        offset+=next_byte
    raise ValueError("too many ObjMdlMesh parts")


def valid_fighter_vertices(data):
    if len(data)%48 or not 3<=len(data)//48<=38:return False
    for off in range(0,len(data),48):
        position=struct.unpack_from("<4f",data,off)
        normal=struct.unpack_from("<3f",data,off+16)
        uv=struct.unpack_from("<2f",data,off+32)
        if (not all(math.isfinite(x) for x in position+normal+uv)
            or not 0<=position[3]<=1
            or max(abs(x) for x in position[:3]+normal+uv)>1e7):
            return False
    return True


def decode_vif_chain(vif):
    """Conservative VIF scan: literal 5Q TEX0 and unmasked V4-32.

    The game also uses DMA calls, VIF cycles, flags and packed formats.
    The general cases are NOT parsed and are deliberately left to the
    original engine. A block is exported only when independently shaped
    like original fighter program0/1 V4-32 source vertices.
    """
    materials=[]
    packets=[]
    i=0
    words=len(vif)//4
    while i<words:
        command=u32(vif,i*4)
        if command==VIF_END:break
        if command==VIF_MATERIAL and i+21<=words:
            base=u32(vif,(i+5)*4)|(u32(vif,(i+6)*4)<<32)
            shade=u32(vif,(i+9)*4)|(u32(vif,(i+10)*4)<<32)
            materials.append({
                "vif_word":i,"base_tex0":f"0x{base:016x}",
                "shade_tex0":f"0x{shade:016x}",
                "base_psm":(base>>20)&0x3f,
                "shade_psm":(shade>>20)&0x3f})
            i+=21
            continue
        if ((command>>24)&0x7f)==0x6c:
            count=(command>>16)&0xff
            if count==0:count=256
            available=i+1+count*4<=words
            if available:
                addr=command&0x3ff
                tops=bool(command&0x8000)
                masked=bool(command&0x10000000)
                if addr==5 and tops and not masked and count%3==0:
                    payload=vif[(i+1)*4:(i+1+count*4)*4]
                    if valid_fighter_vertices(payload):
                        packets.append({"vif_word":i,"count":count//3,
                                        "data":payload})
                i+=1+count*4
                continue
        i+=1
    return materials,packets


def fnv(data):
    value=2166136261
    for b in data:value=((value^b)*16777619)&0xffffffff
    return value


def export_original_mesh(model,directory,texture_file=None):
    root=Path(directory)
    if root.exists() and any(root.iterdir()):
        raise ValueError("output directory must be empty")
    parts=list(original_parts(model))
    catalog={"format":1,
        "source_sha256":hashlib.sha256(model).hexdigest(),
        "source":"original ObjMdlFile/ObjMdlMesh embedded VIF",
        "total_parts":len(parts),"exported_packets":0,"parts":[]}
    if texture_file is not None:
        catalog["texture_file_materials"]=parse_texture_file(texture_file)
    root.mkdir(parents=True,exist_ok=True)
    for p in parts:
        materials,packets=decode_vif_chain(p["vif"])
        metadata={"part":p["index"],"node":p["node"],
                  "enabled":p["enabled"],"byte_offset":p["byte_offset"],
                  "vif_sha256":hashlib.sha256(p["vif"]).hexdigest(),
                  "materials":materials,"packets":[]}
        if p["enabled"]:
            for packet in packets:
                raw=packet["data"]
                key=fnv(raw)
                count=packet["count"]
                name=f"fighter_{key:08x}_{count}.f3p"
                binary=struct.pack("<4s5I",b"F3P1",1,count,48,key,key)+raw
                file=root/name
                if file.exists() and file.read_bytes()!=binary:
                    raise ValueError("original VIF packet FNV collision")
                file.write_bytes(binary)
                metadata["packets"].append({"name":name,
                    "vertex_count":count,"source_vif_word":packet["vif_word"]})
                catalog["exported_packets"]+=1
        metadata["status"]=("supported" if metadata["packets"]
                            else "unsupported_vif_or_disabled")
        catalog["parts"].append(metadata)
    (root/"original_mesh_manifest.json").write_text(
        json.dumps(catalog,indent=2,sort_keys=True)+"\n",
        encoding="utf-8")
    return catalog


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",required=True)
    parser.add_argument("--index",type=lambda x:int(x,0))
    parser.add_argument("--texture-source")
    parser.add_argument("--texture-index",type=lambda x:int(x,0))
    parser.add_argument("--output",required=True)
    args=parser.parse_args()
    model=read_resource(args.source,args.index)
    tex=(read_resource(args.texture_source,args.texture_index)
         if args.texture_source else None)
    result=export_original_mesh(model,args.output,tex)
    print("Original submeshes:",result["total_parts"],
          "safe F3P1 packets:",result["exported_packets"])
    if not result["exported_packets"]:
        print("Unsupported VIF geometry; no guessed fighter mesh emitted.")


if __name__=="__main__":
    main()
