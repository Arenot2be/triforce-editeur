#include "core/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <tuple>

#include "core/bntx.hpp"

namespace te {

namespace {
const int NSRC = 257;

struct Area { double x, z, size; };
struct Scene { double world = 500, hscale = 800; std::map<std::string, Area> areas; };

bool isHexU(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'); }
bool isHexAny(char c) { return isHexU(c) || (c >= 'a' && c <= 'f'); }

Scene parseTscb(const Bytes& b) {
    if (!has(b, 0, "TSCB")) throw Error("pas un TSCB");
    Scene sc;
    float world = rdf(b, 0x10), hs = rdf(b, 0x14);
    sc.world = world > 0 ? world : 500.0;
    sc.hscale = hs > 0 ? hs : 800.0;
    std::map<size_t, std::string> names;
    for (size_t i = 0; i + 11 <= b.size(); i++) {
        if (i > 0 && isHexAny(char(b[i - 1]))) continue;
        bool ok = true;
        for (int k = 0; k < 10 && ok; k++) ok = isHexU(char(b[i + size_t(k)]));
        if (ok && b[i + 10] == 0) names[i] = std::string(b.begin() + long(i), b.begin() + long(i + 10));
    }
    for (size_t o = 0x60; o + 4 <= b.size(); o += 4) {
        uint32_t v = rd32(b, o);
        if (!(v > 0 && v < b.size())) continue;
        auto it = names.find(o + v);
        if (it == names.end() || sc.areas.count(it->second)) continue;
        float x = rdf(b, o - 0x60), z = rdf(b, o - 0x5C), s = rdf(b, o - 0x58);
        if (s > 0 && std::isfinite(x) && std::isfinite(z) && std::isfinite(s) && std::fabs(x) < 1e5 && std::fabs(z) < 1e5 && s < 1e5)
            sc.areas[it->second] = {x, z, s};
    }
    if (sc.areas.empty()) throw Error("aucune tuile trouvée");
    return sc;
}

int lodOf(const std::string& n) { char c = n[1]; return c <= '9' ? c - '0' : c - 'A' + 10; }

struct Piece { std::string name; int i0, j0, span; };

std::vector<Piece> select(const Scene& sc, const std::function<bool(const std::string&)>& hasHeight, int budget) {
    std::map<std::tuple<int, long long, long long>, std::string> byKey;
    for (auto& [nm, a] : sc.areas) byKey[{lodOf(nm), std::llround(a.x * 4096), std::llround(a.z * 4096)}] = nm;
    auto kid = [&](const std::string& nm, int qx, int qz) -> std::string {
        auto& a = sc.areas.at(nm);
        auto it = byKey.find({lodOf(nm) + 1, std::llround((a.x + (qx - .5) * a.size / 2) * 4096), std::llround((a.z + (qz - .5) * a.size / 2) * 4096)});
        return it == byKey.end() ? "" : it->second;
    };
    std::function<bool(const std::string&, int)> usable = [&](const std::string& nm, int depth) -> bool {
        if (hasHeight(nm)) return true;
        if (depth >= 3) return false;
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++) { auto k = kid(nm, a, b); if (!k.empty() && usable(k, depth + 1)) return true; }
        return false;
    };
    int top = 99;
    for (auto& [nm, a] : sc.areas) top = std::min(top, lodOf(nm));
    std::vector<Piece> level, fin, finalP;
    for (auto& [nm, a] : sc.areas) if (lodOf(nm) == top) level.push_back({nm, 0, 0, 256});
    while (true) {
        std::vector<Piece> nxt;
        fin.clear();
        bool changed = false;
        for (auto& p : level) {
            std::vector<std::tuple<int, int, std::string>> kids;
            if (p.span == 256) for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) kids.emplace_back(a, b, kid(p.name, a, b));
            bool any = false;
            for (auto& [a, b, k] : kids) if (!k.empty() && usable(k, 0)) any = true;
            if (any) {
                changed = true;
                for (auto& [a, b, k] : kids) {
                    if (!k.empty() && usable(k, 0)) nxt.push_back({k, 0, 0, 256});
                    else if (hasHeight(p.name)) fin.push_back({p.name, a * 128, b * 128, 128});
                }
            } else {
                nxt.push_back(p);
            }
        }
        int count = 0;
        for (auto* v : {&nxt, &finalP, &fin}) for (auto& p : *v) if (hasHeight(p.name)) count++;
        if (!changed || count > budget) break;
        level = nxt;
        finalP.insert(finalP.end(), fin.begin(), fin.end());
    }
    std::vector<Piece> out;
    for (auto* v : {&level, &finalP}) for (auto& p : *v) if (hasHeight(p.name)) out.push_back(p);
    return out;
}

// TARB : en-tête puis zstd ; blocs (décalage relatif, taille) à 0x1C + 0x10·k
void tarb(const Bytes& raw, Bytes& data, std::vector<std::pair<size_t, size_t>>& blobs) {
    if (!has(raw, 0, "TARB")) throw Error("pas un TARB");
    size_t z = 0;
    for (size_t p = 8; p + 4 <= std::min<size_t>(raw.size(), 64); p++)
        if (raw[p] == 0x28 && raw[p + 1] == 0xB5 && raw[p + 2] == 0x2F && raw[p + 3] == 0xFD) { z = p; break; }
    if (!z) throw Error("TARB sans données zstd");
    data = Zs::unpackPlain(raw.data() + z, raw.size() - z);
    blobs.clear();
    for (int k = 0; k < 4; k++) {
        size_t o = 0x1C + 0x10 * size_t(k);
        if (o + 8 > data.size()) break;
        uint32_t rel = rd32(data, o), size = rd32(data, o + 4);
        if (size && o + rel + size <= data.size()) blobs.emplace_back(o + rel, size);
    }
}
}  // namespace

