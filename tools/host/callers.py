import struct,sys,json
d=open('/home/claude/game/extracted/default.xbe','rb').read()
info=json.load(open('/home/claude/game/xbeinfo.json'))
targets=[int(a,16) for a in sys.argv[1:]]
for s in info['sections']:
    if not s['flags']&4: continue
    raw,va,n=s['raw'],s['va'],s['rsize']
    for i in range(raw,raw+n-5):
        if d[i]==0xE8 or d[i]==0xE9:
            t=va+(i-raw)+5+struct.unpack_from('<i',d,i+1)[0]
            if t in targets: print(f"{'call' if d[i]==0xE8 else 'jmp '} {va+i-raw:08X} -> {t:08X}  [{s['name']}]")
