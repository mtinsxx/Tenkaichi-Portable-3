#!/usr/bin/env python3
"""Regression check for the PPSSPP near-null dereference in Ki blast logic.

Compilation uses -std=gnu89 and the optional clang right-to-left argument
rewriter can be absent. Do not pass uninitialized getter output parameters
as arguments to another function in the SAME expression.

This is a source regression test, not a PPSSPP simulation.
"""
from pathlib import Path
import re

SRC = (Path(__file__).resolve().parents[2] /
       "src/battle/btl_tech_b.c").read_text()

FUNCS = {
    "BtlKiBlast_GetKiCost": "BtlKiBlast_GetCurrent",
    "BtlKiBlast_GetKiCostOfHit": "BtlKiBlast_GetOfHit",
    "BtlKiBlast_GetSpeed": "BtlKiBlast_GetCurrent",
    "BtlKiBlast_GetSpeedOfHit": "BtlKiBlast_GetOfHit",
    "BtlKiBlast_GetReact": "BtlKiBlast_GetCurrent",
    "BtlKiBlast_GetReactOfHit": "BtlKiBlast_GetOfHit",
}

def body(name):
    match = re.search(r"^(?:s32|f32)\s+" + re.escape(name) +
                      r"\s*\([^)]*\)\s*\{", SRC, re.M)
    if not match:
        raise AssertionError(name + " definition missing")
    # None of these function bodies currently contain nested braces.
    end = SRC.find("}", match.end())
    if end < 0:
        raise AssertionError(name + " has no closing brace")
    return SRC[match.end():end]

def test():
    for name, getter in FUNCS.items():
        block = body(name)
        get = re.search(r"\bBtlKiBlastData\s*\*\s*rec\s*=\s*" +
                        re.escape(getter) +
                        r"\s*\([^;]*&kind\s*\)\s*;", block)
        if not get:
            raise AssertionError(name + ": missing explicit getter result")
        ret = re.search(r"\breturn\s+[^;]*\brec->(?:ki|react|speed)\b[^;]*;", block)
        if not ret or ret.start() <= get.end():
            raise AssertionError(name + ": result used before getter completes")
        if re.search(r"\breturn\s+[^;]*" + re.escape(getter) + r"\s*\(", block):
            raise AssertionError(name + ": nested getter reintroduced")
        if getter == "BtlKiBlast_GetOfHit" and "&chr" not in get.group():
            raise AssertionError(name + ": owner output is not populated first")
        print(name, "OK: getter sequenced before consuming outputs")

if __name__ == "__main__":
    test()
