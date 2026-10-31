#!/usr/bin/env python3
"""Rebuild the English font sheets from the HD texture pack's own font sheets (its glyph style).

usage: pack_fonts.py <work dir (index.json, tex/)> <extracted game dir> <pack Text dir> <hd_fonts out dir> <out dir>
The pack's sheets are 4x upscales of the GameCube sheets, whose glyph layout differs from the Xbox ones (each
sheet holds the characters its scenes use). Each Xbox glyph cell (from its .met) is looked up in every pack sheet at
every cell position of the same size; where the pack's cell, scaled down, matches the Xbox glyph, its 4x pixels are
that character's HD glyph. Xbox sheets are then assembled from those glyphs; characters the pack never shows keep the
glyph from hd_fonts.py (its <key>.png). Output <out>/<key>.png at 4x, white on black like the originals.
"""
import glob
import json
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd_fonts  # noqa: E402

S = 4


def main(work, game, pack_dir, hd_dir, out):
    os.makedirs(out, exist_ok=True)
    ix = json.load(open(os.path.join(work, "index.json")))
    sheets = []  # (key, met, lum)
    for key, e in ix.items():
        src = next((s for s in e["sources"] if s.startswith("fonts/") and s.endswith("_EN.txd")), None)
        if not src:
            continue
        met = os.path.join(game, os.path.dirname(src), e["name"] + ".met")
        if os.path.exists(met):
            sheets.append((key, hd_fonts.read_met(met), hd_fonts.decode_sheet(work, key)))
    packs = []
    for p in sorted(glob.glob(os.path.join(pack_dir, "*_256x256_*"))):
        hd = np.asarray(Image.open(p).convert("RGBA"), dtype=np.float32)
        lum_hd = hd[..., :3].mean(-1) * (hd[..., 3] / 255.0)
        small = np.asarray(Image.fromarray(lum_hd.astype(np.uint8)).resize((256, 256), Image.BOX), dtype=np.float32)
        packs.append((p, lum_hd, small))
    print(len(sheets), "xbox sheets,", len(packs), "pack sheets")
    # all cell rectangles (w, h) the sheets use, at the positions they use them
    rects = {}
    for _, met, _ in sheets:
        for code, x0, y0, x1, y1 in met:
            rects.setdefault((x1 - x0 + 1, y1 - y0 + 1), set()).add((x0, y0))
    glyphs = {}  # code -> (err, hd crop)
    for key, met, lum in sheets:
        for code, x0, y0, x1, y1 in met:
            cell = lum[y0:y1 + 1, x0:x1 + 1]
            if cell.max() < 96:
                continue  # blank glyph
            w, h = cell.shape[1], cell.shape[0]
            best = glyphs.get(code, (1e9, None))[0]
            if best < 6:
                continue
            for p, hd, small in packs:
                for (px, py) in rects[(w, h)]:
                    c2 = small[py:py + h, px:px + w]
                    if c2.shape != cell.shape:
                        continue
                    err = float(np.abs(c2 - cell).mean())
                    if err < best and err < 14:
                        best = err
                        glyphs[code] = (err, hd[py * S:(py + h) * S, px * S:(px + w) * S].copy())
    print(len(glyphs), "characters found in the pack")
    codes_all = {c for _, met, _ in sheets for c, *_ in met}
    missing = sorted(codes_all - set(glyphs))
    print(len(missing), "missing:", "".join(chr(c) for c in missing[:80]))
    made = 0
    for key, met, lum in sheets:
        base = os.path.join(hd_dir, key + ".png")
        img = np.asarray(Image.open(base).convert("L"), dtype=np.float32).copy() if os.path.exists(base) \
            else np.zeros((256 * S, 256 * S), np.float32)
        for code, x0, y0, x1, y1 in met:
            g = glyphs.get(code)
            if g is None:
                continue
            crop = g[1]
            img[y0 * S:(y1 + 1) * S, x0 * S:(x1 + 1) * S] = crop[:(y1 - y0 + 1) * S, :(x1 - x0 + 1) * S]
        Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "L").save(os.path.join(out, key + ".png"))
        made += 1
    print(made, "sheets written")


if __name__ == "__main__":
    main(*sys.argv[1:6])