// (en-tête : nombre de matières à 0x34, décalage relatif de leur table à 0x74 ; chaque entrée, désignée par un
// décalage relatif : u32 couche, 3 float, puis les répétitions u et v par mètre)
static std::vector<TerrainMaterial> tscbMaterials(const Bytes& b) {
    std::vector<TerrainMaterial> out;
    if (b.size() < 0x80) return out;
    uint32_t count = rd32(b, 0x34);
    auto tryTable = [&](size_t table) {
        std::vector<TerrainMaterial> r;
        for (uint32_t k = 0; k < count; k++) {
            size_t p = table + 4 * size_t(k);
            if (p + 4 > b.size()) return std::vector<TerrainMaterial>{};
            size_t e = p + rd32(b, p);
            if (e + 24 > b.size()) return std::vector<TerrainMaterial>{};
            TerrainMaterial m;
            m.layer = int(rd32(b, e));
            m.u = rdf(b, e + 16);
            m.v = rdf(b, e + 20);
            if (m.layer < 0 || m.layer > 1024 || !(m.u > 0 && m.u < 100) || !(m.v > 0 && m.v < 100)) return std::vector<TerrainMaterial>{};
            r.push_back(m);
        }
        return r;
    };
    if (!count || count > 1024) return out;
    out = tryTable(0x74 + rd32(b, 0x74));
    for (size_t o = 0x60; out.empty() && o < 0x100; o += 4) out = tryTable(o + rd32(b, o));
    return out;
}

std::vector<TerrainMaterial> terrainMaterials(const Game& game, const std::string& scene) {
    try {
        return tscbMaterials(readFile(game.path("TerrainArc") / u8path(scene + ".tscb")));
    } catch (...) {
        return {};
    }
}

