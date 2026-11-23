#include "core/foliage.hpp"

#include <cmath>
#include <algorithm>
#include <cstring>

#include "core/byml.hpp"
#include "core/util.hpp"

namespace te {

FoliageSet foliageParse(const Bytes& d, const std::string& layer) {
    FoliageSet out;
    if (d.size() < 0x20) throw Error("fichier trop court");
    uint32_t nt = rd32(d, 0), ni = rd32(d, 4), ver = rd32(d, 8);
    size_t base = 0x20 + size_t(nt) * 0x100;
    if (ver != 2 || nt == 0 || nt > 4096 || base + size_t(ni) * 64 > d.size()) throw Error("pas une liste d'instances (couverture du sol ?)");
    for (uint32_t i = 0; i < nt; i++) {
        size_t o = 0x20 + size_t(i) * 0x100;
        size_t L = d[o];
        if (L == 0 || L > 0xff) throw Error("fiche de sorte illisible");
        FoliageType t;
        t.res = std::string(reinterpret_cast<const char*>(d.data() + o + 1), L);
        std::string stem = t.res.substr(t.res.rfind('/') == std::string::npos ? 0 : t.res.rfind('/') + 1);
        t.model = stem.substr(0, stem.find('.'));
        t.layer = layer;
        out.types.push_back(std::move(t));
    }
    for (uint32_t i = 0; i < ni; i++) {
        size_t o = base + size_t(i) * 64;
        FoliageInst f;
        std::memcpy(f.p, d.data() + o, 12);
        std::memcpy(&f.s, d.data() + o + 12, 4);
        std::memcpy(f.x, d.data() + o + 16, 12);
        std::memcpy(f.y, d.data() + o + 32, 12);
        uint16_t t = uint16_t(rd16(d, o + 48));
        if (t >= nt || !std::isfinite(f.p[0]) || !std::isfinite(f.s)) continue;
        out.types[t].inst.push_back(f);
        out.count++;
    }
    return out;
}

FoliageSet foliageLoad(Game& g, const std::string& pre, std::string* status) {
    FoliageSet all;
    int files = 0;
    std::string bad;
    for (auto& f : g.allFiles()) {
        if (f.rfind("Foliage/Placement/Scene/", 0) != 0 || !endsWith(f, ".flnt.zs")) continue;
        std::string base = f.substr(f.rfind('/') + 1);
        if (base.rfind(pre + ".", 0) != 0) continue;   // (« KokiriForest_Child.… » ; pas « KokiriForest_Adult.… »)
        std::string layer = base.substr(0, base.size() - 8);
        layer = layer.substr(layer.rfind('.') + 1);
        try {
            FoliageSet s = foliageParse(g.read(f), layer);
            for (auto& t : s.types) all.types.push_back(std::move(t));
            all.count += s.count;
            files++;
        } catch (const std::exception&) {
            all.groundCover.push_back(layer);
        }
    }
    if (status) {
        *status = fmt("feuillage : %zu plantes de %zu sortes (%d fichier(s))", all.count, all.types.size(), files);
        if (!all.groundCover.empty()) {
            std::string l;
            for (auto& c : all.groundCover) l += (l.empty() ? "" : ", ") + c;
            *status += " ; couverture du sol pas encore lue : " + l;
        }
    }
    return all;
}

GroundLayer groundParse(const Bytes& d, const std::string& name) {
    GroundLayer L;
    L.name = name;
    if (d.size() < 0x24 || rd32(d, 0) != 4) throw Error("pas une couverture du sol");
    uint32_t n = rd32(d, 4);
    size_t offs = 0x20 + size_t(n) * 4, pts = offs + (size_t(n) + 1) * 4;
    if (n == 0 || pts > d.size()) throw Error("couverture du sol : en-tête incohérent");
    uint32_t total = rd32(d, offs + size_t(n) * 4);
    if (pts + size_t(total) * 12 > d.size()) throw Error("couverture du sol : points tronqués");
    L.pts.reserve(total);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* c = d.data() + 0x20 + size_t(i) * 4;
        uint32_t b = rd32(d, offs + size_t(i) * 4), e = rd32(d, offs + size_t(i) * 4 + 4);
        if (b > e || e > total) throw Error("couverture du sol : case incohérente");
        GroundCell cell{{(c[0] - 128) * 4.f + 2, (c[1] - 127) * 4.f + 2, (c[2] - 128) * 4.f + 2}, uint32_t(L.pts.size()), 0};
        for (uint32_t k = b; k < e; k++) {
            const uint8_t* r = d.data() + pts + size_t(k) * 12;
            GroundPt g;
            g.p[0] = (c[0] - 128) * 4.f + r[0] / 64.f;
            g.p[1] = (c[1] - 127) * 4.f + r[1] / 64.f;
            g.p[2] = (c[2] - 128) * 4.f + r[2] / 64.f;
            float ny = r[7] / 255.f, h = std::sqrt(std::max(0.f, 1 - ny * ny)), az = r[6] / 256.f * 6.2831853f;
            g.n[0] = std::cos(az) * h;
            g.n[1] = ny;
            g.n[2] = std::sin(az) * h;
            g.a3 = r[3];
            g.a4 = r[4];
            g.kind = r[5];
            g.a11 = r[11];
            L.pts.push_back(g);
        }
        cell.end = uint32_t(L.pts.size());
        L.cells.push_back(cell);
    }
    return L;
}

