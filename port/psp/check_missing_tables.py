#!/usr/bin/env python3
"""Static PSP audit of unresolved pointers in the original PS2 data tables.

This intentionally does not delete, fake, or rewrite any function pointer.
A table pointer is not evidence that the function can never be invoked:
the runtime PPSSPP test remains mandatory before deleting an entry.
"""
from collections import defaultdict
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
ALLOWLIST = ROOT / "port/psp/expected_stubs.txt"
TABLES = {
    "cod_1C3400.data.s",
    "cod_1F44C0.rodata.s",
}
SYM = re.compile(r"^\s*\.long\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:[#;].*)?$")
LABEL = re.compile(r"^(D_[0-9A-Fa-f]{8}):\s*$")


def main():
    approved = {
        s.strip()
        for s in ALLOWLIST.read_text().splitlines()
        if s.strip() and not s.lstrip().startswith("#")
    }
    if not approved:
        raise SystemExit("PSP expected_stubs.txt is empty or unavailable")

    found = defaultdict(list)
    for path in sorted((ROOT / "port/data").glob("*.s")):
        current_table = "(unlabeled)"
        for line_no, line in enumerate(path.read_text().splitlines(), 1):
            label = LABEL.match(line)
            if label:
                current_table = label.group(1)
            item = SYM.match(line)
            if item and item.group(1) in approved:
                found[item.group(1)].append((path.name, current_table, line_no))

    absent = approved - found.keys()
    elsewhere = sorted(
        (sym, path, line)
        for sym, refs in found.items()
        for path, _, line in refs
        if path not in TABLES
    )
    if absent or elsewhere:
        for sym in sorted(absent):
            print(f"ERROR: allowed missing symbol {sym} is no longer in PSP data tables")
        for sym, path, line in elsewhere:
            print(f"ERROR: {sym} unexpectedly referenced by {path}:{line}")
        return 1

    groups = defaultdict(list)
    for sym, refs in sorted(found.items()):
        for path, table, _ in refs:
            groups[(path, table)].append(sym)
    print(f"PSP missing-pointer audit: {len(approved)} symbols, "
          f"{sum(map(len, found.values()))} table relocations, "
          f"{len(groups)} groups (no direct-C claims)")
    for (path, table), refs in sorted(groups.items()):
        print(f"  {path}:{table}: {len(refs)} relocation(s)")
    print("All unresolved entries remain guarded by Port_MissingAt; "
          "indirect runtime reachability is NOT proven by this audit.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