std::vector<std::string> terrainScenes(const Game& game) {
    std::vector<std::string> out;
    fs::path root = game.path("TerrainArc");
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (auto& e : fs::directory_iterator(root, ec)) {
        std::string n = pathStr(e.path().filename());
        if (endsWith(n, ".tscb") && fs::is_directory(root / u8path(n.substr(0, n.size() - 5)), ec)) out.push_back(n.substr(0, n.size() - 5));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Rangement des hauteurs : celui des textures du jeu (texBlockLinear) ou l'ancien (blockLinear). On garde celui qui
// donne le relief le plus lisse sur l'ensemble de la tuile, à pleine résolution.
static bool altLayout(const Bytes& d, size_t hb, size_t wg) {
    double rough[2] = {0, 0};
    for (int alt = 0; alt < 2; alt++) {
        auto at = [&](int x, int y) {
            size_t o = hb + (alt ? texBlockLinear(size_t(x) * 4, size_t(y), wg, 16) : blockLinear(size_t(x) * 4, size_t(y), wg, 16));
            float v = o + 4 <= d.size() ? rdf(d, o) : 0.f;
            return std::isfinite(v) ? double(v) : 0.0;
        };
        for (int y = 0; y + 1 < NSRC; y += 3)
            for (int x = 0; x + 1 < NSRC; x++) rough[alt] += std::fabs(at(x + 1, y) - at(x, y)) + std::fabs(at(x, y + 1) - at(x, y));
    }
    return rough[1] < rough[0];
}

std::vector<TerrainTile> terrainBuild(const Game& game, const std::string& scene, int budget) {
    fs::path root = game.path("TerrainArc");
    Scene sc = parseTscb(readFile(root / u8path(scene + ".tscb")));
    fs::path folder = root / u8path(scene);
    std::error_code ec;
    auto path = [&](const std::string& nm, const char* ext) -> fs::path {
        fs::path p = folder / u8path(nm + ext);
        return fs::is_regular_file(p, ec) ? p : fs::path();
    };
    auto pieces = select(sc, [&](const std::string& nm) { return !path(nm, ".hght").empty(); }, budget);
    int n = pieces.size() <= 400 ? 65 : 33;
    std::vector<TerrainTile> out;
    std::string cached;
    Bytes hdata, mdata;
    bool alt = false, decide = false;
    std::vector<std::pair<size_t, size_t>> hblobs, mblobs;
    size_t wg = (NSRC * 4 + 63) / 64;
    size_t blSize = wg * 64 * ((NSRC + 127) / 128 * 128);
    for (auto& p : pieces) {
        auto& a = sc.areas.at(p.name);
        if (cached != p.name) {
            cached = p.name;
            tarb(readFile(path(p.name, ".hght")), hdata, hblobs);
            decide = true;
            mdata.clear();
            mblobs.clear();
            fs::path mp = path(p.name, ".mate");
            if (mp.empty()) mp = path(p.name, ".adult.mate");
            if (!mp.empty()) { try { tarb(readFile(mp), mdata, mblobs); } catch (...) { mblobs.clear(); } }
        }
        size_t hb = SIZE_MAX;
        for (auto& [s, ln] : hblobs) if (ln == blSize) { hb = s; break; }
        if (hb == SIZE_MAX) throw Error("tuile de hauteurs au format inattendu");
        if (decide) { alt = altLayout(hdata, hb, wg); decide = false; }   // (décidé pour chaque fichier .hght)
        auto BL = [&](size_t xb, size_t y) { return alt ? texBlockLinear(xb, y, wg, 16) : blockLinear(xb, y, wg, 16); };
        int k = std::max(2, std::min(n, p.span / 4 + 1));
        int step = p.span / (k - 1);
        TerrainTile t;
        t.n = k;
        t.h.resize(size_t(k * k));
        t.mat.assign(size_t(k * k), 0);
        // (.mate : 4 numéros de matière par point (127 : aucune), puis, une grille plus loin, leurs 4 poids (somme 255))
        bool full = !mblobs.empty() && mblobs[0].second >= 2 * blSize;
        if (full) { t.ids.assign(size_t(k * k) * 4, 127); t.wts.assign(size_t(k * k) * 4, 0); }
        for (int y = 0; y < k; y++)
            for (int x = 0; x < k; x++) {
                size_t o = hb + BL(size_t(p.i0 + x * step) * 4, size_t(p.j0 + y * step));
                float v = o + 4 <= hdata.size() ? rdf(hdata, o) : NAN;
                t.h[size_t(y * k + x)] = std::isfinite(v) ? float(v * sc.hscale) : NAN;
                if (!mblobs.empty()) {
                    size_t mo = mblobs[0].first + BL(size_t(p.i0 + x * step) * 4, size_t(p.j0 + y * step));
                    if (mo < mdata.size()) t.mat[size_t(y * k + x)] = mdata[mo];
                    if (full && mo + blSize + 4 <= mdata.size())
                        for (int c = 0; c < 4; c++) {
                            t.ids[size_t(y * k + x) * 4 + size_t(c)] = mdata[mo + size_t(c)];
                            t.wts[size_t(y * k + x) * 4 + size_t(c)] = mdata[mo + blSize + size_t(c)];
                        }
                }
            }
        double side = a.size * sc.world * p.span / 256;
        t.side = float(side);
        t.cx = float((a.x - a.size / 2) * sc.world + (p.i0 + p.span / 2.0) / 256 * a.size * sc.world);
        t.cz = float((a.z - a.size / 2) * sc.world + (p.j0 + p.span / 2.0) / 256 * a.size * sc.world);
        out.push_back(std::move(t));
    }
    return out;
}

// ---- ombre ambiante précalculée ----
namespace {
// (adresse dans le rangement de la Switch : x en octets, y en rangées de blocs, wg GOB de large, bh GOB de haut)
size_t gobAddr(size_t x, size_t y, size_t wg, size_t bh) {
    size_t gx = x / 64, gy = y / 8, by = gy / bh, gi = gy % bh, xi = x % 64, yi = y % 8;
    return (by * wg + gx) * 512 * bh + gi * 512 + (xi / 32) * 256 + (yi / 2) * 64 + ((xi % 32) / 16) * 32 + (yi % 2) * 16 + (xi % 16);
}
// (bloc BC4 → 16 valeurs)
void bc4Block(const uint8_t* b, uint8_t out[16]) {
    int e0 = b[0], e1 = b[1], pal[8] = {e0, e1, 0, 0, 0, 0, 0, 0};
    if (e0 > e1) for (int k = 2; k < 8; k++) pal[k] = ((8 - k) * e0 + (k - 1) * e1) / 7;
    else { for (int k = 2; k < 6; k++) pal[k] = ((6 - k) * e0 + (k - 1) * e1) / 5; pal[6] = 0; pal[7] = 255; }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int t = 0; t < 16; t++) out[t] = uint8_t(pal[(bits >> (3 * t)) & 7]);
}
}  // namespace

std::vector<TerrainBake> terrainBakes(const Game& game, const std::string& scene, const std::string& variant) {
    std::vector<TerrainBake> out;
    fs::path root = game.path("TerrainArc");
    std::error_code ec;
    Scene sc;
    try { sc = parseTscb(readFile(root / u8path(scene + ".tscb"))); } catch (...) { return out; }
    fs::path folder = root / u8path(scene);
    for (auto& [name, a] : sc.areas) {
        fs::path f;
        for (std::string v : {variant, std::string(), std::string("child"), std::string("adult")}) {
            fs::path p = folder / u8path(name + (v.empty() ? "" : "." + v) + ".bake_ao.extm");
            if (fs::is_regular_file(p, ec)) { f = p; break; }
        }
        if (f.empty()) continue;
        try {
            Bytes data;
            std::vector<std::pair<size_t, size_t>> blobs;
            tarb(readFile(f), data, blobs);
            if (blobs.empty() || blobs[0].second < 139264) continue;
            const size_t base = blobs[0].first, W = 65, wg = 17, bh = 8;
            TerrainBake tb;
            tb.cx = float(a.x * sc.world);
            tb.cz = float(a.z * sc.world);
            tb.side = float(a.size * sc.world);
            tb.ao.assign(260 * 260, 255);
            for (size_t by = 0; by < W; by++)
                for (size_t bx = 0; bx < W; bx++) {
                    size_t o = base + gobAddr(bx * 16, by, wg, bh);
                    if (o + 8 > data.size()) continue;
                    uint8_t v[16];
                    bc4Block(&data[o], v);   // (canal R du BC5 : les 8 premiers octets)
                    for (size_t t = 0; t < 16; t++) tb.ao[(by * 4 + t / 4) * 260 + bx * 4 + t % 4] = v[t];
                }
            out.push_back(std::move(tb));
        } catch (...) {}
    }
    return out;
}

float terrainBakeAo(const std::vector<TerrainBake>& bakes, float x, float z) {
    const TerrainBake* best = nullptr;
    for (auto& b : bakes)
        if (std::fabs(x - b.cx) <= b.side / 2 && std::fabs(z - b.cz) <= b.side / 2 && (!best || b.side < best->side)) best = &b;
    if (!best) return 1.f;
    // (texels 2 à 258 pour la tuile ; bilinéaire)
    float u = 2.f + (x - (best->cx - best->side / 2)) / best->side * 256.f - 0.5f;
    float v = 2.f + (z - (best->cz - best->side / 2)) / best->side * 256.f - 0.5f;
    // (les bords de la texture (rangées 0 à 9, colonnes 250 et plus) portent des motifs en tirets : on s'en tient à
    //  l'intérieur, le bord reprend le texel intérieur le plus proche)
    u = std::clamp(u, 10.f, 249.f);
    v = std::clamp(v, 10.f, 249.f);
    int x0 = int(std::floor(u)), y0 = int(std::floor(v));
    float fx = std::clamp(u - float(x0), 0.f, 1.f), fy = std::clamp(v - float(y0), 0.f, 1.f);
    auto at = [&](int xx, int yy) { return float(best->ao[size_t(yy) * 260 + size_t(xx)]) / 255.f; };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

}  // namespace te
