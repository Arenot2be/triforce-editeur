#include "core/foliage.hpp"

#include <cmath>
#include <algorithm>
#include <cstring>

#include <memory>
#include "core/byml.hpp"
#include "core/sarc.hpp"
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
            // (normale : y = 1 - (2i + 1) / 256, azimut i × 2π × 0,618… (angle d'or), comme le nuanceur Foliage du jeu)
            float ny = 1.f - (2.f * r[3] + 1.f) / 256.f, h = std::sqrt(std::max(0.f, 1 - ny * ny)), az = r[3] * 3.883221f;
            g.n[0] = std::cos(az) * h;
            g.n[1] = ny;
            g.n[2] = std::sin(az) * h;
            g.a6 = r[6];
            g.a8 = r[8];
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

// ---- sondes de lumière ----
namespace {
uint64_t cellKey(int x, int y, int z) { return (uint64_t(uint32_t(x + 100000)) << 42) ^ (uint64_t(uint32_t(y + 100000)) << 21) ^ uint64_t(uint32_t(z + 100000)); }
float half2f(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? 65504.f : std::ldexp(float(m | 1024), int(e) - 25);
    return s ? -v : v;
}
}  // namespace

ProbeSet probesParse(const std::vector<Bytes>& files, const float sky[3]) {
    ProbeSet ps;
    if (sky) { for (int c = 0; c < 3; c++) ps.sky[c] = sky[c]; ps.absolute = true; }
    for (auto& d : files) {
        if (d.size() < 0x80) continue;
        // (début des sondes : après 4³, 8³, 16³ ou 32³ cellules ; on vérifie que le nombre lu tombe juste)
        size_t start = 0, cells = 0;
        for (size_t hdr : {size_t(0x10), size_t(0x20)})
            for (size_t n : {4, 8, 16, 32}) {
                size_t st = hdr + 48 * n * n * n + 0x24;
                if (st > d.size() || st < 0x1c) continue;
                uint32_t cnt = rd32(d, st - 0x1c);
                if (st + size_t(cnt) * 72 == d.size() && cnt > 0) start = st, cells = n;
            }
        if (!start) continue;
        size_t cnt = (d.size() - start) / 72;
        uint8_t level = cells == 32 ? 0 : cells == 16 ? 1 : cells == 8 ? 2 : 3;
        for (size_t k = 0; k < cnt; k++) {
            size_t o = start + 72 * k;
            float p[3];
            for (int i = 0; i < 3; i++) p[i] = rdf(d, o + 4 * size_t(i));
            if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) continue;
            ps.pos.insert(ps.pos.end(), p, p + 3);
            ps.lvl.push_back(level);
            for (int i = 0; i < 28; i++) ps.sh.push_back(half2f(uint16_t(d[o + 16 + 2 * size_t(i)] | d[o + 17 + 2 * size_t(i)] << 8)));
        }
    }
    ps.index();
    return ps;
}

void ProbeSet::index() {
    grid.clear();
    for (size_t k = 0; k < size(); k++)
        grid[cellKey(int(std::floor(pos[3 * k] / 4)), int(std::floor(pos[3 * k + 1] / 4)), int(std::floor(pos[3 * k + 2] / 4)))].push_back(uint32_t(k));
    // (référence : éclairement médian vers le haut)
    std::vector<float> up;
    for (size_t k = 0; k < size(); k += 7) {
        const float* h = &sh[28 * k];
        float r = 0.886f * (h[0] + h[16] + sky[0] * h[3]) + 1.023f * (h[4] + h[19] + sky[0] * h[7]);
        float g = 0.886f * (h[1] + h[17] + sky[1] * h[3]) + 1.023f * (h[5] + h[20] + sky[1] * h[7]);
        float b = 0.886f * (h[2] + h[18] + sky[2] * h[3]) + 1.023f * (h[6] + h[21] + sky[2] * h[7]);
        up.push_back(0.2126f * r + 0.7152f * g + 0.0722f * b);
    }
    if (!up.empty() && absolute) {
        // (unités absolues : la médiane n'est gardée que pour le bilan)
        std::nth_element(up.begin(), up.begin() + long(up.size() / 2), up.end());
        median = up[up.size() / 2];
        ref = 1;
    } else if (!up.empty()) {
        std::nth_element(up.begin(), up.begin() + long(up.size() / 2), up.end());
        ref = std::max(1e-4f, up[up.size() / 2]);
        median = ref;
    }
}

