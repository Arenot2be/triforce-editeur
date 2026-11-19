"""Trouver dans le code du jeu (le `main` de ton dump, chargé par les outils de Triforce) le décodeur des modèles
MeshCodec (fichiers .bfres.mc), et en écrire le désassemblage dans un fichier texte à envoyer pour analyse.

Usage (depuis n'importe où, avec le Python de Triforce, celui qui a capstone) :
    python -I trouver_meshcodec.py <dossier source de Triforce> [fichier de sortie]

Ce qui est cherché :
  - les constantes « MCPK » (0x4B50434D) et « FMSH » (0x48534D46), écrites en deux moitiés (movz + movk) ;
  - les textes « MeshCodec » / « Meshoptimizer » du programme, et le code qui les adresse (adrp + add) ;
puis chaque fonction trouvée est désassemblée, avec celles qu'elle appelle (trois niveaux au plus).
Rien n'est modifié ; seul le fichier de sortie est écrit.
"""
import os
import struct
import sys

TARGETS = {"MCPK": 0x4B50434D, "FMSH": 0x48534D46}
TEXTS = [b"MeshCodec", b"Meshoptimizer", b"MCPK", b"FMSH"]
MAX_FUNCS = 90          # (fonctions désassemblées au plus)
MAX_FUNC_BYTES = 0x6000  # (taille d'une fonction au plus)
DEPTH = 3


def sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


class Code:
    """(mémoire du programme : base, octets, plages de code, noms connus)"""

    def __init__(self, base, mem, ranges, names):
        self.base, self.mem, self.ranges, self.names = base, mem, ranges, names
        self.sorted_names = sorted(names)

    def word(self, pc):
        o = pc - self.base
        return struct.unpack_from("<I", self.mem, o)[0] if 0 <= o <= len(self.mem) - 4 else 0

    def in_code(self, pc):
        return any(lo <= pc < hi for lo, hi, _ in self.ranges)

    def words(self, lo, hi):
        o = lo - self.base
        n = (hi - lo) // 4
        return struct.unpack_from("<%dI" % n, self.mem, o)


def find_constants(code):
    """(adresses où une constante cherchée est construite : movz Rd,#bas puis movk Rd,#haut,lsl 16)"""
    hits = []
    for lo, hi, mod in code.ranges:
        ws = code.words(lo, hi)
        for i, w in enumerate(ws):
            if (w & 0x7F800000) != 0x52800000 or ((w >> 21) & 3) != 0:   # (movz, 32 ou 64 bits, sans décalage)
                continue
            rd, imm = w & 31, (w >> 5) & 0xFFFF
            for name, val in TARGETS.items():
                if imm != (val & 0xFFFF):
                    continue
                for j in range(i + 1, min(i + 12, len(ws))):
                    k = ws[j]
                    if (k & 0x7F800000) == 0x72800000 and (k & 31) == rd and ((k >> 21) & 3) == 1:
                        if ((k >> 5) & 0xFFFF) == (val >> 16):
                            hits.append((lo + 4 * i, name))
                        break
    return hits


def find_text_refs(code):
    """(textes cherchés dans tout le programme, et le code qui en calcule l'adresse : adrp + add)"""
    found = []
    for t in TEXTS:
        at = code.mem.find(t)
        while at >= 0 and len(found) < 40:
            s = at
            while s > 0 and 32 <= code.mem[s - 1] < 127:
                s -= 1
            e = at
            while e < len(code.mem) and 32 <= code.mem[e] < 127:
                e += 1
            if all(a != code.base + s for a, _ in found):
                found.append((code.base + s, bytes(code.mem[s:e]).decode("ascii", "replace")))
            at = code.mem.find(t, e)
    want = {a: txt for a, txt in found}
    refs = []
    if not want:
        return found, refs
    for lo, hi, mod in code.ranges:
        ws = code.words(lo, hi)
        for i, w in enumerate(ws):
            if (w & 0x9F000000) != 0x90000000:   # (adrp)
                continue
            pc = lo + 4 * i
            imm = sx(((w >> 29) & 3) | (((w >> 5) & 0x7FFFF) << 2), 21)
            page = (pc & ~0xFFF) + (imm << 12)
            rd = w & 31
            for j in range(i + 1, min(i + 6, len(ws))):
                k = ws[j]
                if (k & 0xFFC00000) == 0x91000000 and ((k >> 5) & 31) == rd:   # (add Xd, Xrd, #imm)
                    tgt = page + ((k >> 10) & 0xFFF)
                    for a, txt in want.items():
                        if a <= tgt < a + len(txt):
                            refs.append((pc, txt))
                    break
    return found, refs


