#!/bin/bash
# snap.sh <run dir> <out png>: two frames from the running harness, first saved as png
cd "$1"; rm -f snap_1.tga snap_2.tga; touch snap
for i in $(seq 1 120); do [ -f snap_2.tga ] && break; sleep 0.5; done
python3 -I -c "
from PIL import Image; Image.open('snap_1.tga').convert('RGB').save('$2')"
grep -h "renderer: frame" logs/*.log | tail -1