bool ProbeSet::ambient(const float p[3], float out[12]) const {
    if (grid.empty()) return false;
    int cx = int(std::floor(p[0] / 4)), cy = int(std::floor(p[1] / 4)), cz = int(std::floor(p[2] / 4));
    // (les sondes des cases voisines, pondérées par l'inverse du carré de la distance ; seul compte le niveau le plus
    //  fin présent autour du point, comme le jeu qui ne descend aux niveaux grossiers que là où les fins manquent)
    double accL[4][28] = {}, wL[4] = {}, wsum = 0;
    for (int r = 1; r <= 4 && wsum == 0; r++)
        for (int x = cx - r; x <= cx + r; x++)
            for (int y = cy - r; y <= cy + r; y++)
                for (int z = cz - r; z <= cz + r; z++) {
                    auto it = grid.find(cellKey(x, y, z));
                    if (it == grid.end()) continue;
                    for (uint32_t k : it->second) {
                        double dx = pos[3 * k] - p[0], dy = pos[3 * k + 1] - p[1], dz = pos[3 * k + 2] - p[2];
                        double w = 1.0 / (dx * dx + dy * dy + dz * dz + 1.0);
                        int L = lvl.empty() ? 0 : std::min<int>(3, lvl[k]);
                        wsum += w;
                        wL[L] += w;
                        for (int i = 0; i < 28; i++) accL[L][i] += w * sh[28 * size_t(k) + size_t(i)];
                    }
                }
    if (wsum == 0) return false;
    int best = 0;
    while (best < 3 && wL[best] == 0) best++;
    double acc[28];
    for (int i = 0; i < 28; i++) acc[i] = accL[best][i] / wL[best];
    // (E(n) = 0,886·L0 + 1,023·(L1·n) pour A, B et le ciel × visibilité ; ordre des coefficients : Y00, y, z, x)
    float a[3], M[3][3];
    for (int c = 0; c < 3; c++) {
        double s = sky[c] * scaleSky, ka = scaleA, kb = scaleB;
        a[c] = float(0.886 * (ka * acc[c] + kb * acc[16 + c] + s * acc[3]) / ref);
        double ly = ka * acc[4 + c] + kb * acc[19 + c] + s * acc[7], lz = ka * acc[8 + c] + kb * acc[22 + c] + s * acc[11],
               lx = ka * acc[12 + c] + kb * acc[25 + c] + s * acc[15];
        M[c][0] = float(1.023 * lx / ref); M[c][1] = float(1.023 * ly / ref); M[c][2] = float(1.023 * lz / ref);
    }
    float o[12] = {a[0], a[1], a[2], M[2][0], M[0][0], M[0][1], M[0][2], M[2][1], M[1][0], M[1][1], M[1][2], M[2][2]};
    for (int i = 0; i < 12; i++) out[i] = std::isfinite(o[i]) ? o[i] : 0.f;
    return true;
}

ProbeSet probesLoad(Game& g, const std::string& pre, std::string* status, const float sky[3]) {
    std::vector<Bytes> files;
    std::string dir = "Bake/Probe/Scene/" + pre + "/";
    int n = 0;
    for (auto& f : g.allFiles()) {
        if (f.rfind(dir, 0) != 0) continue;
        // (tous les niveaux : le plus fin, tous les 2 m, prime là où il existe)
        if (f.find(".bkprb") == std::string::npos && f.find("resident.bkprcb") == std::string::npos) continue;
        try { files.push_back(g.read(f)); n++; } catch (...) {}
    }
    if (files.empty()) {
        if (status) *status = "sondes : pas de " + dir;
        return {};
    }
    ProbeSet ps = probesParse(files, sky);
    if (status) *status = ps.absolute ? fmt("sondes : %zu (%d fichiers), ciel du jeu, éclairement médian vers le haut %.3f (soleil = 1)", ps.size(), n, double(ps.median))
                                      : fmt("sondes : %zu (%d fichiers), ciel supposé", ps.size(), n);
    return ps;
}