def is_prologue(w):
    return ((w & 0xFFC07FFF) == 0xA9807BFD      # stp x29, x30, [sp, #-n]!
            or (w & 0xFF8003FF) == 0xD10003FF   # sub sp, sp, #n
            or w == 0xD503233F                  # paciasp
            or (w & 0xFFE07FFF) == 0xF81F0FFE)  # str x30, [sp, #-n]!


def func_start(code, pc):
    if pc in code.names:
        return pc
    a = pc
    for _ in range(MAX_FUNC_BYTES // 4):
        prev = code.word(a - 4)
        if prev == 0xD65F03C0 or (prev >> 26) == 0x05 or prev == 0 or not code.in_code(a - 4):   # (ret, b, remplissage)
            if is_prologue(code.word(a)) or code.word(a - 4) in (0xD65F03C0, 0):
                return a
        if a in code.names:
            return a
        a -= 4
    return pc & ~3


def func_end(code, start):
    a = start
    while a < start + MAX_FUNC_BYTES:
        w = code.word(a)
        a += 4
        if a in code.names and a != start:
            return a
        if w == 0xD65F03C0 and (is_prologue(code.word(a)) or code.word(a) == 0):
            return a
    return a


def callees(code, lo, hi):
    out = []
    for i, w in enumerate(code.words(lo, hi)):
        if (w >> 26) == 0x25:   # (bl)
            t = lo + 4 * i + 4 * sx(w & 0x3FFFFFF, 26)
            if code.in_code(t) and t not in out:
                out.append(t)
    return out


def name_of(code, pc, inside=False):
    import bisect
    j = bisect.bisect_right(code.sorted_names, pc) - 1
    if j >= 0 and pc - code.sorted_names[j] < 0x10000:
        n = code.names[code.sorted_names[j]]
        return n if pc == code.sorted_names[j] else ("%s+%#x" % (n, pc - code.sorted_names[j]) if inside else "sub_%x" % pc)
    return "sub_%x" % pc


def disasm(code, lo, hi, out, marks):
    try:
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    except ImportError:
        md = None
    for pc in range(lo, hi, 4):
        w = code.word(pc)
        text = ".word %#010x" % w
        if md:
            ins = next(md.disasm(w.to_bytes(4, "little"), pc), None)
            if ins:
                text = "%s %s" % (ins.mnemonic, ins.op_str)
                if (w >> 26) == 0x25 or (w >> 26) == 0x05:
                    t = pc + 4 * sx(w & 0x3FFFFFF, 26)
                    text += "    ; " + name_of(code, t)
        out.write("  %#x: %08x  %s%s\n" % (pc, w, text, ("    <-- " + marks[pc]) if pc in marks else ""))


def run(code, out):
    hits = find_constants(code)
    texts, refs = find_text_refs(code)
    out.write("== constantes MCPK / FMSH construites dans le code : %d\n" % len(hits))
    for pc, n in hits:
        out.write("  %#x %s  (dans %s)\n" % (pc, n, name_of(code, pc, True)))
    out.write("\n== textes trouvés : %d\n" % len(texts))
    for a, t in texts:
        out.write("  %#x « %s »\n" % (a, t))
    out.write("\n== code qui adresse ces textes : %d\n" % len(refs))
    for pc, t in refs:
        out.write("  %#x « %s »  (dans %s)\n" % (pc, t, name_of(code, pc, True)))
    marks = {pc: n for pc, n in hits}
    marks.update({pc: "texte « %s »" % t for pc, t in refs})
    queue = [(func_start(code, pc), 0) for pc in list(marks)]
    seen = set()
    n = 0
    while queue and n < MAX_FUNCS:
        start, depth = queue.pop(0)
        if start in seen:
            continue
        seen.add(start)
        end = func_end(code, start)
        n += 1
        out.write("\n\n==== %s  [%#x, %#x)  niveau %d\n" % (name_of(code, start), start, end, depth))
        disasm(code, start, end, out, marks)
        if depth < DEPTH:
            for t in callees(code, start, end):
                queue.append((func_start(code, t), depth + 1))
    out.write("\n\n== %d fonctions désassemblées%s\n" % (n, " (limite atteinte)" if queue else ""))
    return hits, refs, n


def lz4_block(src, size):
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        t = src[i]
        i += 1
        lit = t >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        out += src[i:i + lit]
        i += lit
        if i >= n:
            break
        off = src[i] | src[i + 1] << 8
        i += 2
        m = t & 15
        if m == 15:
            while True:
                b = src[i]
                i += 1
                m += b
                if b != 255:
                    break
        m += 4
        st = len(out) - off
        if off >= m:
            out += out[st:st + m]
        else:
            for k in range(m):
                out.append(out[st + k])
    if len(out) != size:
        raise ValueError("LZ4 : %d octets au lieu de %d" % (len(out), size))
    return bytes(out)


def load_nso(path, src=None, base=0x80000000):
    """(un module du dump : segments texte, rodata, data à leur place en mémoire, à `base`)"""
    raw = open(path, "rb").read()
    if raw[:4] != b"NSO0":
        raise SystemExit("%s n'est pas un module NSO" % path)
    flags = struct.unpack_from("<I", raw, 0xC)[0]
    segs = [struct.unpack_from("<4I", raw, 0x10 + 0x10 * i) for i in range(3)]
    csize = struct.unpack_from("<3I", raw, 0x60)
    end = max(m + sz for (_, m, sz, _) in segs)
    mem = bytearray(end)
    for i, ((foff, moff, size, _), cs) in enumerate(zip(segs, csize)):
        d = raw[foff:foff + cs]
        if flags & (1 << 7):   # (ZBIC : décodé par l'outil de Triforce)
            if not src:
                raise SystemExit("main est compressé en ZBIC : donne aussi le dossier source de Triforce")
            sys.path.insert(0, os.path.join(src, "nx2"))
            import zbic
            d = zbic.decompress(d, size)
        elif flags & (1 << i):
            print("  décompression LZ4 du segment %d (%d Mo)…" % (i, size >> 20))
            d = lz4_block(d, size)
        mem[moff:moff + len(d)] = d[:size]
    text = segs[0]
    return Code(base, bytes(mem), [(base + text[1], base + text[1] + text[2], os.path.basename(path))], {})


def find_dirs(name, roots, want, depth=4):
    out = []
    for r in roots:
        if not os.path.isdir(r):
            continue
        for cur, dirs, files in os.walk(r):
            if cur[len(r):].count(os.sep) >= depth:
                dirs[:] = []
                continue
            dirs[:] = [d for d in dirs if not d.startswith(".") and d.lower() not in ("node_modules", "appdata", "$recycle.bin")]
            if os.path.basename(cur).lower() == name and os.path.exists(os.path.join(cur, want)):
                out.append(cur)
    return out


def locate(args):
    home = os.path.expanduser("~")
    here = os.path.dirname(os.path.abspath(__file__))
    exefs = src = None
    for a in args:
        a = os.path.abspath(a)
        if os.path.isfile(os.path.join(a, "main")):
            exefs = a
        elif os.path.isfile(os.path.join(a, "exefs", "main")):
            exefs = os.path.join(a, "exefs")
        elif os.path.isfile(os.path.join(a, "nx2", "kit.py")):
            src = a
    if not src:
        for d in [here, os.path.dirname(here), os.getcwd()]:
            if os.path.isfile(os.path.join(d, "nx2", "kit.py")):
                src = d
                break
    if not exefs and os.environ.get("OOT_EXEFS"):
        exefs = os.environ["OOT_EXEFS"]
    if not exefs and src:
        cfg = os.path.join(src, "work", "dump.txt")
        if os.path.exists(cfg):
            for line in open(cfg, encoding="utf-8"):
                line = line.strip()
                if line and not line.startswith("#") and os.path.isfile(os.path.join(line, "exefs", "main")):
                    exefs = os.path.join(line, "exefs")
                    break
    if not exefs:
        print("Recherche du dossier exefs du dump (avec le fichier main)…")
        roots = [os.path.join(home, d) for d in ("Games", "Desktop", "Bureau", "Documents", "Downloads", "Téléchargements")]
        roots += [d + ":\\" for d in "CDEFG"] if os.name == "nt" else []
        found = find_dirs("exefs", roots, "main")
        if found:
            exefs = found[0]
    if not src:
        roots = [os.path.join(home, d) for d in ("Desktop", "Bureau", "Documents", "Downloads", "Games")]
        for d in find_dirs("source", roots, os.path.join("nx2", "kit.py")):
            src = d
            break
    return exefs, src


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dest = os.path.join(os.path.expanduser("~"), "Documents", "Triforce-modkit", "analyse", "meshcodec_desassemblage.txt")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    try:
        exefs, src = locate(args)
        print("dossier exefs : %s" % exefs)
        print("dossier source de Triforce : %s" % src)
        if not exefs:
            raise SystemExit("Dossier exefs introuvable : donne son chemin, ex.  python trouver_meshcodec.py \"C:\\...\\exefs\"")
        if src:
            sys.path.insert(0, os.path.join(src, "nx2"))
        try:
            import capstone  # noqa: F401
        except ImportError:
            print("ATTENTION : capstone absent de ce Python : le fichier contiendra les instructions brutes (lisibles quand même)")
        mods = [m for m in ["main"] + ["subsdk%d" % i for i in range(10)] + ["sdk", "rtld"] if os.path.isfile(os.path.join(exefs, m))]
        print("modules : " + ", ".join(mods))
        with open(dest, "w", encoding="utf-8") as out:
            out.write("exefs : %s\nmodules : %s\n" % (exefs, ", ".join(mods)))
            total = 0
            base = 0x80000000
            for m in mods:
                print("Chargement de %s…" % m)
                try:
                    code = load_nso(os.path.join(exefs, m), src, base)
                except BaseException as e:
                    out.write("\n######## %s : illisible (%s)\n" % (m, e))
                    continue
                base += (len(code.mem) + 0xFFFFF) & ~0xFFFFF
                # (contrôle : le module est-il bien décompressé ? quelques textes connus)
                probe = {t: code.mem.count(t) for t in (b"nn::", b".bfres", b"zstd", b"Meshopt", b"meshopt", b"MeshCodec", b"MCPK", b"FMSH")}
                lo, hi, _ = code.ranges[0]
                out.write("\n######## %s : %.1f Mo, code %.1f Mo ; textes repères : %s\n" % (
                    m, len(code.mem) / 1e6, (hi - lo) / 1e6, ", ".join("%s×%d" % (k.decode(), v) for k, v in probe.items())))
                print("  recherche…")
                hits, refs, n = run(code, out)
                total += n
                print("  %d constantes, %d textes adressés, %d fonctions" % (len(hits), len(refs), n))
        print("%d fonctions désassemblées en tout" % total)
        print("Écrit dans " + dest)
    except BaseException as e:   # (toujours un fichier, avec l'erreur, pour savoir ce qui a coincé)
        import traceback
        msg = "".join(traceback.format_exception(type(e), e, e.__traceback__))
        print("ERREUR :\n" + msg)
        with open(dest, "w", encoding="utf-8") as out:
            out.write("ERREUR\n" + msg)
        print("Erreur écrite dans " + dest)
    if "--sans-pause" not in sys.argv:
        try:
            input("\nAppuie sur Entrée pour fermer…")
        except EOFError:
            pass


if __name__ == "__main__":
    main()
