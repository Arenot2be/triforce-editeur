// Outil de vérification des formats (comparé au modkit Python par tests/compare.py).
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "core/astc.hpp"
#include "core/bfres.hpp"
#include "core/matshader.hpp"
#include "core/bntx.hpp"
#include "core/game.hpp"
#include "core/mapdoc.hpp"
#include "core/meshcodec.hpp"
#include "core/models.hpp"
#include "core/terrain.hpp"
#include "core/textmap.hpp"
#include "core/texsearch.hpp"
#include "core/bake.hpp"
#include "core/effects.hpp"
#include "core/foliage.hpp"

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
            if (const char* e = std::getenv("TE_EBS")) setShaderNames(ebsParse(readFile(u8path(e))));
            Mesh m = bfresRead(f, argc > 3 ? argv[3] : "");
            std::printf("%zu %zu %zu", m.positions.size() / 3, m.indices.size(), m.subs.size());
            for (auto& s : m.subs) std::printf(" [%u %u %s]", s.start, s.count, s.tex.c_str());
            if (std::getenv("TE_KIND")) for (auto& s : m.subs) std::printf("  matériau %s : genre %d, normales %s, courant %s\n", s.material.c_str(), s.kind, s.nrm.c_str(), s.flow.c_str());
            if (std::getenv("TE_EBS")) for (auto& s : m.subs) std::printf("\n  %s : couleur %s | normales %s | rugosité %s (canal %d%s) | couche 2 %s %s %s%s | %s", s.material.c_str(), s.tex.c_str(), s.nmap.c_str(), s.rgh.c_str(), s.rghCh, s.rah ? ", RAH" : "", s.tex2.c_str(), s.nmap2.c_str(), s.rah2.c_str(), s.alphaTest ? " | alpha" : "", s.shaderInfo.c_str());
            double sum = 0;
            for (float f : m.positions) sum += f;
            for (float f : m.uvs) sum += f;
            for (uint32_t i : m.indices) sum += i;
            std::printf(" %.3f\n", sum);
            if (const char* obj = std::getenv("TE_OBJ")) {   // (export OBJ pour vérifier à l'œil)
                FILE* o = std::fopen(obj, "w");
                for (size_t k = 0; k + 2 < m.positions.size(); k += 3) std::fprintf(o, "v %f %f %f\n", m.positions[k], m.positions[k + 1], m.positions[k + 2]);
                for (size_t k = 0; k + 1 < m.uvs.size(); k += 2) std::fprintf(o, "vt %f %f\n", m.uvs[k], m.uvs[k + 1]);
                for (size_t k = 0; k + 2 < m.indices.size(); k += 3) std::fprintf(o, "f %u/%u %u/%u %u/%u\n", m.indices[k] + 1, m.indices[k] + 1, m.indices[k + 1] + 1, m.indices[k + 1] + 1, m.indices[k + 2] + 1, m.indices[k + 2] + 1);
                std::fclose(o);
            }
        } else if (cmd == "cherche") {   // romfs nom... -> rapport de recherche approfondie, puis lecture des trouvées
            Game g(u8path(argv[2]));
            TexSearch s;
            for (int i = 3; i < argc; i++) s.names.push_back(argv[i]);
            searchTextures(g, s, 4);
            std::string r = s.report();
            std::fwrite(r.data(), 1, r.size(), stdout);
            for (size_t k = 0; k < s.names.size(); k++)
                for (auto& u : s.usable[k]) {
                    TexData t = bntxExtract(g.readTex(u), 64, s.names[k]);
                    std::printf("lu : %s %dx%d %s\n", s.names[k].c_str(), t.w, t.h, t.format.c_str());
                }
        } else if (cmd == "bake") {   // romfs scène -> éclairage précalculé lu
            Game g(u8path(argv[2]));
            std::string st;
            auto m = bakeLoad(g, argv[3], &st);
            std::printf("%s\n", st.c_str());
            int k = 0;
            for (auto& [h, e] : m) {
                if (k++ >= 5) break;
                std::printf("%llu %s %s", (unsigned long long)h, e.model.c_str(), e.tex.c_str());
                for (auto& bm : e.mats) std::printf(" [%s %d %.3f %.3f %.4f %.4f]", bm.material.c_str(), bm.index, bm.off[0], bm.off[1], bm.scale[0], bm.scale[1]);
                std::printf("\n");
            }
        } else if (cmd == "probes") {   // fichiers de sondes -> éclairement ambiant en quelques points
            std::vector<Bytes> fs;
            for (int i = 2; i < argc; i++) fs.push_back(readFile(u8path(argv[i])));
            // (TE_ROMFS : prendre la couleur du ciel du jeu, soleil de l'éditeur)
            float skyL[3], sunC[3];
            const char* rf = std::getenv("TE_ROMFS");
            SkyEnv se;
            if (rf) { Game g(u8path(rf)); se = skyLoad(g, nullptr); if (se.ok) { se.at(0.91863f, sunC, skyL); std::printf("ciel %.4f %.4f %.4f\n", skyL[0], skyL[1], skyL[2]); } }
            ProbeSet ps = probesParse(fs, se.ok ? skyL : nullptr);
            std::printf("%zu sondes, référence %.4f\n", ps.size(), ps.ref);
            for (size_t k = 0; k < ps.size(); k += std::max<size_t>(1, ps.size() / 10)) {
                float o[12];
                if (!ps.ambient(&ps.pos[3 * k], o)) continue;
                std::printf("  (%.0f %.0f %.0f) haut %.2f %.2f %.2f  bas %.2f %.2f %.2f\n", ps.pos[3 * k], ps.pos[3 * k + 1], ps.pos[3 * k + 2],
                            o[0] + o[5], o[1] + o[9], o[2] + o[7], o[0] - o[5], o[1] - o[9], o[2] - o[7]);
            }
        } else if (cmd == "sky") {   // romfs -> couleur du soleil et du ciel selon la hauteur du soleil
            Game g(u8path(argv[2]));
            std::string st;
            SkyEnv e = skyLoad(g, &st);
            std::printf("%s\nzénith %.4f %.4f %.4f\n", st.c_str(), e.zenithT[0], e.zenithT[1], e.zenithT[2]);
            for (float deg : {-5.f, 0.f, 5.f, 10.f, 20.f, 40.f, 66.7f, 90.f}) {
                float sun[3], sky[3];
                e.at(std::sin(deg / 57.29578f), sun, sky);
                std::printf("  %5.1f°  soleil %.3f %.3f %.3f   ciel %.4f %.4f %.4f\n", deg, sun[0], sun[1], sun[2], sky[0], sky[1], sky[2]);
            }
        } else if (cmd == "palette") {   // romfs préfixe-de-scène [heure] -> palette d'environnement retenue
            Game g(u8path(argv[2]));
            std::string st;
            EnvPalette e = envLoad(g, argv[3], argc > 4 ? std::atoi(argv[4]) : 12, &st);
            std::printf("%s\ncouleur du soleil %.3f %.3f %.3f ; exposition %.3f ; renvoi %.2f, ciel %.2f, ambiance %.2f\n", st.c_str(),
                        e.sunColor[0], e.sunColor[1], e.sunColor[2], e.exposure(), e.bounceScale, e.skyBounceScale, e.ambientIntensity);
        } else if (cmd == "foliage") {   // romfs préfixe-de-scène -> feuillage lu
            Game g(u8path(argv[2]));
            std::string st;
            FoliageSet f = foliageLoad(g, argv[3], &st);
            std::printf("%s\n", st.c_str());
            for (auto& t : f.types)
                if (!t.inst.empty())
                    std::printf("  %-8s %5zu %s  ex. %.2f %.2f %.2f s=%.2f haut=%.2f %.2f %.2f\n", t.layer.c_str(), t.inst.size(), t.model.c_str(), t.inst[0].p[0],
                                t.inst[0].p[1], t.inst[0].p[2], t.inst[0].s, t.inst[0].y[0], t.inst[0].y[1], t.inst[0].y[2]);
        } else if (cmd == "ground") {   // romfs préfixe-de-scène -> couverture du sol et lumières
            Game g(u8path(argv[2]));
            std::string st, sl;
            auto L = groundLoad(g, argv[3], &st);
            std::printf("%s\n", st.c_str());
            for (auto& l : L)
                if (!l.pts.empty()) {
                    auto& p = l.pts[l.pts.size() / 2];
                    std::printf("  %s : %zu cases, ex. %.2f %.2f %.2f n %.2f %.2f %.2f ; %s, %zu sortes\n", l.name.c_str(), l.cells.size(), p.p[0], p.p[1], p.p[2], p.n[0],
                                p.n[1], p.n[2], l.texArray.c_str(), l.kinds.size());
                    std::map<int, size_t> used;
                    for (auto& q : l.pts) used[lower(l.name).find("turf") != std::string::npos ? q.a11 : q.kind]++;
                    for (auto& [k, c] : used) {
                        std::string lay;
                        if (k < int(l.kinds.size())) for (int x : l.kinds[size_t(k)].texLayers) lay += fmt(" %d", x);
                        std::printf("     sorte %d × %zu : %s couches%s teinte %.2f %.2f %.2f taille %.2f/%.2f\n", k, c, k < int(l.kinds.size()) ? l.kinds[size_t(k)].res.c_str() : "?",
                                    lay.c_str(), k < int(l.kinds.size()) ? l.kinds[size_t(k)].color[0] : 0.f, k < int(l.kinds.size()) ? l.kinds[size_t(k)].color[1] : 0.f,
                                    k < int(l.kinds.size()) ? l.kinds[size_t(k)].color[2] : 0.f, k < int(l.kinds.size()) ? l.kinds[size_t(k)].width : 0.f,
                                    k < int(l.kinds.size()) ? l.kinds[size_t(k)].height : 0.f);
                    }
                }
            auto lights = lightsLoad(g, argv[3], &sl);
            std::printf("%s\n", sl.c_str());
            for (size_t i = 0; i < lights.size(); i += std::max<size_t>(1, lights.size() / 6))
                std::printf("  %d %.2f %.2f %.2f col %.2f %.2f %.2f I=%.0f %.2f %.2f\n", int(lights[i].kind), lights[i].p[0], lights[i].p[1], lights[i].p[2], lights[i].col[0],
                            lights[i].col[1], lights[i].col[2], lights[i].intensity, lights[i].a, lights[i].b);
        } else if (cmd == "fx") {   // romfs acteur -> effet lu
            Game g(u8path(argv[2]));
            EffectInfo e = effectLoad(g, argv[3]);
            std::printf("%s %s kind=%s color=%s mask=%s permanent=%d\n", e.name.c_str(), e.file.c_str(), fxKindName(e.kind), e.color.c_str(), e.mask.c_str(), e.ambient ? 1 : 0);
            if (!e.why.empty()) std::printf("pourquoi : %s\n", e.why.c_str());
            for (auto& s : e.esets) std::printf("eset %s\n", s.c_str());
            for (auto& s : e.textures) std::printf("tex %s\n", s.c_str());
            for (auto& s : e.emitters) std::printf("emitter %s\n", s.c_str());
            for (auto& s : e.modules) std::printf("module %s\n", s.c_str());
        } else if (cmd == "mc") {   // .bfres.mc -> BFRES et tampons décodés
            out(meshCodecUnpack(readFile(u8path(argv[2]))), argv[3]);
        } else if (cmd == "bntx") {
            TexData t = bntxExtract(readFile(u8path(argv[2])), std::getenv("TE_MAX") ? std::atoi(std::getenv("TE_MAX")) : 512, argc > 4 ? argv[4] : "", std::getenv("TE_LAYER") ? std::atoi(std::getenv("TE_LAYER")) : 0);
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
            if (std::getenv("TE_MAT")) {
                auto mats = terrainMaterials(g, argv[3]);
                std::printf("%zu matières :", mats.size());
                for (size_t k = 0; k < mats.size() && k < 16; k++) std::printf(" %d:%g", mats[k].layer, mats[k].u);
                std::printf("\n");
            }
            if (std::getenv("TE_MAT") && !tiles.empty() && !tiles[0].ids.empty()) {   // (matières de la première tuile)
                auto& t = tiles[0];
                for (int y = 0; y < t.n; y += t.n / 4)
                    for (int x = 0; x < t.n; x += t.n / 4) {
                        size_t i = size_t(y * t.n + x) * 4;
                        std::printf("(%d,%d) %d %d %d %d / %d %d %d %d\n", x, y, t.ids[i], t.ids[i + 1], t.ids[i + 2], t.ids[i + 3], t.wts[i], t.wts[i + 1], t.wts[i + 2], t.wts[i + 3]);
                    }
            }
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
