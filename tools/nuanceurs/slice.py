# slice.py file.glsl var [maxlines] : lignes dont dépend var (définitions), dans l'ordre
import re,sys
L=open(sys.argv[1]).read().split('\n')
defs={}
pat=re.compile(r'^\s*(?:precise\s+)?(?:(?:float|uint|int|bool|[iu]?vec\d|double)\s+)?(_\d+)(?:\.[xyzw])?\s*=\s*(.*);\s*$')
for i,l in enumerate(L):
    m=pat.match(l)
    if m: defs.setdefault(m.group(1),[]).append(i)
seen=set(); out=set()
stack=[sys.argv[2]]
stop=set(sys.argv[4].split(',')) if len(sys.argv)>4 else set()
while stack:
    v=stack.pop()
    if v in seen or v in stop: continue
    seen.add(v)
    for i in defs.get(v,[]):
        out.add(i)
        for t in re.findall(r'_\d+',L[i].split('=',1)[1]):
            stack.append(t)
mx=int(sys.argv[3]) if len(sys.argv)>3 else 400
for i in sorted(out)[:mx]: print(i+1, L[i].strip()[:220])
print('#',len(out),'lines')
