#include <cstring>
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
            // (lumière renvoyée précalculée, même tuile)
            std::string an = f.filename().string();
            fs::path gf = f.parent_path() / (an.substr(0, an.size() - std::string("bake_ao.extm").size()) + "bake_gi.extm");
            if (fs::is_regular_file(gf, ec)) {
                try {
                    Bytes gd;
                    std::vector<std::pair<size_t, size_t>> gb;
                    tarb(readFile(gf), gd, gb);
                    if (!gb.empty() && gb[0].second >= 6 * 139264) {
                        tb.gi.assign(size_t(260 * 260 * 18), 0.f);
                        float px[48];
                        for (size_t L = 0; L < 6; L++)
                            for (size_t by = 0; by < W; by++)
                                for (size_t bx = 0; bx < W; bx++) {
                                    size_t o = gb[0].first + L * 139264 + gobAddr(bx * 16, by, wg, bh);
                                    if (o + 16 > gd.size()) continue;
                                    bc6hBlock(&gd[o], true, px);
                                    for (size_t t = 0; t < 16; t++)
                                        for (size_t c = 0; c < 3; c++)
                                            tb.gi[((by * 4 + t / 4) * 260 + bx * 4 + t % 4) * 18 + L * 3 + c] = px[t * 3 + c];
                                }
                    }
                } catch (...) { tb.gi.clear(); }
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

// ---- BC6H ----
namespace {
struct Bc6Field { int f, hi, lo; };   // (f : 0..11 = r0..r3, g0..g3, b0..b3 ; hi < lo : bits rangés à l'envers)
struct Bc6Mode { int code, regions; bool transformed; int epb, delta[3]; std::vector<Bc6Field> fields; };
std::vector<Bc6Field> bc6Layout(const char* s) {
    std::vector<Bc6Field> out;
    std::string t(s);
    size_t i = 0;
    while (i < t.size()) {
        size_t j = t.find(',', i);
        if (j == std::string::npos) j = t.size();
        std::string tok = t.substr(i, j - i);
        int c = tok[0] == 'r' ? 0 : tok[0] == 'g' ? 1 : 2, k = tok[1] - '0';
        size_t br = tok.find('['), co = tok.find(':');
        int a = std::stoi(tok.substr(br + 1)), bb = co == std::string::npos ? a : std::stoi(tok.substr(co + 1));
        out.push_back({c * 4 + k, a, bb});
        i = j + 1;
    }
    return out;
}
const std::vector<Bc6Mode>& bc6Modes() {
    static const std::vector<Bc6Mode> m = {
        {0x00, 2, true, 10, {5, 5, 5}, bc6Layout("g2[4],b2[4],b3[4],r0[9:0],g0[9:0],b0[9:0],r1[4:0],g3[4],g2[3:0],g1[4:0],b3[0],g3[3:0],b1[4:0],b3[1],b2[3:0],r2[4:0],b3[2],r3[4:0],b3[3]")},
        {0x01, 2, true, 7, {6, 6, 6}, bc6Layout("g2[5],g3[4],g3[5],r0[6:0],b3[0],b3[1],b2[4],g0[6:0],b2[5],b3[2],g2[4],b0[6:0],b3[3],b3[5],b3[4],r1[5:0],g2[3:0],g1[5:0],g3[3:0],b1[5:0],b2[3:0],r2[5:0],r3[5:0]")},
        {0x02, 2, true, 11, {5, 4, 4}, bc6Layout("r0[9:0],g0[9:0],b0[9:0],r1[4:0],r0[10],g2[3:0],g1[3:0],g0[10],b3[0],g3[3:0],b1[3:0],b0[10],b3[1],b2[3:0],r2[4:0],b3[2],r3[4:0],b3[3]")},
        {0x06, 2, true, 11, {4, 5, 4}, bc6Layout("r0[9:0],g0[9:0],b0[9:0],r1[3:0],r0[10],g3[4],g2[3:0],g1[4:0],g0[10],g3[3:0],b1[3:0],b0[10],b3[1],b2[3:0],r2[3:0],b3[0],b3[2],r3[3:0],g2[4],b3[3]")},
        {0x0A, 2, true, 11, {4, 4, 5}, bc6Layout("r0[9:0],g0[9:0],b0[9:0],r1[3:0],r0[10],b2[4],g2[3:0],g1[3:0],g0[10],b3[0],g3[3:0],b1[4:0],b0[10],b2[3:0],r2[3:0],b3[1],b3[2],r3[3:0],b3[4],b3[3]")},
        {0x0E, 2, true, 9, {5, 5, 5}, bc6Layout("r0[8:0],b2[4],g0[8:0],g2[4],b0[8:0],b3[4],r1[4:0],g3[4],g2[3:0],g1[4:0],b3[0],g3[3:0],b1[4:0],b3[1],b2[3:0],r2[4:0],b3[2],r3[4:0],b3[3]")},
        {0x12, 2, true, 8, {6, 5, 5}, bc6Layout("r0[7:0],g3[4],b2[4],g0[7:0],b3[2],g2[4],b0[7:0],b3[3],b3[4],r1[5:0],g2[3:0],g1[4:0],b3[0],g3[3:0],b1[4:0],b3[1],b2[3:0],r2[5:0],r3[5:0]")},
        {0x16, 2, true, 8, {5, 6, 5}, bc6Layout("r0[7:0],b3[0],b2[4],g0[7:0],g2[5],g2[4],b0[7:0],g3[5],b3[4],r1[4:0],g3[4],g2[3:0],g1[5:0],g3[3:0],b1[4:0],b3[1],b2[3:0],r2[4:0],b3[2],r3[4:0],b3[3]")},
        {0x1A, 2, true, 8, {5, 5, 6}, bc6Layout("r0[7:0],b3[1],b2[4],g0[7:0],b2[5],g2[4],b0[7:0],b3[5],b3[4],r1[4:0],g3[4],g2[3:0],g1[4:0],b3[0],g3[3:0],b1[5:0],b2[3:0],r2[4:0],b3[2],r3[4:0],b3[3]")},
        {0x1E, 2, false, 6, {6, 6, 6}, bc6Layout("r0[5:0],g3[4],b3[0],b3[1],b2[4],g0[5:0],g2[5],b2[5],b3[2],g2[4],b0[5:0],g3[5],b3[3],b3[5],b3[4],r1[5:0],g2[3:0],g1[5:0],g3[3:0],b1[5:0],b2[3:0],r2[5:0],r3[5:0]")},
        {0x03, 1, false, 10, {10, 10, 10}, bc6Layout("r0[9:0],g0[9:0],b0[9:0],r1[9:0],g1[9:0],b1[9:0]")},
        {0x07, 1, true, 11, {9, 9, 9}, bc6Layout("r0[9:0],g0[9:0],b0[9:0],r1[8:0],r0[10],g1[8:0],g0[10],b1[8:0],b0[10]")},
        {0x0B, 1, true, 12, {8, 8, 8}, bc6Layout("r0[9:0],g0[9:0],b0[9:0],r1[7:0],r0[10:11],g1[7:0],g0[10:11],b1[7:0],b0[10:11]")},
        {0x0F, 1, true, 16, {4, 4, 4}, bc6Layout("r0[9:0],g0[9:0],b0[9:0],r1[3:0],r0[10:15],g1[3:0],g0[10:15],b1[3:0],b0[10:15]")},
    };
    return m;
}
const uint16_t kP2[32] = {0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80, 0xC800, 0xFFEC, 0xFE80, 0xE800, 0xFFE8, 0xFF00, 0xFFF0, 0xF000,
                          0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310, 0x3100, 0x8CCE, 0x088C, 0x3110, 0x6666, 0x366C, 0x17E8, 0x0FF0, 0x718E, 0x399C};
const uint8_t kA2[32] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2};
const int kW3[8] = {0, 9, 18, 27, 37, 46, 55, 64}, kW4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
int sext(int x, int n) { return (x & (1 << (n - 1))) ? x - (1 << n) : x; }
float halfToFloat(uint16_t h) {
    uint32_t s = uint32_t(h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 0x3FF, f;
    if (e == 0) {
        if (m == 0) f = s;
        else { e = 127 - 15 + 1; while (!(m & 0x400)) { m <<= 1; e--; } m &= 0x3FF; f = s | (e << 23) | (m << 13); }
    } else if (e == 31) f = s | 0x7F800000 | (m << 13);
    else f = s | ((e + 127 - 15) << 23) | (m << 13);
    float r;
    std::memcpy(&r, &f, 4);
    return r;
}
}  // namespace

void bc6hBlock(const uint8_t* blk, bool sg, float out[48]) {
    uint64_t lo, hi;
    std::memcpy(&lo, blk, 8);
    std::memcpy(&hi, blk + 8, 8);
    int pos = 0;
    auto rd = [&](int n) -> uint32_t {
        uint32_t r = 0;
        for (int i = 0; i < n; i++, pos++) {
            uint64_t bit = pos < 64 ? (lo >> pos) & 1 : (hi >> (pos - 64)) & 1;
            r |= uint32_t(bit) << i;
        }
        return r;
    };
    for (int i = 0; i < 48; i++) out[i] = 0;
    int code = int(rd(2));
    if (code >= 2) code |= int(rd(3)) << 2;
    const Bc6Mode* md = nullptr;
    for (auto& m : bc6Modes()) if (m.code == code) { md = &m; break; }
    if (!md) return;
    int E[12] = {};
    for (auto& f : md->fields) {
        if (f.hi >= f.lo) E[f.f] |= int(rd(f.hi - f.lo + 1)) << f.lo;
        else {
            int n = f.lo - f.hi + 1, x = int(rd(n)), r = 0;
            for (int i = 0; i < n; i++) r |= ((x >> i) & 1) << (n - 1 - i);
            E[f.f] |= r << f.hi;
        }
    }
    int part = md->regions == 2 ? int(rd(5)) : 0;
    int epb = md->epb, mask = (1 << epb) - 1, ne = 2 * md->regions;
    int ep[3][4];
    for (int c = 0; c < 3; c++) {
        int w = E[c * 4] & mask;
        int v[4] = {w, 0, 0, 0};
        for (int k = 1; k < ne; k++) {
            int x = E[c * 4 + k];
            int d = md->delta[c];
            if (md->transformed) x = (w + sext(x & ((1 << d) - 1), d)) & mask;
            v[k] = x & mask;
        }
        for (int k = 0; k < ne; k++) {
            int x = v[k], q;
            if (sg) {
                x = sext(x, epb);
                if (epb >= 16) q = x;
                else {
                    bool neg = x < 0;
                    int a = neg ? -x : x;
                    q = a == 0 ? 0 : a >= (1 << (epb - 1)) - 1 ? 0x7FFF : ((a << 15) + 0x4000) >> (epb - 1);
                    if (neg) q = -q;
                }
            } else {
                q = epb >= 15 ? x : x == 0 ? 0 : x == mask ? 0xFFFF : ((x << 16) + 0x8000) >> epb;
            }
            ep[c][k] = q;
        }
    }
    for (int t = 0; t < 16; t++) {
        int sub = 0, w;
        if (md->regions == 1) w = kW4[rd(t == 0 ? 3 : 4)];
        else {
            sub = (kP2[part] >> t) & 1;
            w = kW3[rd((t == 0 || t == kA2[part]) ? 2 : 3)];
        }
        for (int c = 0; c < 3; c++) {
            int q = ((64 - w) * ep[c][2 * sub] + w * ep[c][2 * sub + 1] + 32) >> 6;
            uint16_t h;
            if (sg) h = q < 0 ? uint16_t(0x8000 | ((((-q) * 31) >> 5) & 0x7FFF)) : uint16_t(((q * 31) >> 5) & 0x7FFF);
            else h = uint16_t((q * 31) >> 6);
            out[t * 3 + c] = halfToFloat(h);
        }
    }
}

bool terrainBakeGi(const std::vector<TerrainBake>& bakes, float x, float z, const float n[3], float out[7]) {
    const TerrainBake* best = nullptr;
    for (auto& b : bakes)
        if (!b.gi.empty() && std::fabs(x - b.cx) <= b.side / 2 && std::fabs(z - b.cz) <= b.side / 2 && (!best || b.side < best->side)) best = &b;
    if (!best) return false;
    float u = std::clamp(2.f + (x - (best->cx - best->side / 2)) / best->side * 256.f - 0.5f, 10.f, 249.f);
    float v = std::clamp(2.f + (z - (best->cz - best->side / 2)) / best->side * 256.f - 0.5f, 10.f, 249.f);
    int x0 = int(u), y0 = int(v);
    float fx = u - float(x0), fy = v - float(y0);
    float g[18];
    for (int k = 0; k < 18; k++) {
        auto at = [&](int xx, int yy) { return best->gi[(size_t(yy) * 260 + size_t(xx)) * 18 + size_t(k)]; };
        g[k] = (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
    }
    // (direction rangée (r = y, g = x, b = z) = L1 / L0 / 2 ; E = 0,886·L0·(1 + 1,155·2·d·n))
    auto lobe = [&](int l) {
        float d = g[l * 3 + 1] * n[0] + g[l * 3 + 0] * n[1] + g[l * 3 + 2] * n[2];
        return std::max(0.f, 0.886f * (1.f + 2.31f * d));
    };
    float fs = lobe(3), fb = lobe(5), fa = lobe(1);
    out[0] = std::max(0.f, g[6]) * fs;
    for (int c = 0; c < 3; c++) out[1 + c] = std::max(0.f, g[12 + c]) * fb;
    for (int c = 0; c < 3; c++) out[4 + c] = std::max(0.f, g[0 + c]) * fa;
    return true;
}

}  // namespace te
