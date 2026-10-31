import re,sys,glob,numpy as np,struct,time
cap,pid=sys.argv[1],int(sys.argv[2])
fs=sorted(glob.glob(cap+'/draws_*.txt'))
fs=[f for f in fs if open(f).read(10)][-12:]
def get(f):
    out=[]
    for b in open(f).read().split('DRAW ')[1:]:
        h=b.split('\n')[0]
        if 'rt=03A2B080' in h:
            w=re.search(r'  world ([^\n]*)',b); t=re.search(r'tex0=(\w+)',h).group(1); n=re.search(r'n=(\d+)',h).group(1)
            out.append((t+':'+n,w.group(1) if w else ''))
    return dict(out)
a=[get(f) for f in fs]
cands=[k for k in a[-1] if ''.join('C' if a[i].get(k)!=a[i+1].get(k) else '.' for i in range(len(a)-1)).count('C')>=4]
print('alternating', cands)
w=[float(x) for x in a[-1][cands[-1]].split()]
x,y,z=w[12],w[13],w[14]
maps=open(f'/proc/{pid}/maps').read().split('\n'); f=open(f'/proc/{pid}/mem','rb'); hits=[]
for l in maps:
    if not l: continue
    lo,hi=[int(v,16) for v in l.split()[0].split('-')]
    if not (0x100000000<=lo<0x200000000) or 'rw' not in l.split()[1]: continue
    try: f.seek(lo); buf=f.read(hi-lo)
    except Exception: continue
    arr=np.frombuffer(buf[:len(buf)//4*4],dtype=np.float32)
    with np.errstate(all='ignore'): idx=np.where(np.abs(arr-y)<2e-3)[0]
    for i in idx:
        if 0<i<len(arr)-1 and abs(arr[i-1]-x)<2e-3 and abs(arr[i+1]-z)<5e-3: hits.append(lo+(i-1)*4)
print('hits',[hex(h-0x100000000) for h in hits])
