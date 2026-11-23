import os
import struct
e=open(os.environ.get('TE_EBS','ExternalBinaryString.bfres'),'rb').read()
def _s(b,o):
    n=struct.unpack_from('<H',b,o)[0]; return b[o+2:o+2+n].decode('latin1')
H=list(struct.unpack_from('<454Q',e,0xf0))
NAMES=[_s(e,struct.unpack_from('<Q',e,0xf28+16*(k+1)+8)[0]) for k in range(454)]
IDX={h:i for i,h in enumerate(H)}
def name(h): return NAMES[IDX[h]] if h in IDX else f'#{h:016x}'
