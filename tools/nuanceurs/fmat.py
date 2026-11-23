# matériaux d'un bfres TotK/OoT2 : options du nuanceur, samplers (noms via ExternalBinaryString)
import struct,re
from ebs import name as hname
class Mats:
    def __init__(s,path):
        s.d=open(path,'rb').read() if isinstance(path,str) else path
        s.mats=[]
        for m in re.finditer(b'FMAT',s.d): 
            try: s.mats.append(s.read(m.start()))
            except Exception as e: pass
    def str(s,o):
        n=struct.unpack_from('<H',s.d,o)[0]; return s.d[o+2:o+2+n].decode('latin1')
    def hdic(s,o):
        if not o: return []
        n=struct.unpack_from('<I',s.d,o+4)[0]
        return [struct.unpack_from('<Q',s.d,o+8+16*(k+1)+8)[0] for k in range(n)]
    def read(s,f):
        d=s.d
        nm=s.str(struct.unpack_from('<Q',d,f+8)[0])
        si=struct.unpack_from('<Q',d,f+0x10)[0]
        sa=struct.unpack_from('<Q',d,si)[0]
        saq=struct.unpack_from('<9Q',d,sa)
        arch=s.str(saq[0]); model=s.str(saq[1])
        optn=[hname(h) for h in s.hdic(saq[8])]
        sampk=[s.str(struct.unpack_from('<Q',d,saq[7]+8+16*(k+1)+8)[0]) for k in range(struct.unpack_from('<I',d,saq[7]+4)[0])]
        nattr,nsamp,nbool=d[si+0x44],d[si+0x45],d[si+0x46]
        nval=struct.unpack_from('<H',d,si+0x48)[0]
        bits=struct.unpack_from('<Q',d,struct.unpack_from('<Q',d,si+0x28)[0])[0]
        sv=struct.unpack_from('<Q',d,si+0x30)[0]
        idx=struct.unpack_from(f'<{nval}H',d,struct.unpack_from('<Q',d,si+0x38)[0])
        opts={}
        for j,oi in enumerate(idx):
            if j<nbool: opts[optn[oi]]=str((bits>>j)&1)
            else: opts[optn[oi]]=s.str(struct.unpack_from('<Q',d,sv+8*(j-nbool))[0])
        sp=struct.unpack_from('<Q',d,si+0x18)[0]
        samp={}
        for k in range(nsamp):
            v=struct.unpack_from('<Q',d,sp+8*k)[0]
            if v: samp[sampk[k]]=s.str(v)
        # textures du matériau
        return dict(name=nm,archive=arch,model=model,opts=opts,samplers=samp,allopts=optn)