// ---- réglages de la couverture du sol (Pack/Foliage.pack)
namespace {
struct FolPack {
    std::shared_ptr<const Sarc> p;
    // « Work/Foliage/ResX/Y.game__foliage__ResX.gyml » -> membre « Foliage/ResX/Y.game__foliage__ResX.bgyml »
    const Value* get(const std::string& work, std::vector<Document>& keep) {
        if (!p) return nullptr;
        std::string m = work.rfind("Work/", 0) == 0 ? work.substr(5) : work;
        if (endsWith(m, ".gyml")) m = m.substr(0, m.size() - 5) + ".bgyml";
        const Bytes* b = p->get(m);
        if (!b) return nullptr;
        try { keep.push_back(bymlLoad(*b)); } catch (...) { return nullptr; }
        return keep.back().root.isMap() ? &keep.back().root : nullptr;
    }
    std::vector<std::string> list(const std::string& work, const char* key, std::vector<Document>& keep) {
        std::vector<std::string> v;
        if (auto r = get(work, keep))
            if (auto a = r->get(key); a && a->isArray())
                for (auto& x : *a->a) if (x.t == T::String) v.push_back(x.s);
        return v;
    }
};
float num(const Value* m, const char* k, float d) {
    if (!m || !m->isMap()) return d;
    auto v = m->get(k);
    if (!v) return d;
    if (v->t == T::F32 || v->t == T::F64) return float(v->f);
    if (v->t == T::I32 || v->t == T::U32 || v->t == T::I64) return float(v->i);
    return d;
}
int indexIn(const std::vector<std::string>& arr, const std::string& tex) {
    for (size_t i = 0; i < arr.size(); i++) if (arr[i] == tex) return int(i);
    return -1;
}
void fillKinds(FolPack& P, GroundLayer& L, std::vector<Document>& keep) {
    std::string ln = lower(L.name);
    auto stem = [](const std::string& w) { std::string b = w.substr(w.rfind('/') + 1); return b.substr(0, b.find('.')); };
    if (ln.find("moss") != std::string::npos) {
        L.texArray = "Foliage_Moss_Alb";
        auto arr = P.list("Work/Foliage/ResTexArray/Moss.game__foliage__ResTexArray.gyml", "TexList", keep);
        for (auto& m : P.list("Work/Foliage/ResMossList/Default.game__foliage__ResMossList.gyml", "MossList", keep)) {
            GroundKind k;
            k.res = stem(m);
            for (auto& t : P.list(m, "MossTexNameList", keep)) if (int i = indexIn(arr, t); i >= 0) k.texLayers.push_back(i);
            L.kinds.push_back(k);
        }
    } else if (ln.find("stone") != std::string::npos) {
        L.texArray = "Foliage_Stone_Alb";
        auto arr = P.list("Work/Foliage/ResTexArray/Stone.game__foliage__ResTexArray.gyml", "TexList", keep);
        for (auto& m : P.list("Work/Foliage/ResStoneList/Default.game__foliage__ResStoneList.gyml", "StoneList", keep)) {
            GroundKind k;
            k.res = stem(m);
            if (auto r = P.get(m, keep)) {
                if (auto t = r->get("StoneTexName"); t && t->t == T::String)
                    if (int i = indexIn(arr, t->s); i >= 0) k.texLayers.push_back(i);
                float c = num(r, "ColorScale", 1);
                k.color[0] = k.color[1] = k.color[2] = c;
                k.width = num(r, "WidthRate", 0.5f);
                k.height = num(r, "HeightRate", 0.25f);
            }
            L.kinds.push_back(k);
        }
    } else if (ln.find("turf") != std::string::npos) {
        L.texArray = "Foliage_Turf_Alb";
        auto arr = P.list("Work/Foliage/ResTexArray/Turf.game__foliage__ResTexArray.gyml", "TexList", keep);
        for (auto& m : P.list("Work/Foliage/ResTurfList/Default.game__foliage__ResTurfList.gyml", "TurfList", keep)) {
            GroundKind k;
            k.res = stem(m);
            if (auto r = P.get(m, keep))
                if (auto tl = r->get("TexList"); tl && tl->isArray() && !tl->a->empty()) {
                    // (première variante : sa teinte ; les autres ne changent que la teinte et le poids)
                    const Value& e = (*tl->a)[0];
                    if (auto t = e.get("Tex"); t && t->t == T::String)
                        if (int i = indexIn(arr, t->s); i >= 0) k.texLayers.push_back(i);
                    if (auto c = e.get("ColorScale"))
                        { k.color[0] = num(c, "R", 1); k.color[1] = num(c, "G", 1); k.color[2] = num(c, "B", 1); }
                }
            L.kinds.push_back(k);
        }
    } else {
        L.texArray = "Foliage_ShootLeaf_Alb";
        auto arr = P.list("Work/Foliage/ResTexArray/ShootLeaf.game__foliage__ResTexArray.gyml", "TexList", keep);
        std::string list = ln.find("8") != std::string::npos ? "Default8Leaf" : "Default4Leaf";
        for (auto& m : P.list("Work/Foliage/ResShootLeafUndergrowthList/" + list + ".game__foliage__ResShootLeafUndergrowthList.gyml", "ShootLeafList", keep)) {
            GroundKind k;
            k.res = stem(m);
            if (auto r = P.get(m, keep)) {
                k.leaves = int(num(r, "NumLeaf", 4));
                float lo = num(r, "ScaleMin", 0.6f), hi = num(r, "ScaleMax", 1.f);
                k.height = 0.5f * (lo + hi);
                if (auto sl = r->get("ShootLeaf"); sl && sl->t == T::String)
                    if (auto leaf = P.get(sl->s, keep)) {
                        k.width = num(leaf, "LeafSizeScale", 1) * 0.5f + 0.5f;
                        if (auto t = leaf->get("LeafTexName"); t && t->t == T::String)
                            if (int i = indexIn(arr, t->s); i >= 0) k.texLayers.push_back(i);
                    }
            }
            L.kinds.push_back(k);
        }
    }
}
}  // namespace

