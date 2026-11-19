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


def load_triforce(src):
    sys.path.insert(0, os.path.join(src, "nx2"))
    os.chdir(src)
    import kit
    import image as eden_image
    import nx_image  # noqa: F401  (chargé par image)
    img = eden_image.EdenImage(kit.need_exefs())
    base = min(m.base for m in img.modules)
    names = dict(getattr(img, "names", {}) or {})
    return Code(base, bytes(img.mem), list(img.code), names)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = os.path.abspath(sys.argv[1])
    dest = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(
        os.path.expanduser("~"), "Documents", "Triforce-modkit", "analyse", "meshcodec_desassemblage.txt")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    print("Chargement du programme du jeu (comme les outils de Triforce)…")
    code = load_triforce(src)
    print("Recherche…")
    with open(dest, "w", encoding="utf-8") as out:
        hits, refs, n = run(code, out)
    print("%d constantes, %d textes adressés, %d fonctions désassemblées" % (len(hits), len(refs), n))
    print("Écrit dans " + dest)


if __name__ == "__main__":
    main()
