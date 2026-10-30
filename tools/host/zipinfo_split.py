import struct,sys
z=open(sys.argv[1],'rb').read()
i=z.rfind(b'PK\x05\x06'); print('EOCD at',i, struct.unpack('<4s4H2LH',z[i:i+22]))
j=z.rfind(b'PK\x06\x06'); print('zip64 eocd at',j)
c=z.find(b'PK\x01\x02')
print('CD at',c)
h=struct.unpack('<4s6H3L5H2L',z[c:c+46]); print(h)
n=h[12];e=h[13]
print(z[c+46:c+46+n]); ex=z[c+46+n:c+46+n+e]; print(ex.hex())
