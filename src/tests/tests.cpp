// Outil de vérification des formats (comparé au modkit Python par tests/compare.py).
#include <cstdio>
#include <iostream>

#include "core/astc.hpp"
#include "core/bfres.hpp"
#include "core/bntx.hpp"
#include "core/game.hpp"
#include "core/mapdoc.hpp"
#include "core/meshcodec.hpp"
#include "core/models.hpp"
#include "core/terrain.hpp"
#include "core/textmap.hpp"

using namespace te;

static void out(const Bytes& b, const char* path) { writeFileAtomic(u8path(path), b); }

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage : te_tests <commande> ...\n"); return 2; }
    std::string cmd = argv[1];
    try {
        if (cmd == "byml2text") {   // fichier BYML (décompressé) -> texte
            std::string t = toText(bymlLoad(readFile(u8path(argv[2]))));
            std::fwrite(t.data(), 1, t.size(), stdout);
        } else if (cmd == "text2byml") {   // texte -> BYML
            out(bymlDump(fromText(readText(u8path(argv[2])))), argv[3]);
        } else if (cmd == "bymlroundtrip") {
            out(bymlDump(bymlLoad(readFile(u8path(argv[2])))), argv[3]);
        } else if (cmd == "sarc") {   // archive -> réécrite
            out(Sarc::load(readFile(u8path(argv[2]))).dump(), argv[3]);
        } else if (cmd == "astc") {   // blocs.astc w h srgb hdr -> rgba
            Bytes a = readFile(u8path(argv[2]));
            int bw = a[4], bh = a[5], w = a[7] | a[8] << 8, h = a[10] | a[11] << 8;
            bool srgb = std::atoi(argv[3]) != 0, hdr = std::atoi(argv[4]) != 0;
            out(astcDecode(a.data() + 16, a.size() - 16, w, h, bw, bh, srgb, hdr, false), argv[5]);
        } else if (cmd == "bfres") {
            Bytes f = readFile(u8path(argv[2]));
            if (isMeshCodec(f)) f = meshCodecUnpack(f);
            Mesh m = bfresRead(f, argc > 3 ? argv[3] : "");
            std::printf("%zu %zu %zu", m.positions.size() / 3, m.indices.size(), m.subs.size());
            for (auto& s : m.subs) std::printf(" [%u %u %s]", s.start, s.count, s.tex.c_str());
            double sum = 0;
            for (float f : m.positions) sum += f;
            for (float f : m.uvs) sum += f;
            for (uint32_t i : m.indices) sum += i;
            std::printf(" %.3f\n", sum);
        } else if (cmd == "mc") {   // .bfres.mc -> BFRES et tampons décodés
            out(meshCodecUnpack(readFile(u8path(argv[2]))), argv[3]);
        } else if (cmd == "bntx") {
            TexData t = bntxExtract(readFile(u8path(argv[2])), 512, argc > 4 ? argv[4] : "");
            std::printf("%d %d %d %d %08x\n", t.code, t.w, t.h, t.srgb ? 1 : 0, t.compSel);
            out(t.blocks, argv[3]);
        } else if (cmd == "zs") {   // romfs fichier -> décompressé
            Game g(u8path(argv[2]));
            out(g.read(argv[3]), argv[4]);
        } else if (cmd == "build") {   // romfs travail mods nom
            Game g(u8path(argv[2]));
            return buildMod(g, u8path(argv[3]), u8path(argv[4]), argv[5], [](const std::string& s) { std::puts(s.c_str()); });
        } else if (cmd == "maps") {
            Game g(u8path(argv[2]));
            for (auto& m : MapDoc::listMaps(g, u8path(argv[3]))) std::printf("%s %d\n", m.path.c_str(), m.edited ? 1 : 0);
        } else if (cmd == "editmap") {   // romfs travail carte : déplace le 1er objet, duplique le 2e, supprime le 3e, enregistre
            Game g(u8path(argv[2]));
            MapDoc d;
            d.open(g, u8path(argv[3]), argv[4]);
            auto& L = d.layers.at(0);
            auto tx = d.begin();
            double p[3];
            d.touch(tx, L.recs[0]);
            vec3(L.recs[0]->value, posKey(L.recs[0]->value), p);
            p[0] += 10;
            setVec(L.recs[0]->value, posKey(L.recs[0]->value), p);
            auto dup = std::make_shared<Rec>(*L.recs[1]);
            dup->dup = true;
            L.recs.insert(L.recs.begin() + 2, dup);
            L.recs.erase(L.recs.begin() + 3);
            d.commit(std::move(tx));
            d.undo();
            d.redo();
            std::vector<std::string> warn;
            std::printf("%s\n", pathStr(d.save(g, u8path(argv[3]), warn)).c_str());
            for (auto& w : warn) std::printf("! %s\n", w.c_str());
        } else if (cmd == "model") {   // romfs acteur
            Game g(u8path(argv[2]));
            ModelIndex mi(g);
            Mesh m = mi.load(argv[3]);
            std::printf("%zu %zu %zu\n", m.positions.size() / 3, m.indices.size(), m.subs.size());
        } else if (cmd == "terrain") {   // romfs scène
            Game g(u8path(argv[2]));
            for (auto& s : terrainScenes(g)) std::printf("scene %s\n", s.c_str());
            auto tiles = terrainBuild(g, argv[3]);
            double sum = 0;
            for (auto& t : tiles) for (float h : t.h) if (h == h) sum += h;
            std::printf("%zu tuiles, n=%d, somme %.3f, centre %.3f %.3f %.3f\n", tiles.size(), tiles.empty() ? 0 : tiles[0].n, sum,
                        tiles.empty() ? 0 : tiles[0].cx, tiles.empty() ? 0 : tiles[0].cz, tiles.empty() ? 0 : tiles[0].side);
        } else if (cmd == "web") {   // texte des propriétés -> relu avec le modèle de types
            Document d = bymlLoad(readFile(u8path(argv[2])));
            std::string t = toWebText(d.root);
            Value back = fromWeb(jsonParse(t), &d.root);
            std::printf("%s\n", same(back, d.root) ? "identique" : "DIFFÉRENT");
        } else {
            std::fprintf(stderr, "commande inconnue\n");
            return 2;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERREUR : %s\n", e.what());
        return 1;
    }
    return 0;
}
