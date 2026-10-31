#!/usr/bin/env python3
"""Index every Xbox texture of the game: key, name, format, size, source file.
usage: index_textures.py <extracted game dir> <work dir>
Writes <work>/index.json and <work>/tex/<key>.bin (raw texture + palette, for decoding later)."""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xbtex  # noqa: E402

EXTRACT = os.environ.get("ONE_EXTRACT", "/home/claude/tools/one_extract")


def main(game, work):
    os.makedirs(os.path.join(work, "one"), exist_ok=True)
    index = {}
    files = []
    for root, _, names in os.walk(game):
        for n in names:
            files.append(os.path.join(root, n))
    for path in sorted(files):
        rel = os.path.relpath(path, game)
        low = path.lower()
        blobs = []
        if low.endswith(".one"):
            out = os.path.join(work, "one", rel.replace("/", "__"))
            os.makedirs(out, exist_ok=True)
            subprocess.run([EXTRACT, path, out], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for n in os.listdir(out):
                blobs.append((rel + ":" + n, os.path.join(out, n)))
        elif low.endswith((".txd", ".dff", ".bsp")):
            blobs.append((rel, path))
        for src, p in blobs:
            try:
                b = open(p, "rb").read()
            except OSError:
                continue
            for t in xbtex.parse_txd(b, src):
                k = f"{t.key:016x}"
                if k in index:
                    index[k]["sources"].append(src)
                    continue
                index[k] = {"name": t.name, "fmt": t.fmt, "w": t.w, "h": t.h, "sources": [src]}
                with open(os.path.join(work, "tex", k + ".bin"), "wb") as f:
                    f.write(bytes([t.fmt]) + t.w.to_bytes(2, "little") + t.h.to_bytes(2, "little") +
                            (t.palette or b"") + t.data)
    json.dump(index, open(os.path.join(work, "index.json"), "w"), indent=0)
    print(len(index), "textures")


if __name__ == "__main__":
    os.makedirs(os.path.join(sys.argv[2], "tex"), exist_ok=True)
    main(sys.argv[1], sys.argv[2])