// ---- ciel ----
SkyEnv skyParse(const Bytes& b) {
    SkyEnv e;
    const size_t trans = 0, irrOff = 0x120000;
    if (b.size() < irrOff + 512 * 8 * 8) return e;
    auto texel = [&](size_t off, size_t w, size_t x, size_t y, int c) {
        size_t o = off + 8 * (y * w + x) + 2 * size_t(c);
        return half2f(uint16_t(b[o] | b[o + 1] << 8));
    };
    // (transmittance : x = altitude (0 = sol), y = direction, la dernière ligne vers le zénith)
    for (int c = 0; c < 3; c++) e.zenithT[c] = std::clamp(texel(trans, 512, 0, 31, c), 0.05f, 1.f);
    // (éclairement du ciel : x = altitude, y = hauteur du soleil mu = −0,2 + 1,2·y/7 ; ligne 0 = soleil couché)
    e.irr.resize(8 * 3);
    for (size_t y = 0; y < 8; y++)
        for (int c = 0; c < 3; c++) e.irr[y * 3 + size_t(c)] = std::max(0.f, texel(irrOff, 512, 0, y, c));
    e.ok = e.irr[7 * 3 + 2] > 0;
    return e;
}

void SkyEnv::at(float mu, float sun[3], float sky[3]) const {
    mu = std::clamp(mu, -0.2f, 1.f);
    // (soleil : transmittance du zénith élevée à la masse d'air (Kasten et Young) ; les lignes de la table la suivent
    //  à quelques pour cent près)
    float zdeg = std::acos(std::clamp(mu, 0.f, 1.f)) * 57.29578f;
    float am = mu > 0.f ? 1.f / (mu + 0.50572f * std::pow(96.07995f - zdeg, -1.6364f)) : 38.f;
    for (int c = 0; c < 3; c++) sun[c] = mu > -0.02f ? std::pow(zenithT[c], am) : 0.f;
    float y = (mu + 0.2f) / 1.2f * 7.f;
    int y0 = std::clamp(int(y), 0, 6);
    float t = std::clamp(y - float(y0), 0.f, 1.f);
    for (int c = 0; c < 3; c++) {
        float E = irr.empty() ? 0.f : irr[size_t(y0) * 3 + size_t(c)] * (1 - t) + irr[size_t(y0 + 1) * 3 + size_t(c)] * t;
        sky[c] = E / 3.14159265f;   // (ciel uniforme de même éclairement : L = E / π)
    }
}

SkyEnv skyLoad(Game& g, std::string* status) {
    const std::string path = "Env/GameScene.Nin_Ounce_NVN2.genvb.zs";
    try {
        if (!g.exists(path)) { if (status) *status = "ciel : pas de " + path; return {}; }
        Sarc s = Sarc::load(g.read(path));
        const Bytes* sb = s.get("res/master_field.skybin");
        if (!sb) sb = s.get("res/Master_Field.skybin");
        if (!sb) { if (status) *status = "ciel : pas de master_field.skybin dans " + path; return {}; }
        SkyEnv e = skyParse(*sb);
        if (status) *status = e.ok ? "ciel : tables d'atmosphère lues (master_field.skybin)" : "ciel : tables illisibles";
        return e;
    } catch (const std::exception& ex) {
        if (status) *status = std::string("ciel : ") + ex.what();
        return {};
    }
}

// ---- palette d'environnement ----
void kelvinToRgb(float T, float out[3]) {
    double t = std::clamp(double(T), 1667.0, 25000.0), x, y;
    if (t <= 4000) x = -0.2661239e9 / (t * t * t) - 0.2343589e6 / (t * t) + 0.8776956e3 / t + 0.179910;
    else x = -3.0258469e9 / (t * t * t) + 2.1070379e6 / (t * t) + 0.2226347e3 / t + 0.240390;
    if (t <= 2222) y = -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683;
    else if (t <= 4000) y = -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867;
    else y = 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
    double X = x / y, Y = 1, Z = (1 - x - y) / y;
    out[0] = float(std::max(0.0, 3.2406 * X - 1.5372 * Y - 0.4986 * Z));
    out[1] = float(std::max(0.0, -0.9689 * X + 1.8758 * Y + 0.0415 * Z));
    out[2] = float(std::max(0.0, 0.0557 * X - 0.2040 * Y + 1.0570 * Z));
}

float EnvPalette::exposure() const { return unit() / (3.14159265f * 1.2f * std::pow(2.f, ev)); }

