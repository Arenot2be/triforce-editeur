# fwd.py file vars depth : lignes qui utilisent ces variables, en suivant les résultats (profondeur)
import re,sys
L=open(sys.argv[1]).read().split('\n')
pat=re.compile(r'^\s*(?:precise\s+)?(?:(?:float|uint|int|bool|[iu]?vec\d)\s+)?(_\d+)(?:\.[xyzw])?\s*=\s*(.*);\s*$')
defs={}
for i,l in enumerate(L):
    m=pat.match(l)
    if m: defs[i]=(m.group(1),m.group(2))
cur=set(sys.argv[2].split(',')); out=set()
for d in range(int(sys.argv[3])):
    nxt=set()
    for i,(v,e) in defs.items():
        toks=set(re.findall(r'_\d+',e))
        if toks & cur and i not in out:
            out.add(i); nxt.add(v)
    cur=nxt
for i in sorted(out): print(i+1,L[i].strip()[:230])
