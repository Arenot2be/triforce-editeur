# bfsha (Nin_Ounce_NVN2) : modèles, options, programmes, code des étapes
import struct
class Bfsha:
    def __init__(s, path):
        s.d=open(path,'rb').read(); d=s.d
        s.models=[]
        i=0
        while True:
            b=0x80+0x100*i
            q=struct.unpack_from('<32Q',d,b)
            if q[0]==0 or q[0]>=len(d): break
            nm=s.str(q[0])
            if not nm or not nm.isprintable(): break
            m={'name':nm,'q':q,'base':b}
            tail=d[b+0xb0:b+0x100]
            m['nstatic']=struct.unpack_from('<H',d,b+0xe4)[0]; m['ndyn']=struct.unpack_from('<H',d,b+0xe6)[0]
            m['nprog']=struct.unpack_from('<H',d,b+0xe8)[0]
            m['keylen']=d[b+0xed] if False else None
            m['opts']=s.opts(q[1],q[2])+s.opts(q[3],q[4],dyn=True)
            m['bnsh']=q[21]
            s.models.append(m); i+=1
    def str(s,o):
        if not o or o>=len(s.d)-2: return None
        n=struct.unpack_from('<H',s.d,o)[0]; return s.d[o+2:o+2+n].decode('latin1')
    def dic(s,o):
        if not o: return []
        n=struct.unpack_from('<I',s.d,o+4)[0]
        return [s.str(struct.unpack_from('<Q',s.d,o+8+16*(k+1)+8)[0]) for k in range(n)]
    def opts(s,arr,dic,dyn=False):
        out=[]
        for k,n in enumerate(s.dic(dic)):
            o=arr+k*0x28
            ch=s.dic(struct.unpack_from('<Q',s.d,o+8)[0])
            cnt=s.d[o+0x18]; dflt=s.d[o+0x19]
            mask=struct.unpack_from('<I',s.d,o+0x20)[0]; word=s.d[o+0x24]; shift=s.d[o+0x25]
            out.append(dict(name=n,choices=ch,default=dflt,mask=mask,word=word,shift=shift,dyn=dyn))
        return out
    def keylen(s,m):
        return max(o['word'] for o in m['opts'])+1
    def programs(s,m):
        q=m['q']; d=s.d; kl=s.keylen(m)
        n=(q[18]-q[17])//64
        for i in range(n):
            p=q[17]+i*64
            var=struct.unpack_from('<Q',d,p+0x20)[0]
            key=struct.unpack_from(f'<{kl}I',d,q[18]+i*kl*4)
            yield i,var,key
    def stages(s,m,var):
        d=s.d; B=m['bnsh']
        bp=struct.unpack_from('<Q',d,var+0x10)[0]
        if not bp: return {}
        p=B+bp
        slots=struct.unpack_from('<12Q',d,p)
        names=['?','vertex','hull','domain','geometry','fragment','compute','task','mesh','?','?','?']
        out={}
        for k in range(1,9):
            c=slots[k]
            if not c: continue
            ctl,data,sz=struct.unpack_from('<QQQ',d,B+c+8)
            out[names[k]]=(B+data, sz&0xffffffff, B+ctl, sz>>32)
        return out
    def choice(s,key,o):
        v=(key[o['word']]&o['mask'])>>o['shift']
        return o['choices'][v] if v<len(o['choices']) else f'?{v}'
