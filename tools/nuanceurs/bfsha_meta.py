# Dump bfsha metadata: models, static/dynamic options with choices, samplers, uniform blocks, uniforms
import struct,sys
d=open(sys.argv[1],'rb').read()
def s(o):
    if not o or o>=len(d)-2: return '?'
    n=struct.unpack_from('<H',d,o)[0]; return d[o+2:o+2+n].decode('latin1')
def dic(o):
    if not o: return []
    n=struct.unpack_from('<I',d,o+4)[0]
    return [s(struct.unpack_from("<Q",d,o+8+16*(k+1)+8)[0]) for k in range(n)]
def arr_names(base,n,stride):
    return [s(struct.unpack_from('<Q',d,base+i*stride)[0]) for i in range(n)]
i=0
out=[]
while True:
    b=0x80+0x100*i
    q=struct.unpack_from('<32Q',d,b)
    nm=s(q[0])
    if not nm or nm=='?' or not nm.isprintable() or q[0]==0: break
    out.append(f'#### model {nm}')
    so=dic(q[2]); do=dic(q[4])
    def opts(base,names,tag):
        for k,n in enumerate(names):
            o=base+k*0x28
            ch=dic(struct.unpack_from('<Q',d,o+8)[0])
            out.append(f'{tag} {n}: {" | ".join(ch)}')
    opts(q[1],so,'S'); opts(q[3],do,'D')
    for tag,ai,di in (('ATTR',7,8),('SAMP',11,12),('UBLK',13,15)):
        out.append(f'{tag}: '+' '.join(dic(q[di])))
    i+=1
print('\n'.join(out))
