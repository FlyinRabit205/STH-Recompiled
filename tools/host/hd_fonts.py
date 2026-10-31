#!/usr/bin/env python3
"""Redraw Shadow the Hedgehog's font sheets at 4x for the runtime's texture replacement.

usage: hd_fonts.py <work dir with index.json, tex/> <extracted game dir> <out dir> [lang ...]
For every fonts/<set>/<set>_<LANG>.txd sheet: read its .met (METRICS1: name, a number, then lines
"code x0 y0 x1 y1 #", inclusive pixel rectangles in the 256x256 sheet), measure the original glyphs' cap height and
baseline, and draw each character with a monospace gothic font into the same rectangle, 4x larger. The sheet stays
white on black (the game reads brightness as coverage). Output: <out>/<key>.png per sheet and a preview sheet.
"""
import json
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xbtex  # noqa: E402

SCALE = 4
CJK = "/usr/share/fonts/opentype/noto/NotoSansCJK-Medium.ttc"
CJK_BOLD = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"


def mono_face(path, size):
    # the collection's faces: find "Noto Sans Mono CJK JP"
    for i in range(20):
        try:
            f = ImageFont.truetype(path, size, index=i)
        except OSError:
            break
        if f.getname()[0] == "Noto Sans Mono CJK JP":
            return f
    return ImageFont.truetype(path, size)


def read_met(path):
    lines = open(path, encoding="latin1").read().split("\n")
    out = []
    for l in lines[3:]:
        p = l.split()
        if len(p) >= 5 and p[0].isdigit():
            code, x0, y0, x1, y1 = map(int, p[:5])
            out.append((code, x0, y0, x1, y1))
    return out


def char_of(code):
    return chr(code)  # the .met codes are Unicode code points (\u266a for the note)


def decode_sheet(work, key):
    raw = open(os.path.join(work, "tex", key + ".bin"), "rb").read()
    t = xbtex.Tex()
    t.fmt = raw[0]
    t.w, t.h = struct.unpack_from("<HH", raw, 1)
    t.palette, t.data = (raw[5:1029], raw[1029:]) if t.fmt == 0x0B else (None, raw[5:])
    return xbtex.decode(t)[..., :3].mean(-1)  # brightness


def ink_rows(lum, x0, y0, x1, y1):
    cell = lum[y0:y1 + 1, x0:x1 + 1]
    rows = np.where(cell.max(1) > 96)[0]
    return (rows[0], rows[-1]) if len(rows) else None


def make_sheet(work, key, met, lum):
    """The 4x sheet as an L image."""
    W = lum.shape[1] * SCALE
    H = lum.shape[0] * SCALE
    # calibrate on capitals and digits: their ink top is the cap line, bottom the baseline (cell relative)
    tops, bots = [], []
    for code, x0, y0, x1, y1 in met:
        c = char_of(code)
        if c.isupper() or c.isdigit():
            r = ink_rows(lum, x0, y0, x1, y1)
            if r:
                tops.append(r[0])
                bots.append(r[1])
    cap_top = float(np.median(tops)) if tops else 3.0
    base = float(np.median(bots)) if bots else 19.0
    cap_h = (base - cap_top + 1) * SCALE
    # font size whose "H" is cap_h tall
    probe = mono_face(CJK, 100)
    hb = probe.getbbox("H")
    size = max(8, int(round(100 * cap_h / (hb[3] - hb[1]))))
    font = mono_face(CJK, size)
    hb = font.getbbox("H")
    adv_half = font.getlength("M")  # monospace: every half-width glyph advances this far
    img = Image.new("L", (W, H), 0)
    for code, x0, y0, x1, y1 in met:
        c = char_of(code)
        cw, ch = (x1 - x0 + 1) * SCALE, (y1 - y0 + 1) * SCALE
        adv = font.getlength(c) or adv_half
        # draw at full width on a strip with the baseline where the original has it, then condense the strip to
        # the cell's width: one squeeze per cell width, so the sheet keeps one consistent letter shape
        strip_w = int(adv + 2 * size)
        extra = size  # room below the cell for long descenders
        strip = Image.new("L", (strip_w, ch + extra), 0)
        gy = (base + 1) * SCALE - hb[3]
        ImageDraw.Draw(strip).text((size, gy), c, font=font, fill=255)
        bb = strip.getbbox()
        if not bb:
            continue
        if bb[3] > ch - 1:
            # a descender past the cell: squeeze the glyph's height into the cell, its top kept where it is
            top = bb[1]
            glyph = strip.crop((0, top, strip_w, bb[3]))
            glyph = glyph.resize((strip_w, max(1, ch - 1 - top)), Image.LANCZOS)
            strip = Image.new("L", (strip_w, ch + extra), 0)
            strip.paste(glyph, (0, top))
        strip = strip.crop((0, 0, strip_w, ch))
        # the glyph's own advance box, kept centred as the font places it
        left = int(size - (adv_half if cw <= 12 * SCALE + 2 else adv) * 0.0)
        box_w = adv if adv > 0 else adv_half
        fx = min(1.0, (cw * 0.94) / box_w)
        part = strip.crop((int(size), 0, int(size + box_w + 0.5), ch))
        nw = max(1, int(round(part.width * fx)))
        part = part.resize((nw, ch), Image.LANCZOS)
        img.paste(part, (x0 * SCALE + (cw - nw) // 2, y0 * SCALE))
    return img, size


def main(work, game, out, langs):
    ix = json.load(open(os.path.join(work, "index.json")))
    os.makedirs(out, exist_ok=True)
    made = {}
    for key, e in ix.items():
        for src in e["sources"]:
            if not src.startswith("fonts/") or not src.endswith(".txd"):
                continue
            base_name = os.path.basename(src)[:-4]  # Movie_EN
            lang = base_name.rsplit("_", 1)[-1]
            if lang not in langs:
                continue
            met_path = os.path.join(game, os.path.dirname(src), f"{e['name']}.met")
            if not os.path.exists(met_path):
                continue
            lum = decode_sheet(work, key)
            img, size = make_sheet(work, key, read_met(met_path), lum)
            img.save(os.path.join(out, key + ".png"))
            made[key] = {"sheet": e["name"], "src": src, "size": size}
            break
    json.dump(made, open(os.path.join(out, "fonts.json"), "w"), indent=1)
    print(len(made), "font sheets")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:] or ["EN"])