namespace {
// (valeurs d'une palette, toutes mélangeables)
struct PalVals {
    float sunColor[3] = {1, 1, 1}, sunLux = 1e4f, skyLux = 1e4f, ev = 10, bounce = 1, skyBounce = 1, ambient = 1;
    bool fogOn = false;
    FogLayer fog[2];
    float fogMain = 1, fogSky = 1, fogBake = 1;
};
double vnum(const Value* v, double d) { return v && v->isNum() ? v->num() : d; }
bool vflag(const Value* v) { return v && v->t == T::Bool && v->b; }
void vcol(const Value* c, float out[3], bool alpha) {
    if (!c || !c->isMap()) return;
    float a = alpha ? float(vnum(c->get("A"), 1)) : 1.f;
    out[0] = float(vnum(c->get("R"), out[0])) * a; out[1] = float(vnum(c->get("G"), out[1])) * a; out[2] = float(vnum(c->get("B"), out[2])) * a;
}
PalVals palRead(const Value& q) {
    PalVals v;
    v.sunLux = float(vnum(q.get("BgDifIntensity"), 1e4));
    v.ev = float(vnum(q.get("ExposureValue"), 10));
    v.skyLux = float(vnum(q.get("SkySunColorIntensity"), v.sunLux));
    v.bounce = float(vnum(q.get("DirectionalLightBounceScale"), 1));
    v.skyBounce = float(vnum(q.get("SkyLightBounceScale"), 1));
    v.ambient = float(vnum(q.get("AmbientIntensity"), 1));
    float col[3] = {1, 1, 1}, tc[3] = {1, 1, 1}, wb[3];
    vcol(q.get("BgDifColor"), col, false);
    if (vflag(q.get("BgDifUseTemperature"))) kelvinToRgb(float(vnum(q.get("BgDifTemperature"), 6500)), tc);
    kelvinToRgb(float(vnum(q.get("WhiteBalanceTemperature"), 6500)), wb);
    for (int c = 0; c < 3; c++) v.sunColor[c] = col[c] * tc[c] / std::max(1e-3f, wb[c]);
    float lum = 0.2126f * v.sunColor[0] + 0.7152f * v.sunColor[1] + 0.0722f * v.sunColor[2];
    for (float& x : v.sunColor) x /= std::max(1e-3f, lum);
    v.fogOn = !q.get("VolumeFogIsEnabled") || vflag(q.get("VolumeFogIsEnabled"));
    v.fogMain = float(vnum(q.get("VolumeFogMainLightScale"), 1));
    v.fogSky = float(vnum(q.get("VolumeFogProbeSkyLightScale"), 1));
    v.fogBake = float(vnum(q.get("VolumeFogProbeBakeLightScale"), 1));
    for (int k = 0; k < 2; k++) {
        FogLayer& f = v.fog[k];
        const Value* en = q.get(k == 0 ? "VolumeFogEnableFog1" : "VolumeFogEnableFog2");
        const Value* p = q.get(k == 0 ? "VolumeFogParams1" : "VolumeFogParams2");
        f.on = p && p->isMap() && (!en || vflag(en));
        if (!p || !p->isMap()) continue;
        vcol(p->get("AbsorptionColor"), f.absorb, true);
        vcol(p->get("EmissionColor"), f.emit, true);
        vcol(p->get("MieColor"), f.mie, true);
        f.absorbD = float(vnum(p->get("AbsorptionDensity"), 0));
        f.emitD = float(vnum(p->get("EmissionDensity"), 0));
        f.mieD = float(vnum(p->get("MieDensity"), 0));
        f.rayD = float(vnum(p->get("RayleighDensity"), 0));
        f.nearD = float(vnum(p->get("NearDensity"), 0));
        f.nfs = float(vnum(p->get("NearFadeStart"), 0));
        f.nfe = float(vnum(p->get("NearFadeEnd"), 0));
        f.farD = float(vnum(p->get("FarDensity"), 0));
        f.ffs = float(vnum(p->get("FarFadeStart"), 0));
        f.ffe = float(vnum(p->get("FarFadeEnd"), 0));
        f.falloff = float(vnum(p->get("EHFFalloff"), 0));
        f.yoff = float(vnum(p->get("EHFYOffset"), 0));
        f.aniso = float(vnum(p->get("MieAnisotropy"), 0.2));
        f.noise = float(vnum(p->get("NoiseStrength"), 0));
        f.heightMap = vflag(p->get("UseHeightMap"));
    }
    return v;
}
float lerpf(float a, float b, float t) { return a + (b - a) * t; }
void fogLerp(FogLayer& o, const FogLayer& a, const FogLayer& b, float t) {
    // (couche absente d'un côté : sa densité va vers 0)
    FogLayer A = a, B = b;
    if (!A.on) { A = B; A.nearD = A.farD = 0; }
    if (!B.on) { B = A; B.nearD = B.farD = 0; }
    o.on = a.on || b.on;
    o.heightMap = t < 0.5f ? A.heightMap : B.heightMap;
    for (int c = 0; c < 3; c++) { o.absorb[c] = lerpf(A.absorb[c], B.absorb[c], t); o.emit[c] = lerpf(A.emit[c], B.emit[c], t); o.mie[c] = lerpf(A.mie[c], B.mie[c], t); }
    o.absorbD = lerpf(A.absorbD, B.absorbD, t); o.emitD = lerpf(A.emitD, B.emitD, t); o.mieD = lerpf(A.mieD, B.mieD, t); o.rayD = lerpf(A.rayD, B.rayD, t);
    o.nearD = lerpf(A.nearD, B.nearD, t); o.nfs = lerpf(A.nfs, B.nfs, t); o.nfe = lerpf(A.nfe, B.nfe, t);
    o.farD = lerpf(A.farD, B.farD, t); o.ffs = lerpf(A.ffs, B.ffs, t); o.ffe = lerpf(A.ffe, B.ffe, t);
    o.falloff = lerpf(A.falloff, B.falloff, t); o.yoff = lerpf(A.yoff, B.yoff, t); o.aniso = lerpf(A.aniso, B.aniso, t); o.noise = lerpf(A.noise, B.noise, t);
}
}  // namespace

