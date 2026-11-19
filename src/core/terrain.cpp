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

}  // namespace te