std::vector<GroundLayer> groundLoad(Game& g, const std::string& pre, std::string* status) {
    std::vector<GroundLayer> out;
    size_t total = 0;
    for (auto& f : g.allFiles()) {
        if (f.rfind("Foliage/Placement/Scene/", 0) != 0 || !endsWith(f, ".flnt.zs")) continue;
        std::string base = f.substr(f.rfind('/') + 1);
        if (base.rfind(pre + ".", 0) != 0) continue;
        std::string layer = base.substr(0, base.size() - 8);
        layer = layer.substr(layer.rfind('.') + 1);
        try {
            out.push_back(groundParse(g.read(f), layer));
            total += out.back().pts.size();
        } catch (const std::exception&) {}
    }
    FolPack P;
    try { if (g.exists("Pack/Foliage.pack.zs")) P.p = g.pack("Pack/Foliage.pack.zs"); } catch (...) {}
    std::vector<Document> keep;
    for (auto& L : out) try { fillKinds(P, L, keep); } catch (...) {}
    if (status) {
        std::string l;
        for (auto& L : out) l += (l.empty() ? "" : ", ") + L.name + fmt(" %zu", L.pts.size());
        *status = out.empty() ? "couverture du sol : aucune" : fmt("couverture du sol : %zu points (", total) + l + ")";
        if (!P.p) *status += " ; Pack/Foliage.pack absent : textures et teintes inconnues";
    }
    return out;
}

