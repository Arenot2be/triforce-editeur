#include "core/bake.hpp"

#include "core/byml.hpp"
#include "core/game.hpp"
#include "core/sarc.hpp"
#include "core/util.hpp"

namespace te {

std::string bakePrefix(const std::string& mapPath) {
    std::string n = mapPath.substr(mapPath.find_last_of('/') + 1);
    n = n.substr(0, n.find('.'));
    for (const char* s : {"_Dynamic", "_Static", "_Dyn", "_Sta"})
        if (endsWith(n, s)) { n.resize(n.size() - std::string(s).size()); break; }
    return n;
}

std::map<uint64_t, BakeEntry> bakeLoad(Game& g, const std::string& prefix, std::string* status) {
    std::map<uint64_t, BakeEntry> out;
    int files = 0, bad = 0;
    std::string want = "Bake/Scene/" + prefix + "_G_";
    for (auto& f : g.allFiles()) {
        if (f.rfind(want, 0) != 0 || !endsWith(f, ".bkres.zs")) continue;
        try {
            Sarc s = Sarc::load(g.read(f));
            const Bytes* dat = s.get("bkdat.byaml");
            if (!dat) continue;
            Document doc = bymlLoad(*dat);
            const Value* des = doc.root.get("DataElements");
            if (!des || !des->isArray()) continue;
            files++;
            for (auto& de : *des->a) {
                const Value* type = de.get("DataType");
                const Value* names = de.get("TextureNames");
                const Value* mes = de.get("ModelElements");
                // (type 3 : la carte d'éclairage ; les autres sont l'éclairage indirect en harmoniques sphériques)
                if (!type || type->num() != 3 || !names || !names->isArray() || names->a->empty() || !mes || !mes->isArray()) continue;
                for (auto& me : *mes->a) {
                    const Value* guid = me.get("Guid");
                    if (!guid || guid->t != T::String) continue;
                    std::string gs = guid->s;
                    auto us = gs.find('_');
                    if (us == std::string::npos || gs.substr(us + 1) != "0") continue;
                    uint64_t h = std::strtoull(gs.substr(0, us).c_str(), nullptr, 10);
                    BakeEntry e;
                    const Value* mn = me.get("ModelName");
                    if (mn && mn->t == T::String) e.model = mn->s;
                    const Value* mats = me.get("MaterialElements");
                    if (!mats || !mats->isArray()) continue;
                    int texIdx = 0;
                    for (auto& m : *mats->a) {
                        BakeMat bm;
                        if (auto* v = m.get("MaterialName")) bm.material = v->s;
                        if (auto* v = m.get("OriginalMaterialIndex")) bm.index = int(v->num());
                        if (auto* v = m.get("TextureIndex")) texIdx = int(v->num());
                        auto xy = [&](const char* k, float* d) {
                            if (auto* v = m.get(k)) {
                                if (auto* x = v->get("X")) d[0] = float(x->num());
                                if (auto* y = v->get("Y")) d[1] = float(y->num());
                            }
                        };
                        xy("TexcoordOffset", bm.off);
                        xy("TexcoordScale", bm.scale);
                        e.mats.push_back(bm);
                    }
                    if (texIdx < 0 || size_t(texIdx) >= names->a->size()) texIdx = 0;
                    e.tex = (*names->a)[size_t(texIdx)].s;
                    out[h] = std::move(e);
                }
            }
        } catch (...) {
            bad++;
        }
    }
    if (status)
        *status = files ? fmt("%zu objets éclairés (%d cases de Bake/Scene/%s_G_*)%s", out.size(), files, prefix.c_str(),
                              bad ? fmt(", %d illisibles", bad).c_str() : "")
                        : "aucun éclairage précalculé (Bake/Scene/" + prefix + "_G_*.bkres.zs)";
    return out;
}

}  // namespace te
