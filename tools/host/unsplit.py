import zlib,sys
parts=sys.argv[1:-1]; out=open(sys.argv[-1],'wb')
d=zlib.decompressobj(-15); crc=0; skip=86; n=0
for p in parts:
    with open(p,'rb') as f:
        if skip: f.read(skip); skip=0
        while True:
            b=f.read(1<<22)
            if not b: break
            o=d.decompress(b)
            if o: crc=zlib.crc32(o,crc); out.write(o); n+=len(o)
            if d.eof: break
    if d.eof: break
o=d.flush(); crc=zlib.crc32(o,crc); out.write(o); n+=len(o)
print('bytes',n,'crc %08x'%crc,'eof',d.eof)
