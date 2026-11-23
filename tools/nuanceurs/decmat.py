import os
# decmat.py <bfres> <material> <tag> [assign] : décompile les programmes (vertex/fragment) d'un matériau, optimisés
import sys,subprocess
from match import *
from fmat import Mats
f,mn,tag=sys.argv[1:4]
want=sys.argv[4] if len(sys.argv)>4 else 'cubemap'
m=[x for x in Mats(f).mats if x['name']==mn][0]
r=find(m['opts'])
print(mn,len(r),'programmes')
for i,var,key in r:
    a=B.choice(key,OPT['gsys_assign_type']).replace('gsys_assign_','')
    if a!=want: continue
    for k,(off,sz,ctl,csz) in B.stages(M0,var).items():
        out=os.path.join(os.environ.get('TE_OUT','.'),f'{tag}_{a}_{k}.glsl')
        subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)),'dec.sh'),os.environ.get('TE_MODEL_BFSHA','Model.Product.110.product.Nin_Ounce_NVN2.bfsha'),hex(off),hex(ctl),out],capture_output=True)
        n=subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)),'opt.sh'),os.path.join(os.environ.get('TMPDIR','/tmp'),'_c.spv'),out.replace('.glsl','_O.glsl')],capture_output=True,text=True).stdout.strip()
        print(out.replace('.glsl','_O.glsl'),n)
    break
