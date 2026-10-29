#!/usr/bin/env python3
"""Safely extract entries from a user's own BT3 .AFS archive.

BT3's DATA/*.AFS has magic AFS\0, a LE 32-bit file count and
(count * 8) LE (offset,length) pairs. This is **archive extraction**,
not a decoder of proprietary character/scene models or animations.
No copyrighted bytes are distributed with the source repository.
"""
import argparse
import json
import struct
from pathlib import Path

MAX_ENTRIES = 65535


def index_afs(path):
    src = Path(path)
    size = src.stat().st_size
    with src.open("rb") as f:
        first = f.read(8)
        if len(first) != 8:
            raise ValueError("truncated AFS header")
        magic, count = struct.unpack("<4sI", first)
        if magic != b"AFS\0" or not (0 < count <= MAX_ENTRIES):
            raise ValueError("invalid AFS header or entry count")
        table_end = 8 + count * 8
        if table_end > size:
            raise ValueError("AFS file index runs past file end")
        table = f.read(count * 8)
        if len(table) != count * 8:
            raise ValueError("truncated AFS index")
        records = []
        for idx in range(count):
            offset, length = struct.unpack_from("<II", table, idx * 8)
            if (offset < table_end and length) or offset > size or length > size - offset:
                raise ValueError(f"invalid AFS range at entry {idx}")
            records.append((offset, length))
        return records


def extract_afs(path, output, entries=None):
    src = Path(path).resolve()
    output = Path(output).resolve()
    rows = index_afs(src)
    chosen = range(len(rows)) if entries is None else sorted(set(entries))
    if any(i < 0 or i >= len(rows) for i in chosen):
        raise ValueError("AFS entry index is out of bounds")
    if src.is_relative_to(output):
        raise ValueError("output directory cannot contain the original AFS")
    output.mkdir(parents=True, exist_ok=True)
    result = []
    with src.open("rb") as fin:
        for i in chosen:
            pos, length = rows[i]
            dest = output / f"{i:05d}.bin"
            if dest.exists():
                raise ValueError(f"refusing to overwrite {dest}")
            with dest.open("xb") as fout:
                fin.seek(pos)
                left = length
                while left:
                    chunk = fin.read(min(left, 1 << 20))
                    if not chunk:
                        raise ValueError(f"truncated AFS entry {i}")
                    fout.write(chunk)
                    left -= len(chunk)
            result.append({"index": i, "filename": dest.name, "bytes": length})
    (output / "afs_index.json").write_text(
        json.dumps({"source": src.name, "entries": result},
                   indent=2, sort_keys=True) + "\n",
        encoding="utf-8")
    return len(result)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", required=True, type=Path,
                   help="DATA/PZS3US0.AFS (from your own extracted game)")
    p.add_argument("--output", required=True, type=Path,
                   help="destination for numbered extracted .bin entries")
    p.add_argument("--indices", nargs="*", type=int,
                   help="optional subset of archive indices")
    args = p.parse_args()
    print("Extracted AFS entries:", extract_afs(args.source,args.output,
                                               args.indices))


if __name__ == "__main__":
    main()