std::vector<SceneLight> lightsParse(const Bytes& d) {
    if (d.size() < 0x14 || std::memcmp(d.data(), "BKLT", 4) != 0) throw Error("pas un fichier de lumières BKLT");
    uint32_t np = rd32(d, 8), ns = rd32(d, 12), nr = rd32(d, 16);
    size_t o = 0x14;
    if (o + size_t(np) * 28 + size_t(ns) * 52 + size_t(nr) * 60 > d.size()) throw Error("fichier de lumières tronqué");
    auto f = [&](size_t at) { float v; std::memcpy(&v, d.data() + at, 4); return v; };
    std::vector<SceneLight> out;
    auto colour = [](SceneLight& L, float r, float g, float b) {
        float m = std::max({r, g, b, 1e-6f});
        L.col[0] = r / m; L.col[1] = g / m; L.col[2] = b / m;
        L.intensity = m;
    };
    for (uint32_t i = 0; i < np; i++, o += 28) {
        SceneLight L;
        L.kind = SceneLight::Point;
        for (int k = 0; k < 3; k++) L.p[k] = f(o + 4 * k);
        colour(L, f(o + 12), f(o + 16), f(o + 20));
        L.a = f(o + 24);
        out.push_back(L);
    }
    for (uint32_t i = 0; i < ns; i++, o += 52) {
        SceneLight L;
        L.kind = SceneLight::Spot;
        for (int k = 0; k < 3; k++) { L.p[k] = f(o + 4 * k); L.dir[k] = f(o + 12 + 4 * k); }
        colour(L, f(o + 24), f(o + 28), f(o + 32));
        L.a = f(o + 36);
        L.b = f(o + 40);
        out.push_back(L);
    }
    for (uint32_t i = 0; i < nr; i++, o += 60) {
        SceneLight L;
        L.kind = SceneLight::Rect;
        for (int k = 0; k < 3; k++) { L.p[k] = f(o + 4 * k); L.dir[k] = f(o + 12 + 4 * k); L.up[k] = f(o + 24 + 4 * k); }
        colour(L, f(o + 36), f(o + 40), f(o + 44));
        L.a = f(o + 48);
        L.b = f(o + 52);
        out.push_back(L);
    }
    return out;
}

std::vector<SceneLight> lightsLoad(Game& g, const std::string& pre, std::string* status) {
    std::string rel = "Bake/Light/" + pre + ".bklight.zs";
    if (!g.exists(rel)) {
        if (status) *status = "lumières de la scène : pas de " + rel;
        return {};
    }
    auto v = lightsParse(g.read(rel));
    int n[3] = {0, 0, 0};
    for (auto& L : v) n[L.kind]++;
    if (status) *status = fmt("lumières de la scène : %d ponctuelles, %d projecteurs, %d rectangulaires", n[0], n[1], n[2]);
    return v;
}

}  // namespace te
