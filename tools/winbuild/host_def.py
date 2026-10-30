#!/usr/bin/env python3
"""Write the .def for the MSVC-format import library of Shadow.exe that Shadow.dll links against.

The exe is built with llvm-mingw (Itanium C++ mangling); the DLL with clang targeting MSVC (Microsoft mangling).
extern "C" names match as they are. For C++ functions the DLL imports, map the MSVC name to the exe's Itanium
export of the same function using the def `==` import-name form:  ?f@@YAXH@Z == _Z1fi

usage: host_def.py <exe_exports.txt> <dll_imports.txt> <out.def>
  exe_exports.txt: one exported name per line (llvm-readobj --coff-exports of Shadow.exe)
  dll_imports.txt: one name per line that the DLL objects import (their __imp_ symbols without the prefix)
"""
import re
import sys


def msvc_base(name):
    # ?x86_icall@@YAXAEAUX86Context@@PEAEI@Z -> x86_icall ; plain C names stay
    if name.startswith("?"):
        return name[1:].split("@", 1)[0]
    return name


def itanium_base(name):
    # _Z9x86_icallR10X86ContextPhj -> x86_icall ; _ZN6kernel3R32Ej -> kernel::R32
    m = re.match(r"_Z(N)?((?:\d+[A-Za-z_][A-Za-z0-9_]*)+)", name)
    if not m:
        return None
    parts, s = [], m.group(2)
    while s:
        n = re.match(r"(\d+)", s)
        if not n:
            break
        ln = int(n.group(1))
        s = s[len(n.group(1)):]
        parts.append(s[:ln])
        s = s[ln:]
        if not m.group(1):
            break
    return parts[-1] if parts else None


def main(exe_exports, dll_imports, out):
    exports = [l.strip() for l in open(exe_exports) if l.strip()]
    exset = set(exports)
    by_base = {}
    for e in exports:
        b = itanium_base(e)
        if b:
            by_base.setdefault(b, []).append(e)
    lines, missing = [], []
    for name in sorted(set(l.strip() for l in open(dll_imports) if l.strip())):
        if name in exset:
            continue  # written below with the plain exports
        cands = by_base.get(msvc_base(name), [])
        if len(cands) == 1:
            lines.append(f"    {name} == {cands[0]}")
        else:
            missing.append((name, cands))
    with open(out, "w") as f:
        f.write("LIBRARY Shadow.exe\nEXPORTS\n")
        for e in exports:
            f.write(f"    {e}\n")
        f.write("\n".join(lines) + "\n")
    for name, cands in missing:
        print(f"unmapped: {name} candidates={cands}", file=sys.stderr)
    print(f"{len(exports)} exports, {len(lines)} mapped C++ imports, {len(missing)} unmapped")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
