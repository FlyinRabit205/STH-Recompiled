#!/usr/bin/env python3
"""Paint HD Xbox face buttons into the menu sheets that carry multi-platform button art.

usage: xbox_buttons.py <work dir with index.json, tex/, match_ui.json> <out dir>
The sheets (gamemenu, pause_main) hold PlayStation, GameCube and Xbox buttons side by side. Every button slot gets
an Xbox button with the same meaning (confirm -> A, back -> B, ...) drawn glossy like the HD pack's own buttons:
coloured sphere, dark rim, top highlight, white letter with a dark outline. The sheet around the slots comes from the
pack's HD image. Output: <out>/<key>.png at 4x.
"""
import json
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont
from scipy import ndimage

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xbtex  # noqa: E402

S = 4  # sheet scale
SS = 4  # supersampling for drawing
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
# Duke / Controller S colours: (base, dark)
COL = {"A": ((86, 190, 40), (20, 80, 10)), "B": ((220, 40, 35), (90, 8, 8)),
       "X": ((40, 110, 230), (10, 30, 100)), "Y": ((250, 200, 30), (120, 80, 0))}
# slots: sheet name -> list of ((x, y) inside the slot, Xbox letter)
SLOTS = {
    "gamemenu": [((242, 189), "B"), ((142, 217), "A"), ((169, 216), "X"), ((198, 218), "Y"), ((226, 217), "A"),
                 ((142, 244), "Y"), ((170, 244), "B"), ((198, 244), "A"), ((226, 244), "Y")],
    "pause_main": [((184, 109), "Y"), ((212, 110), "Y"), ((242, 108), "Y")],
}


def dec(work, k):
    raw = open(os.path.join(work, "tex", k + ".bin"), "rb").read()
    t = xbtex.Tex()
    t.fmt = raw[0]
    t.w, t.h = struct.unpack_from("<HH", raw, 1)
    t.palette, t.data = (raw[5:1029], raw[1029:]) if t.fmt == 0x0B else (None, raw[5:])
    return xbtex.decode(t)


def button(d, letter):
    """An RGBA button d pixels across."""
    n = d * SS
    base, dark = (np.array(c, np.float32) for c in COL[letter])
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) + 0.5
    c = n / 2
    r = np.hypot(xx - c, yy - c) / c
    rim = 0.86
    # sphere shading: lit from the upper left
    lx, ly = (xx - c * 0.75) / c, (yy - c * 0.7) / c
    light = np.clip(1.15 - 0.55 * np.hypot(lx, ly), 0.0, 1.2)
    body = dark + (base - dark) * light[..., None]
    body = np.minimum(body * 1.0 + 25 * np.clip(light - 0.9, 0, 1)[..., None], 255)
    img = np.zeros((n, n, 4), np.float32)
    img[..., :3] = np.where((r < rim)[..., None], body, np.array([18, 18, 22], np.float32))
    img[..., 3] = np.clip((1.0 - r) * n * 0.5, 0, 1) * 255
    im = Image.fromarray(img.astype(np.uint8), "RGBA")
    # specular highlight
    hl = Image.new("L", (n, n), 0)
    ImageDraw.Draw(hl).ellipse((n * 0.24, n * 0.13, n * 0.72, n * 0.45), fill=120)
    hl = hl.filter(ImageFilter.GaussianBlur(n * 0.05))
    im = Image.composite(Image.new("RGBA", (n, n), (255, 255, 255, 255)), im, hl)
    im.putalpha(Image.fromarray(img[..., 3].astype(np.uint8)))
    # letter: white, dark outline
    f = ImageFont.truetype(FONT, int(n * 0.56))
    dr = ImageDraw.Draw(im)
    bb = dr.textbbox((0, 0), letter, font=f)
    pos = (c - (bb[0] + bb[2]) / 2, c - (bb[1] + bb[3]) / 2)
    dr.text(pos, letter, font=f, fill=(255, 255, 255, 255), stroke_width=int(n * 0.035),
            stroke_fill=tuple(int(v * 0.6) for v in dark) + (255,))
    return im.resize((d, d), Image.LANCZOS)


def main(work, out):
    ix = json.load(open(os.path.join(work, "index.json")))
    m = json.load(open(os.path.join(work, "match_ui.json")))
    os.makedirs(out, exist_ok=True)
    made = {}
    for k, e in ix.items():
        slots = SLOTS.get(e["name"])
        if not slots or not any(s.endswith("_EN.txd") for s in e["sources"]) or k not in m:
            continue
        x = dec(work, k)
        h, w = x.shape[:2]
        hd = Image.open(m[k]["pack"]).convert("RGBA").resize((w * S, h * S), Image.LANCZOS)
        arr = np.asarray(hd).copy()
        lab, _ = ndimage.label(x[..., 3] > 8)
        for (px, py), letter in slots:
            comp = lab == lab[py, px]
            ys, xs = np.where(comp)
            x0, y0, x1, y1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
            clear = ndimage.binary_dilation(comp, iterations=1)
            big = np.kron(clear, np.ones((S, S), bool))
            arr[big] = 0
            d = min(max(int(max(x1 - x0, y1 - y0)), 21), 23) * S  # kidney slots get a full-size button
            b = button(d, letter)
            tmp = Image.fromarray(arr, "RGBA")
            cx, cy = int(x0 + x1) * S // 2, int(y0 + y1) * S // 2
            tmp.alpha_composite(b, (cx - d // 2, cy - d // 2))
            arr = np.asarray(tmp).copy()
        Image.fromarray(arr, "RGBA").save(os.path.join(out, k + ".png"))
        made[k] = e["name"]
    json.dump(made, open(os.path.join(out, "buttons.json"), "w"), indent=1)
    print(made)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
