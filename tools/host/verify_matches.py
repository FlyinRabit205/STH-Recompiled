#!/usr/bin/env python3
"""Second check of pack matches at the Xbox texture's own size: the error relative to the image's own contrast.

usage: verify_matches.py <work dir> <match.json> <out.json> [threshold]
match_pack.py's 16x16 thumbnails let dark, sparse effect textures (sparks, rain, lightning on black) match
anything else dark. Here both images are compared at the Xbox size (alpha-weighted colour), and the mean squared error
is divided by the images' mean variance: unrelated sparse images score near or above 1, true matches far below.
Writes the matches that pass, and prints the rejected ones."""
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_pak  # noqa: E402
import xbtex  # noqa: E402


def rgba(work, k):
    raw = open(os.path.join(work, "tex", k + ".bin"), "rb").read()
    t = xbtex.Tex()
    t.fmt = raw[0]
    t.w, t.h = struct.unpack_from("<HH", raw, 1)
    t.palette, t.data = (raw[5:1029], raw[1029:]) if t.fmt == 0x0B else (None, raw[5:])
    return xbtex.decode(t)


def prep(a):
    a = a.astype(np.float32) / 255.0
    return a[..., :3] * a[..., 3:4]


def main(work, match, out, thr=0.25):
    m = json.load(open(match))
    keep, bad = {}, []
    cache = {}
    for k, v in m.items():
        if not make_pak.accepted(v):
            continue
        x = rgba(work, k)
        h, w = x.shape[:2]
        key = (v["pack"], w, h)
        if key not in cache:
            cache[key] = prep(np.asarray(Image.open(v["pack"]).convert("RGBA").resize((w, h), Image.BOX)))
        p = cache[key]
        xa = prep(x)
        mse = float(np.mean((xa - p) ** 2))
        var = 0.5 * (float(xa.var()) + float(p.var())) + 1e-5
        score = mse / var
        v = dict(v, rel=score)
        if score <= thr:
            keep[k] = v
        else:
            bad.append((score, k, v["name"], v["pack"].split("/GUP/")[-1][:60]))
    json.dump(keep, open(out, "w"), indent=1)
    bad.sort(reverse=True)
    for b in bad:
        print("reject %.3f %s %-20s %s" % b)
    print(len(keep), "kept,", len(bad), "rejected")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]) if len(sys.argv) > 4 else 0.25)
