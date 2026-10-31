#!/usr/bin/env python3
"""Find MSVC RTTI vtables for classes whose decorated name matches a substring.
usage: rtti.py <substring> [<substring>...]
Prints class name, complete-object-locator VA, vtable VA and the first vtable slots."""
import json
import struct
import sys

XBE = "/home/claude/game/extracted/default.xbe"
INFO = "/home/claude/game/xbeinfo.json"
d = open(XBE, "rb").read()
info = json.load(open(INFO))
secs = info["sections"]


def va2off(va):
    for s in secs:
        if s["va"] <= va < s["va"] + s["rsize"]:
            return s["raw"] + va - s["va"]
    return None


def off2va(off):
    for s in secs:
        if s["raw"] <= off < s["raw"] + s["rsize"]:
            return s["va"] + off - s["raw"]
    return None


def u32(va):
    o = va2off(va)
    return struct.unpack_from("<I", d, o)[0] if o is not None else None


def find_dword(value):
    pat = struct.pack("<I", value)
    out, i = [], d.find(pat)
    while i != -1:
        if i % 4 == 0:
            v = off2va(i)
            if v:
                out.append(v)
        i = d.find(pat, i + 1)
    return out


def code(va):
    return any(s["va"] <= va < s["va"] + s["vsize"] and s["flags"] & 4 for s in secs)


for sub in sys.argv[1:]:
    start = 0
    while True:
        i = d.find(b".?AV", start)
        if i == -1:
            break
        end = d.index(b"\0", i)
        name = d[i:end].decode("latin1")
        start = end
        if sub not in name or "checked_deleter" in name or "sp_counted" in name:
            continue
        td = off2va(i - 8)
        for ref in find_dword(td):
            col = ref - 12
            if u32(col) != 0:
                continue
            for vref in find_dword(col):
                vt = vref + 4
                slots = []
                for k in range(16):
                    v = u32(vt + 4 * k)
                    if v is None or not code(v):
                        break
                    slots.append(f"{v:08X}")
                print(f"{name}\n  COL {col:08X} vtable {vt:08X} slots: {' '.join(slots)}")
