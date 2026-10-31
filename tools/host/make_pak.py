#!/usr/bin/env python3
"""Build texture .pak files for the runtime (gpu/texturedumper.cpp ListPak) from match_pack.py's result.

usage: make_pak.py <match.json> <out dir> <name prefix> [scale cap]
Each image is scaled to at most <cap> times the Xbox texture's size (default 4, at most 1024 pixels a side), given a
full mip chain and encoded BC1 when it has no alpha, else BC3. Images shared by several keys are stored once.
A pak: "SNPK", u32 1, u32 count, count x {u64 key, u64 offset, u32 size}, then DDS files. Paks stay under 28 MB.
"""
import io
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

MAX_PAK = 10 << 20  # transfers to the PC refuse files much over 10 MB
MAX_SIDE = 1024
# accept rule (see PROGRESS.md session 7): close, or a bit further but far ahead of the runner-up
def accepted(m):
    err, second = m["err"], m["second"]
    return (err < 0.004 and second >= err * 1.6) or (err < 0.01 and second >= err * 4.0)


def encode_level(im, fourcc):
    buf = io.BytesIO()
    im.save(buf, format="DDS", pixel_format=fourcc)
    b = buf.getvalue()
    return b[128:] if b[84:88] != b"DX10" else b[148:]


def make_dds(path, w, h):
    im = Image.open(path).convert("RGBA")
    if im.size != (w, h):
        im = im.resize((w, h), Image.LANCZOS)
    a = np.asarray(im)[..., 3]
    fourcc = "DXT1" if a.min() >= 250 else "DXT5"
    levels = []
    cw, ch = w, h
    cur = im
    while True:
        levels.append(encode_level(cur, fourcc))
        if cw <= 4 and ch <= 4:
            break
        cw, ch = max(cw // 2, 4), max(ch // 2, 4)
        cur = im.resize((cw, ch), Image.LANCZOS)
    hdr = bytearray(128)
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
    struct.pack_into("<4sIIIIIII", hdr, 0, b"DDS ", 124, flags, h, w, len(levels[0]), 0, len(levels))
    struct.pack_into("<II4s", hdr, 76, 32, 0x4, fourcc.encode())
    struct.pack_into("<I", hdr, 108, 0x1000 | 0x8 | 0x400000)
    return bytes(hdr) + b"".join(levels)


def main(match, out_dir, prefix, cap=4):
    m = json.load(open(match))
    keep = {k: v for k, v in m.items() if accepted(v)}
    print(len(keep), "of", len(m), "matches accepted")
    os.makedirs(out_dir, exist_ok=True)
    blobs = {}  # (pack image, w, h) -> bytes
    entries = []  # (key, blob id)
    for k, v in sorted(keep.items()):
        w = min(v["w"] * cap, MAX_SIDE)
        h = min(v["h"] * cap, MAX_SIDE)
        bid = (v["pack"], w, h)
        if bid not in blobs:
            blobs[bid] = make_dds(v["pack"], w, h)
        entries.append((int(k, 16), bid))
    # group keys by blob so a blob and all its keys land in one pak
    by_blob = {}
    for key, bid in entries:
        by_blob.setdefault(bid, []).append(key)
    paks, cur, size = [], [], 0
    for bid, keys in by_blob.items():
        n = len(blobs[bid])
        if cur and size + n > MAX_PAK:
            paks.append(cur)
            cur, size = [], 0
        cur.append((bid, keys))
        size += n
    if cur:
        paks.append(cur)
    total = 0
    for i, group in enumerate(paks):
        count = sum(len(keys) for _, keys in group)
        head = 12 + count * 20
        index, data, off = [], [], head
        for bid, keys in group:
            for key in keys:
                index.append(struct.pack("<QQI", key, off, len(blobs[bid])))
            data.append(blobs[bid])
            off += len(blobs[bid])
        path = os.path.join(out_dir, f"{prefix}_{i:02d}.pak")
        with open(path, "wb") as f:
            f.write(struct.pack("<4sII", b"SNPK", 1, count))
            f.write(b"".join(index))
            f.write(b"".join(data))
        total += os.path.getsize(path)
        print(path, count, "textures", os.path.getsize(path) >> 20, "MB")
    print(f"{len(entries)} textures, {len(blobs)} images, {total >> 20} MB in {len(paks)} paks")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]) if len(sys.argv) > 4 else 4)
