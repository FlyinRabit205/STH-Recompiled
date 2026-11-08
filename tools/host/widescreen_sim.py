# offline model of MenuPivots on a capture's screen draws (screen programs only; glyph draws lack positions)
import sys,re,subprocess
d=sys.argv[1]
import os; P=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'capture_draws.py')
out=subprocess.run(['python3',P,d+'/frame.txt','03D98000','03EC4000'],capture_output=True,text=True).stdout
W=640;f=0.75
bl=[]
for l in out.splitlines():
    m=re.search(r'(\w+) (\w+x\w+|notex|) ?x (-?[\d]+)\.\.(-?[\d]+) y (-?[\d]+)\.\.(-?[\d]+)',l)
    m=re.search(r'(notex|\w+ \w+x\w+) x (-?[\d]+)\.\.(-?[\d]+) y (-?[\d]+)\.\.(-?[\d]+)',l)
    if not m: continue
    tex=m.group(1);lo,hi,top,bot=map(float,m.groups()[1:])
    if lo<=W*.02 and hi>=W*.98: continue
    if tex=='notex' and top<=10 and bot>=470: continue
    if hi<=lo: continue
    bl.append(dict(tex=tex,lo=lo,hi=hi,top=top,bot=bot,id=l.split()[0]))
n=len(bl)
def touch(a,b,s): return a['lo']<b['hi']+s and b['lo']<a['hi']+s and a['top']<b['bot']+s and b['top']<a['bot']+s
def ov(a,b):
    w=min(a['hi'],b['hi'])-max(a['lo'],b['lo']); h=min(a['bot'],b['bot'])-max(a['top'],b['top'])
    if w<=0 or h<=0: return 0
    sm=min((a['hi']-a['lo'])*(a['bot']-a['top']),(b['hi']-b['lo'])*(b['bot']-b['top']))
    return w*h/sm
big=lambda b: b['hi']-b['lo']>=40 and b['bot']-b['top']>=40
ch=list(range(n))
def rt(i):
    while ch[i]!=i: i=ch[i]
    return i
for i in range(n):
    if not big(bl[i]): continue
    for j in range(i+1,n):
        if big(bl[j]) and bl[j]['tex']==bl[i]['tex'] and touch(bl[i],bl[j],1) and ov(bl[i],bl[j])<0.3: ch[rt(i)]=rt(j)
ext={}
for i in range(n):
    if not big(bl[i]): continue
    r=rt(i); e=ext.setdefault(r,[1e9,-1e9,1e9,-1e9,0]); b=bl[i]
    e[0]=min(e[0],b['lo']);e[1]=max(e[1],b['hi']);e[2]=min(e[2],b['top']);e[3]=max(e[3],b['bot']);e[4]+=1
deco=[]
for i in range(n):
    if not big(bl[i]): deco.append(False); continue
    e=ext[rt(i)]
    off=e[0]<-2 or e[1]>W+2 or e[2]<-2 or e[3]>482
    lg=e[1]-e[0]>=W*.8 or e[3]-e[2]>=480*.8
    deco.append(off and lg and e[4]>=2)
par=list(range(n))
def find(i):
    while par[i]!=i: i=par[i]
    return i
for i in range(n):
    if deco[i]: continue
    for j in range(i+1,n):
        if not deco[j] and touch(bl[i],bl[j],8): par[find(i)]=find(j)
lo={};hi={}
for i in range(n):
    if deco[i]: continue
    r=find(i); lo[r]=min(lo.get(r,1e9),bl[i]['lo']); hi[r]=max(hi.get(r,-1e9),bl[i]['hi'])
for i,b in enumerate(bl):
    if deco[i]: print(b['id'],'DECO stretched %.0f..%.0f'%(b['lo'],b['hi'])); continue
    r=find(i); p=(lo[r]+hi[r])/2
    sq=lambda x:p+(x-p)*f
    # display coords: 640-wide 16:9 screen
    print(b['id'],'%.0f..%.0f -> %.0f..%.0f (pivot %.0f, centre-squeeze %.0f..%.0f)'%(b['lo'],b['hi'],sq(b['lo']),sq(b['hi']),p,320+(b['lo']-320)*f,320+(b['hi']-320)*f))
