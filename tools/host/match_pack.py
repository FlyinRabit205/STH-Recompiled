#!/usr/bin/env python3
"""Match a GameCube-version texture pack to the Xbox textures by image, for the runtime's textures/replacement.

usage: match_pack.py <work dir with index.json, tex/> <pack root> <out.json> <category>...
Each pack image's original size is in its name (tex1_WxH_...). Every Xbox texture is compared with the pack images of
the same size on 16x16 RGBA thumbnails; it takes the closest one when that one is close enough and clearly closer than
the next. Writes {xbox key: {pack, err, second, name}}.
"""
import json
import os
import re
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xbtex  # noqa: E402

THUMB = 16
MAX_ERR = 0.012     # mean squared error on 0..1 RGBA thumbnails
MARGIN = 1.6        # the runner-up must be this much worse


def thumb(rgba):
    im = Image.fromarray(rgba, "RGBA").resize((THUMB, THUMB), Image.BOX)
    a = np.asarray(im, dtype=np.float32) / 255.0
    # colour weighted by alpha, so invisible texels do not decide
    a[..., :3] *= a[..., 3:4]
    return a.reshape(-1)


def load_xbox(work):
    ix = json.load(open(os.path.join(work, "index.json")))
    out = {}
    for k, e in ix.items():
        raw = open(os.path.join(work, "tex", k + ".bin"), "rb").read()
        fmt = raw[0]
        w, h = struct.unpack_from("<HH", raw, 1)
        t = xbtex.Tex()
        t.fmt, t.w, t.h, t.name = fmt, w, h, e["name"]
        if fmt == 0x0B:
            t.palette, t.data = raw[5:5 + 1024], raw[5 + 1024:]
        else:
            t.palette, t.data = None, raw[5:]
        try:
            out[k] = (w, h, thumb(xbtex.decode(t)), e)
        except Exception as ex:  # noqa: BLE001
            print("decode failed", k, e["name"], ex)
    return out


def load_pack(root, cats):
    out = []
    pat = re.compile(r"tex1_(\d+)x(\d+)_")
    for cat in cats:
        for dirpath, _, names in os.walk(os.path.join(root, cat)):
            for n in names:
                m = pat.search(n)
                if not m or not n.lower().endswith((".dds", ".png")):
                    continue
                p = os.path.join(dirpath, n)
                try:
                    im = Image.open(p).convert("RGBA")
                except Exception as ex:  # noqa: BLE001
                    print("pack image failed", p, ex)
                    continue
                w, h = int(m.group(1)), int(m.group(2))
                out.append((w, h, thumb(np.asarray(im)), p))
    return out


def main(work, root, out_path, cats):
    xb = load_xbox(work)
    pack = load_pack(root, cats)
    print(len(xb), "xbox textures,", len(pack), "pack images")
    by_size = {}
    for w, h, v, p in pack:
        by_size.setdefault((w, h), []).append((v, p))
    result = {}
    for k, (w, h, v, e) in xb.items():
        cands = by_size.get((w, h))
        if not cands:
            continue
        errs = np.array([np.mean((v - c) ** 2) for c, _ in cands])
        order = np.argsort(errs)
        best = errs[order[0]]
        second = errs[order[1]] if len(order) > 1 else 1.0
        if best <= MAX_ERR and (second >= best * MARGIN or second - best > 0.004):
            result[k] = {"pack": cands[order[0]][1], "err": float(best), "second": float(second), "name": e["name"],
                         "w": w, "h": h}
    json.dump(result, open(out_path, "w"), indent=1)
    used = len({r["pack"] for r in result.values()})
    print(len(result), "xbox textures matched,", used, "distinct pack images used")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:])