EnvPalette envLoad(Game& g, const std::string& pre, int minutes, std::string* status) {
    EnvPalette e;
    const std::string path = "Pack/EnvPalette.pack.zs";
    minutes = ((minutes % 1440) + 1440) % 1440;
    try {
        if (!g.exists(path)) { if (status) *status = "palette : pas de " + path; return e; }
        Sarc s = Sarc::load(g.read(path));
        auto doc = [&](const std::string& dir, const std::string& name, const char* type) -> std::unique_ptr<Document> {
            const Bytes* b = s.get("WorldMgr/" + dir + "/" + name + ".game__wm__" + type + ".bgyml");
            if (!b) return nullptr;
            return std::make_unique<Document>(bymlLoad(*b));
        };
        // (table horaire de la scène : son nom, puis sans les suffixes « _xxx » un à un ; sinon le champ par beau temps)
        std::unique_ptr<Document> tt;
        std::string n = pre;
        while (!n.empty() && !(tt = doc("ResEnvPaletteTimeTable", n, "ResEnvPaletteTimeTable"))) {
            size_t k = n.find_last_of('_');
            if (k == std::string::npos) n.clear(); else n.resize(k);
        }
        if (!tt) { n = "MainField_Bluesky"; tt = doc("ResEnvPaletteTimeTable", n, "ResEnvPaletteTimeTable"); }
        if (!tt) { if (status) *status = "palette : pas de table horaire"; return e; }
        e.table = n;
        const Value& r = tt->root;
        e.forcedSun = vflag(r.get("IsForceMainLight"));
        e.sunEl = float(vnum(r.get("MainLight_Elevation"), 0));
        e.sunAz = float(vnum(r.get("MainLight_Azimuth"), 0));
        // (entrées triées par heure ; celle de l'heure voulue est la dernière commencée — sinon la dernière de la
        //  veille — et la précédente est celle d'avant, en tournant)
        struct Ent { int t; std::string pal; float blendMin; bool curve; float d[4]; };
        std::vector<Ent> ents;
        if (const Value* arr = r.get("TimeTableArray"); arr && arr->isArray())
            for (auto& it : *arr->a) {
                const Value* p = it.get("EnvPalette");
                if (!p || p->t != T::String) continue;
                Ent en{int(vnum(it.get("Hour"), 0)) * 60 + int(vnum(it.get("Min"), 0)), p->s, float(vnum(it.get("BlendTime"), 0)),
                       vflag(it.get("IsBlendCurve")), {0, 0, 1, 0}};
                if (const Value* c = it.get("Curve"); c && c->isMap())
                    if (const Value* d = c->get("Data"); d && d->isArray() && d->a->size() >= 4)
                        for (int k = 0; k < 4; k++) en.d[k] = float((*d->a)[size_t(k)].num());
                size_t a = en.pal.find_last_of('/'), b = en.pal.find(".game__");
                if (a != std::string::npos && b != std::string::npos && b > a) en.pal = en.pal.substr(a + 1, b - a - 1);
                ents.push_back(en);
            }
        if (ents.empty()) { if (status) *status = "palette : table horaire vide (" + n + ")"; return e; }
        std::sort(ents.begin(), ents.end(), [](const Ent& a, const Ent& b) { return a.t < b.t; });
        int cur = int(ents.size()) - 1;
        for (int i = 0; i < int(ents.size()); i++) if (ents[size_t(i)].t <= minutes) cur = i;
        int prev = (cur + int(ents.size()) - 1) % int(ents.size());
        const Ent& E = ents[size_t(cur)];
        int since = (minutes - E.t + 1440) % 1440;
        float t = E.blendMin > 0 ? std::clamp(float(since) / E.blendMin, 0.f, 1.f) : 1.f;
        if (E.curve) {
            // (courbe d'Hermite : points 0 → 1, tangentes Data[1] et Data[3])
            float t2 = t * t, t3 = t2 * t;
            t = std::clamp((2 * t3 - 3 * t2 + 1) * E.d[0] + (t3 - 2 * t2 + t) * E.d[1] + (-2 * t3 + 3 * t2) * E.d[2] + (t3 - t2) * E.d[3], 0.f, 1.f);
        }
        auto pd = doc("ResEnvPalette", E.pal, "ResEnvPalette");
        if (!pd) { if (status) *status = "palette : " + E.pal + " introuvable"; return e; }
        PalVals A = palRead(pd->root), B = A;
        e.palette = E.pal;
        if (t < 1.f && prev != cur)
            if (auto pp = doc("ResEnvPalette", ents[size_t(prev)].pal, "ResEnvPalette")) { B = palRead(pp->root); e.palette2 = ents[size_t(prev)].pal; }
        if (e.palette2.empty()) t = 1.f;
        e.blend = t;
        // (B → A : éclairements et exposition en échelle logarithmique, le reste linéairement)
        auto lg = [&](float b, float a) { return std::exp(lerpf(std::log(std::max(b, 1e-3f)), std::log(std::max(a, 1e-3f)), t)); };
        for (int c = 0; c < 3; c++) e.sunColor[c] = lerpf(B.sunColor[c] * B.sunLux, A.sunColor[c] * A.sunLux, t);
        e.sunLux = lg(B.sunLux, A.sunLux);
        for (int c = 0; c < 3; c++) e.sunColor[c] /= std::max(1e-3f, lerpf(B.sunLux, A.sunLux, t));
        e.skyLux = lg(B.skyLux, A.skyLux);
        e.ev = lerpf(B.ev, A.ev, t);
        e.bounceScale = lerpf(B.bounce, A.bounce, t);
        e.skyBounceScale = lerpf(B.skyBounce, A.skyBounce, t);
        e.ambientIntensity = lerpf(B.ambient, A.ambient, t);
        e.fogOn = A.fogOn || B.fogOn;
        for (int k = 0; k < 2; k++) {
            FogLayer fa = A.fog[k], fb = B.fog[k];
            if (!A.fogOn) fa.on = false;
            if (!B.fogOn) fb.on = false;
            fogLerp(e.fog[k], fb, fa, t);
        }
        e.fogMainLight = lerpf(B.fogMain, A.fogMain, t);
        e.fogSkyLight = lerpf(B.fogSky, A.fogSky, t);
        e.fogBakeLight = lerpf(B.fogBake, A.fogBake, t);
        if (!e.forcedSun) {
            // (soleil non fixé : course simple, levé à 6 h à l'est, 60° à midi, couché à 18 h ; la nuit, la lune en face)
            float h = float(minutes) / 60.f, ph = (h - 6.f) / 12.f * 3.14159265f;
            float el = 60.f * std::sin(ph), az = 90.f + 180.f * (h - 6.f) / 12.f;
            if (el < 3.f) { el = std::max(8.f, 45.f * std::fabs(std::sin(ph))); az += 180.f; }
            e.sunEl = el;
            e.sunAz = std::fmod(az + 720.f, 360.f);
        }
        e.ok = true;
        if (status) {
            *status = fmt("palette : %s → %s", e.table.c_str(), e.palette.c_str());
            if (!e.palette2.empty()) *status += fmt(" (fondu depuis %s, %.0f %%)", e.palette2.c_str(), double(t * 100));
            *status += fmt(" à %02d:%02d ; soleil %s%.1f° / %.0f°, %.0f lux (ciel %.0f), EV %.1f", minutes / 60, minutes % 60, e.forcedSun ? "fixé " : "",
                           double(e.sunEl), double(e.sunAz), double(e.sunLux), double(e.skyLux), double(e.ev));
        }
        return e;
    } catch (const std::exception& ex) {
        if (status) *status = std::string("palette : ") + ex.what();
        return e;
    }
}

}  // namespace te
