"""Compare la version C++ (te_tests) au modkit Python sur un faux jeu.
usage : python3 tests/compare.py <te_tests> <dossier du modkit Python>"""
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

TE, MK = sys.argv[1], sys.argv[2]
sys.path.insert(0, MK)
sys.path.insert(0, os.path.join(MK, "tests"))
from triforce_modkit import bfres, bntx, byml as B, cli, editor, sarc, terrain, textmap  # noqa: E402
import test_bfres  # noqa: E402
import test_bntx  # noqa: E402
import test_editor  # noqa: E402
import test_modkit  # noqa: E402
import test_terrain3d  # noqa: E402
import zstandard  # noqa: E402

ok = fail = 0


def check(name, cond, detail=""):
    global ok, fail
    if cond:
        ok += 1
    else:
        fail += 1
        print("ÉCHEC", name, detail)


def run(*a):
    r = subprocess.run([TE, *a], capture_output=True)
    if r.returncode not in (0,):
        print("  (te_tests %s : %s)" % (a[0], r.stderr.decode(errors="replace").strip()))
    return r.stdout.decode()


tmp = tempfile.mkdtemp()
try:
    # -- BYML : texte, aller-retour, relecture du texte
    for i, root in enumerate([test_modkit.sample_root(), test_editor.rich_map()]):
        for big, ver in ((False, 7), (True, 3)):
            raw = B.dump(B.Document(root, ver, big))
            p = os.path.join(tmp, "d%d%d.byml" % (i, big))
            open(p, "wb").write(raw)
            txt = textmap.to_text(B.load(raw))
            check("byml2text %d %s" % (i, big), run("byml2text", p) == txt)
            run("bymlroundtrip", p, p + ".rt")
            check("byml aller-retour %d %s" % (i, big), open(p + ".rt", "rb").read() == raw)
            open(p + ".json", "w", encoding="utf-8").write(txt)
            run("text2byml", p + ".json", p + ".bin")
            check("text2byml %d %s" % (i, big), open(p + ".bin", "rb").read() == B.dump(textmap.from_text(txt)))
            check("propriétés %d" % i, run("web", p).strip() == "identique")
    # flottants : écriture la plus courte, comme Python
    vals = [0.1, 1.0, -0.0, 3.1415927, 1e-7, 123456.79, 1e20, 2.5e-30, float("inf")] + [random.uniform(-1e4, 1e4) for _ in range(300)]
    root = {"F": [B.F32(v) for v in vals], "D": [B.F64(v) for v in vals[:-1]]}
    p = os.path.join(tmp, "f.byml")
    open(p, "wb").write(B.dump(B.Document(root, 7, False)))
    a, b = run("byml2text", p), textmap.to_text(B.load(open(p, "rb").read()))
    if a != b:
        # (les doubles peuvent s'écrire autrement ; ils doivent se relire à l'identique)
        open(p + ".json", "w").write(a)
        run("text2byml", p + ".json", p + ".bin")
        check("flottants relus", B.load(open(p + ".bin", "rb").read()).root == B.load(open(p, "rb").read()).root)
        fa = [l for l in a.split("\n") if "!f64" not in l]
        fb = [l for l in b.split("\n") if "!f64" not in l]
        check("flottants f32 écrits comme Python", fa == fb)
    else:
        check("flottants", True)

    # -- SARC
    files = {"A/b.txt": b"x" * 33, "Model/X.bfres": b"FRES" + bytes(100), "Z": b"", "Ünï/çode": b"\x01\x02"}
    raw = sarc.dump(sarc.Sarc(files, aligns={"Model/X.bfres": 0x1000}))
    p = os.path.join(tmp, "a.sarc")
    open(p, "wb").write(raw)
    run("sarc", p, p + ".rt")
    check("sarc aller-retour", open(p + ".rt", "rb").read() == sarc.dump(sarc.load(raw)))

    # -- ASTC (vecteurs de l'encodeur de référence)
    vec = os.path.join(MK, "tests", "astc")
    n = 0
    for f in sorted(os.listdir(vec)):
        if not f.endswith(".astc"):
            continue
        hdr = f.startswith("hdr_")
        run("astc", os.path.join(vec, f), "1" if f.endswith("_s.astc") else "0", "1" if hdr else "0", os.path.join(tmp, "o.rgba"))
        got, ref = open(os.path.join(tmp, "o.rgba"), "rb").read(), open(os.path.join(vec, f[:-5] + ".rgba"), "rb").read()
        good = len(got) == len(ref) and (max(abs(x - y) for x, y in zip(got, ref)) <= 1 if hdr else got == ref)
        check("astc " + f, good)
        n += 1
    check("vecteurs astc présents", n >= 20)

    # -- BFRES et BNTX
    p = os.path.join(tmp, "m.bfres")
    open(p, "wb").write(test_bfres.make_bfres())
    m = bfres.read(test_bfres.make_bfres())
    s = sum(m.positions) + sum(m.uvs) + sum(m.indices)
    want = "%d %d %d%s %.3f" % (len(m.positions) // 3, len(m.indices), len(m.subs),
                                "".join(" [%d %d %s]" % (a, c, t or "") for a, c, t in m.subs), s)
    check("bfres", run("bfres", p).strip() == want, run("bfres", p) + " / " + want)
    for kw in ({}, {"fmt": 0x3301, "w": 64, "h": 48}):
        data = test_bntx.make_bntx(**kw)
        p = os.path.join(tmp, "t.bntx")
        open(p, "wb").write(data)
        code, w, h, blocks, name, srgb, cs = bntx.extract(data)
        line = run("bntx", p, p + ".bin").split()
        check("bntx %s" % kw, line[:4] == [str(code), str(w), str(h), str(int(srgb))] and open(p + ".bin", "rb").read() == blocks, line)

    # -- faux jeu : modèles, terrain, cartes, construction du mod
    class T:
        pass
    t = T()
    test_editor.ModelTest.setUp(t)
    R = t.romfs
    out = run("model", R, "Enemy_Child").split()
    m = bfres.read(test_bfres.make_bfres())
    check("modèle d'un acteur ($parent)", out == [str(len(m.positions) // 3), str(len(m.indices)), str(len(m.subs))], out)
    tiles = [("0100000000", 0.0, 0.0, 1.0)]
    for i, (dx, dz) in enumerate([(-.25, -.25), (.25, -.25), (-.25, .25), (.25, .25)]):
        tiles.append(("02000000%02X" % (i + 1), dx, dz, 0.5))
    os.makedirs(os.path.join(R, "TerrainArc", "Champ"))
    open(os.path.join(R, "TerrainArc", "Champ.tscb"), "wb").write(test_terrain3d.make_tscb(tiles))
    for nm, *_ in tiles:
        open(os.path.join(R, "TerrainArc", "Champ", nm + ".hght"), "wb").write(
            test_terrain3d.make_hght(lambda x, y: (x + 2 * y) / 1000.0))
    game = cli.Game(R)
    blob = terrain.build(game, "Champ")
    cnt, = struct.unpack_from("<I", blob, 4)
    hs = []
    o = 8
    first = None
    for k in range(cnt):
        cx, cz, side, n = struct.unpack_from("<3fI", blob, o)
        first = first or (cx, cz, side)
        o += 16
        h = struct.unpack_from("<%df" % (n * n), blob, o)
        o += 4 * n * n + n * n
        o = (o + 3) // 4 * 4
        hs += [v for v in h if v == v]
    line = run("terrain", R, "Champ")
    want = "%d tuiles, n=%d, somme %.3f, centre %.3f %.3f %.3f" % (cnt, n, sum(hs), *first)
    check("terrain", want in line, line + " / " + want)

    work = t.work
    mapj_rel = "Banc/Kokiri/Kokiri.bcett.byml.zs"
    open(os.path.join(R, *mapj_rel.split("/")), "wb").write(
        zstandard.ZstdCompressor(dict_data=t.d3).compress(B.dump(B.Document(test_editor.rich_map(), 7, False))))
    check("liste des cartes", mapj_rel + " 0" in run("maps", R, work))
    out = run("editmap", R, work, mapj_rel)
    jpath = out.split("\n")[0]
    doc = textmap.from_text(open(jpath, encoding="utf-8").read())
    acts = doc.root["Actors"]
    ref = test_editor.rich_map()["Actors"]
    check("carte : objet déplacé", float(acts[0]["Translate"][0]) == float(ref[0]["Translate"][0]) + 10)
    check("carte : copie avec nouveaux identifiants", acts[2]["Gyaml"] == ref[1]["Gyaml"] and acts[2]["Hash"] != ref[1]["Hash"]
          and isinstance(acts[2]["Hash"], B.U64) and isinstance(acts[2]["SRTHash"], B.U32))
    check("carte : objet supprimé, avertissement", len(acts) == 4 and "Enemy_Deku" in out, out)
    check("carte : relue par Python", editor.Editor(R, work, os.path.join(t.tmp, "m")).load_map(mapj_rel)["origine"] == "travail")
    # construction : même résultat que la version Python
    os.makedirs(os.path.join(work, "Pack", "Actor", "Enemy.pack.zs", "Component"), exist_ok=True)
    open(os.path.join(work, "Pack", "Actor", "Enemy.pack.zs", "Component", "Enemy.bgyml.json"), "w", encoding="utf-8").write(
        textmap.to_text(B.Document({"Life": 30, "Speed": B.F32(2.0)}, 7, False)))
    mods_py, mods_cpp = os.path.join(t.tmp, "mods_py"), os.path.join(t.tmp, "mods_cpp")
    import io
    from contextlib import redirect_stdout
    with redirect_stdout(io.StringIO()):
        cli.main(["construire", "Test", "--romfs", R, "--travail", work, "--mods", mods_py])
    log = run("build", R, work, mods_cpp, "Test")
    check("construction : journal", "Mod construit" in log, log)
    zd = cli.Game(R).zs
    for root_dir, _, fl in os.walk(mods_py):
        for f in fl:
            a = os.path.join(root_dir, f)
            b = os.path.join(mods_cpp, os.path.relpath(a, mods_py))
            if not os.path.isfile(b):
                check("construction : " + f, False, "absent en C++")
                continue
            da, db = open(a, "rb").read(), open(b, "rb").read()
            if f.endswith(".zs"):
                da, db = zd.unpack(da), zd.unpack(db)
            if da[:2] in (b"YB", b"BY"):
                check("construction : " + f, B.load(da).root == B.load(db).root)
            elif da[:4] == b"SARC":
                sa, sb = sarc.load(da), sarc.load(db)
                check("construction : " + f, sorted(sa.files) == sorted(sb.files)
                      and all(zd.unpack(sa.files[k]) == zd.unpack(sb.files[k]) for k in sa.files))
            else:
                check("construction : " + f, da == db)
    test_modkit.EndToEndTest.tearDown(t)
finally:
    shutil.rmtree(tmp, ignore_errors=True)
print("%d vérifications réussies, %d échecs" % (ok, fail))
sys.exit(1 if fail else 0)
