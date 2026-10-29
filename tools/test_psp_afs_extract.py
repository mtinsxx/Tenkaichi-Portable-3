#!/usr/bin/env python3
import importlib.util
import json
import struct
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "psp_afs_extract", Path(__file__).with_name("psp_afs_extract.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp)
    afs=root/"sample.AFS"
    # Valid real AFS layout, table at offset 8, contents at 0x20.
    header=struct.pack("<4sI",b"AFS\0",2)
    table=struct.pack("<4I",0x20,3,0x30,4)
    afs.write_bytes(header+table+b"\0"*8+b"cat"+b"\0"*13+b"woof")
    assert module.index_afs(afs)==[(32,3),(48,4)]
    out=root/"unpacked"
    assert module.extract_afs(afs,out)==2
    assert (out/"00000.bin").read_bytes()==b"cat"
    assert (out/"00001.bin").read_bytes()==b"woof"
    assert len(json.loads((out/"afs_index.json").read_text())["entries"])==2
    try:
        module.extract_afs(afs,out)
        raise AssertionError("unexpected overwrite")
    except ValueError:
        pass
    bad=root/"bad.AFS"
    bad.write_bytes(header+struct.pack("<4I",0x20,3,0xfffffff0,4))
    try:
        module.index_afs(bad)
        raise AssertionError("invalid AFS length accepted")
    except ValueError:
        pass
    bad.write_bytes(b"NOTAFS\0\0")
    try:
        module.index_afs(bad)
        raise AssertionError("invalid AFS signature accepted")
    except ValueError:
        pass
print("AFS extraction format, ranges, overwrite, wrong-header: OK")
