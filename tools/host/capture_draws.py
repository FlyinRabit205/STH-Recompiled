# capture_draws.py <frame.txt> <rt...>: one line per draw onto those targets: id, rt, vs, widescreen code, texture, extent
import re,sys
f=sys.argv[1]
cur=None
for line in open(f):
    m=re.match(r'^(\d{4}) draw rt (\w+) .* vs (\w+) .*',line)
    if m:
        cur=m.groups(); hud=re.search(r'\| hud ([\d.]+)',line); tex=re.search(r'\| t0 (\w+) fmt \w+ (\d+x\d+)',line)
        info=[cur[0],cur[1],cur[2],hud.group(1) if hud else '-', tex.group(1)+' '+tex.group(2) if tex else 'notex']
        continue
    if cur and line.strip().startswith('verts'):
        pts=re.findall(r'\[ ([-\d.e+]+) ([-\d.e+]+) \]',line)
        if info[1] in sys.argv[2:] :
            xs=[float(p[0]) for p in pts]; ys=[float(p[1]) for p in pts]
            print(' '.join(info), 'x %.0f..%.0f y %.0f..%.0f'%(min(xs),max(xs),min(ys),max(ys)) if pts else 'nopos')
        cur=None
