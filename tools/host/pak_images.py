#!/usr/bin/env python3
"""Pack ready-made replacement images (one per Xbox key, at their final size) into runtime .pak files.
usage: pak_images.py <dir of <key>.png> <out dir> <prefix>"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_pak  # noqa: E402
from PIL import Image  # noqa: E402


def main(src, out, prefix):
    m = {}
    for n in sorted(os.listdir(src)):
        if n.endswith(".png") and len(n) == 20:
            w, h = Image.open(os.path.join(src, n)).size
            m[n[:16]] = {"pack": os.path.join(src, n), "w": w, "h": h, "err": 0.0, "second": 1.0, "name": n}
    import json
    tmp = os.path.join(out, prefix + "_match.json")
    os.makedirs(out, exist_ok=True)
    json.dump(m, open(tmp, "w"))
    make_pak.main(tmp, out, prefix, cap=1)


if __name__ == "__main__":
    main(*sys.argv[1:4])
