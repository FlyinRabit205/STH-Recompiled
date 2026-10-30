#!/usr/bin/env python3
"""Minimal XDVDFS (Xbox ISO) reader: list and extract files.
usage: xiso.py list <iso>
       xiso.py extract <iso> <outdir> [name-substring]
"""
import os, struct, sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"


def find_base(f):
    for base in (0, 0x18300000, 0x2080000, 0xFD90000):
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return base
    raise SystemExit("not an XDVDFS image")


def walk(f, base, sector, size, path=""):
    if size == 0:
        return
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    stack = [0]
    seen = set()
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(data):
            continue
        seen.add(off)
        left, right, start, fsize, attr, nlen = struct.unpack_from("<HHIIBB", data, off)
        if left == 0xFFFF:
            continue
        name = data[off + 14: off + 14 + nlen].decode("latin1")
        full = f"{path}/{name}"
        if attr & 0x10:
            yield full, None, None
            yield from walk(f, base, start, fsize, full)
        else:
            yield full, start, fsize
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)


def entries(iso):
    f = open(iso, "rb")
    base = find_base(f)
    f.seek(base + 32 * SECTOR + 20)
    root_sec, root_size = struct.unpack("<II", f.read(8))
    return f, base, list(walk(f, base, root_sec, root_size))


def main():
    cmd, iso = sys.argv[1], sys.argv[2]
    f, base, ents = entries(iso)
    if cmd == "list":
        for p, s, n in sorted(ents):
            print(("<dir>" if s is None else f"{n:>11}"), p)
    elif cmd == "extract":
        out = sys.argv[3]
        flt = sys.argv[4] if len(sys.argv) > 4 else ""
        for p, s, n in ents:
            if s is None or flt.lower() not in p.lower():
                continue
            dst = os.path.join(out, p.lstrip("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            f.seek(base + s * SECTOR)
            with open(dst, "wb") as o:
                left = n
                while left:
                    b = f.read(min(left, 1 << 22))
                    o.write(b)
                    left -= len(b)
        print("done")


if __name__ == "__main__":
    main()
