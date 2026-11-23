import os
import sys,pickle,os
from bfsha import Bfsha
from fmat import Mats
B=Bfsha(os.environ.get('TE_MODEL_BFSHA','Model.Product.110.product.Nin_Ounce_NVN2.bfsha'))
M0=B.models[0]
OPT={o['name']:o for o in M0['opts']}
PROGS=list(B.programs(M0))
def find(mopts, extra={}):
    want=dict(mopts); want.update(extra)
    res=[]
    for i,var,key in PROGS:
        ok=True
        for n,v in want.items():
            o=OPT.get(n)
            if not o: continue
            if B.choice(key,o)!=v: ok=False; break
        if ok: res.append((i,var,key))
    return res
