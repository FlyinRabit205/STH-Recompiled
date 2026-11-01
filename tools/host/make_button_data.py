#!/usr/bin/env python3
"""pack_buttons.bin: what the texture pack import paints over 7 HD sheets (Xbox buttons).

usage: make_button_data.py <out.bin>
For each sheet made by xbox_buttons.py, the pixels that differ from the pack image scaled the
same way are stored, so the importer can repeat the edit on the player's own copy of the pack. The stored pixels are
the painted buttons over cleared slots (made by xbox_buttons.py), never the pack's art.
Format: "SNBT", u32 1, u32 count; per sheet: u64 key, u32 w, u32 h, u16 path length, path (inside the pack, UTF-8),
u32 runs, runs x {u32 first pixel, u32 count}, then the run pixels as RGBA8.
"""
import json
import struct
import sys

import numpy as np
from PIL import Image

SRC = {  # key -> pack image (made in sessions 9 and 10)
    "03c321b9482d8769": "HUD/tex1_256x128_09d976ea25d1c1c0_10ad82008942c7ef_8.dds",
    "183f7f822c92ad17": "Menu/tex1_256x256_143a9a3691ec2fe3_efabe8033e55bf28_9.dds",
    "1908d27fb5b7e02e": "HUD/tex1_64x64_cc9e9b4f27f7130b_d28ca1e543d25903_9.dds",
    "1ceb42767f9d7c17": "HUD/tex1_256x256_fb31bc5c96de28fd_2fefe3dc6932e435_9.dds",
    "20af7434f4dfc9ed": "HUD/tex1_128x64_f044169b53dba310_a8be49c7b1655ce5_8.dds",
    "5e6d440065844883": "HUD/tex1_256x256_7cb29588b25458f8_53709788725cd20d_9.dds",
}
PACK = "/home/claude/pack/GUP/"
MADE = "/home/claude/btn/out/"


def main(out):
    blob = bytearray(b"SNBT" + struct.pack("<II", 1, len(SRC)))
    for key, path in SRC.items():
        made = np.asarray(Image.open(MADE + key + ".png").convert("RGBA")).astype(np.int16)
        h, w = made.shape[:2]
        pack = np.asarray(Image.open(PACK + path).convert("RGBA").resize((w, h), Image.LANCZOS)).astype(np.int16)
        # only pixels the painting changed: no pack pixels go into the file
        diff = np.abs(made - pack).max(axis=2) > 8
        flat = diff.ravel()
        idx = np.flatnonzero(np.diff(np.concatenate([[0], flat.astype(np.int8), [0]])))
        runs = list(zip(idx[0::2], idx[1::2] - idx[0::2]))
        px = made.astype(np.uint8).reshape(-1, 4)
        p = path.encode()
        blob += struct.pack("<QIIH", int(key, 16), w, h, len(p)) + p + struct.pack("<I", len(runs))
        for a, n in runs:
            blob += struct.pack("<II", int(a), int(n))
        for a, n in runs:
            blob += px[a:a + n].tobytes()
        print(key, w, h, len(runs), "runs", int(flat.sum()), "pixels")
    open(out, "wb").write(blob)
    print(len(blob), "bytes")


if __name__ == "__main__":
    main(sys.argv[1])
