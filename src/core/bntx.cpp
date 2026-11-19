#include "core/bntx.hpp"

#include <algorithm>
#include <map>
#include <vector>

namespace te {

const int ASTC_DIMS[14][2] = {{4, 4}, {5, 4}, {5, 5}, {6, 5}, {6, 6}, {8, 5}, {8, 6}, {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12}};

size_t blockLinear(size_t x, size_t y, size_t wg, size_t bh) {
    size_t gh = 8 * bh;
    return (y / gh) * 512 * bh * wg + (x / 64) * 512 * bh + ((y % gh) / 8) * 512 + ((x % 64) / 32) * 256 + ((y % 8) / 2) * 64 +
           ((x % 32) / 16) * 32 + (y % 2) * 16 + (x % 16);
}

namespace {
struct Fmt { std::string name; int bw, bh, bpb, code; };
const std::map<uint32_t, Fmt>& formats() {
    static std::map<uint32_t, Fmt> f = [] {
        std::map<uint32_t, Fmt> m = {
            {0x0b01, {"RGBA8", 1, 1, 4, 1}}, {0x0b06, {"RGBA8 sRGB", 1, 1, 4, 1}},
            {0x1a01, {"BC1", 4, 4, 8, 2}}, {0x1a06, {"BC1 sRGB", 4, 4, 8, 2}},
            {0x1b01, {"BC2", 4, 4, 16, 3}}, {0x1b06, {"BC2 sRGB", 4, 4, 16, 3}},
            {0x1c01, {"BC3", 4, 4, 16, 4}}, {0x1c06, {"BC3 sRGB", 4, 4, 16, 4}},
            {0x1d01, {"BC4", 4, 4, 8, 5}}, {0x1d02, {"BC4 signé", 4, 4, 8, 5}},
            {0x1e01, {"BC5", 4, 4, 16, 6}}, {0x1e02, {"BC5 signé", 4, 4, 16, 6}},
            {0x1f05, {"BC6H", 4, 4, 16, 8}}, {0x1f0a, {"BC6H", 4, 4, 16, 8}},
            {0x2001, {"BC7", 4, 4, 16, 7}}, {0x2006, {"BC7 sRGB", 4, 4, 16, 7}}};
        for (int i = 0; i < 14; i++)
            for (uint32_t t : {1u, 6u})
                m[((0x2du + uint32_t(i)) << 8) | t] = {fmt("ASTC %dx%d", ASTC_DIMS[i][0], ASTC_DIMS[i][1]), ASTC_DIMS[i][0], ASTC_DIMS[i][1], 16, 20 + i};
        return m;
    }();
    return f;
}

uint64_t u64s(const Bytes& b, size_t o) { return o + 8 <= b.size() ? rd64(b, o) : 0; }

std::string nameAt(const Bytes& d, uint64_t p) {
    if (!(p > 0 && p + 2 < d.size())) return "";
    size_t ln = rd16(d, size_t(p));
    if (p + 2 + ln > d.size()) return "";
    return std::string(d.begin() + long(p + 2), d.begin() + long(p + 2 + ln));
}

std::vector<std::pair<size_t, std::string>> textures(const Bytes& d) {
    std::vector<std::pair<size_t, std::string>> out;
    size_t n = d.size();
    uint32_t count = n > 0x28 ? rd32(d, 0x24) : 0;
    uint64_t arr = u64s(d, 0x28);
    if (arr > 0 && arr < n && count > 0 && count <= 4096) {
        for (uint32_t i = 0; i < count; i++) {
            uint64_t p = u64s(d, size_t(arr) + 8 * i);
            if (p > 0 && p < n - 0xA0 && has(d, size_t(p), "BRTI")) out.emplace_back(size_t(p), nameAt(d, u64s(d, size_t(p) + 0x60)));
        }
    }
    if (out.empty()) {
        for (size_t p = 0x20; p + 0xA0 < n && out.size() < 4096; p++)
            if (std::memcmp(d.data() + p, "BRTI", 4) == 0) out.emplace_back(p, nameAt(d, u64s(d, p + 0x60)));
    }
    return out;
}

struct Info {
    std::string name;
    uint32_t format = 0;
    int w = 0, h = 0, mips = 0, tileMode = 0, bhLog2 = 0;
    std::vector<size_t> mipOffsets;
    uint32_t compSel = 0x05040302;
};

Info info(const Bytes& d, const std::string& want) {
    if (!has(d, 0, "BNTX")) throw Error("pas un BNTX");
    auto lst = textures(d);
    if (lst.empty()) throw Error("pas de texture (BRTI)");
    size_t brti = lst[0].first;
    bool picked = false;
    if (!want.empty())
        for (auto& [p, nm] : lst) if (lower(nm) == lower(want)) { brti = p; picked = true; break; }
    if (!picked)
        for (auto& [p, nm] : lst) if (endsWith(lower(nm), "_alb")) { brti = p; break; }
    Info t;
    t.tileMode = rd16(d, brti + 0x12);
    t.mips = rd16(d, brti + 0x16);
    t.format = rd32(d, brti + 0x1C);
    t.w = int32_t(rd32(d, brti + 0x24));
    t.h = int32_t(rd32(d, brti + 0x28));
    t.bhLog2 = int32_t(rd32(d, brti + 0x34)) & 7;
    uint32_t cs = rd32(d, brti + 0x58);
    bool ok = cs != 0;
    for (int k = 0; k < 4; k++) if (((cs >> (8 * k)) & 0xFF) > 5) ok = false;
    if (ok) t.compSel = cs;
    if (!(t.w > 0 && t.w <= 16384 && t.h > 0 && t.h <= 16384 && t.mips > 0 && t.mips <= 16))
        throw Error(fmt("en-tête de texture incohérent (%dx%d, %d mips)", t.w, t.h, t.mips));
    t.name = nameAt(d, u64s(d, brti + 0x60));
    uint64_t ptrs = u64s(d, brti + 0x70);
    if (ptrs > 0 && ptrs + 8 * size_t(t.mips) < d.size())
        for (int i = 0; i < t.mips; i++) t.mipOffsets.push_back(size_t(u64s(d, size_t(ptrs) + 8 * size_t(i))));
    bool good = !t.mipOffsets.empty();
    for (auto o : t.mipOffsets) if (!(o > 0 && o < d.size())) good = false;
    if (!good) {
        t.mipOffsets.clear();
        for (size_t p = 0x20; p + 4 <= d.size(); p++)
            if (std::memcmp(d.data() + p, "BRTD", 4) == 0) { t.mipOffsets.push_back(p + 0x10); break; }
        if (t.mipOffsets.empty()) throw Error("emplacement des données introuvable");
    }
    return t;
}

size_t pow2(size_t v) { size_t p = 1; while (p < v) p <<= 1; return p; }
size_t mipBlockHeight(size_t hb, size_t bh) { while (bh > 1 && pow2(hb) < bh * 8) bh /= 2; return bh; }

Bytes deswizzle(const Bytes& d, size_t start, size_t wb, size_t hb, size_t bpb, size_t gobBh, int level) {
    size_t wg = (wb * bpb + 63) / 64;
    size_t span = wg * 64 * ((hb + 8 * gobBh - 1) / (8 * gobBh) * 8 * gobBh);
    if (start + span > d.size()) throw Error(fmt("données hors du fichier (niveau %d)", level));
    Bytes out(wb * hb * bpb);
    for (size_t y = 0; y < hb; y++)
        for (size_t x = 0; x < wb; x++) {
            size_t o = start + blockLinear(x * bpb, y, wg, gobBh);
            std::memcpy(out.data() + (y * wb + x) * bpb, d.data() + o, bpb);
        }
    return out;
}
}  // namespace

TexData bntxExtract(const Bytes& d, int maxSize, const std::string& want) {
    Info t = info(d, want);
    auto it = formats().find(t.format);
    if (it == formats().end()) throw Error(fmt("format de texture %#06x inconnu", t.format));
    const Fmt& f = it->second;
    int level = 0;
    while (level + 1 < std::min<int>(t.mips, int(t.mipOffsets.size())) && std::max(t.w >> level, t.h >> level) > maxSize) level++;
    TexData r;
    r.w = std::max(1, t.w >> level);
    r.h = std::max(1, t.h >> level);
    size_t wb = size_t((r.w + f.bw - 1) / f.bw), hb = size_t((r.h + f.bh - 1) / f.bh), bpb = size_t(f.bpb);
    size_t start = t.mipOffsets[size_t(level)];
    r.code = f.code;
    r.format = f.name;
    r.srgb = (t.format & 0xFF) == 6;
    r.compSel = t.compSel;
    if (t.tileMode == 1) {
        size_t pitch = (wb * bpb + 31) / 32 * 32;
        if (start + pitch * (hb - 1) + wb * bpb > d.size()) throw Error("données hors du fichier");
        r.blocks.resize(wb * hb * bpb);
        for (size_t y = 0; y < hb; y++) std::memcpy(r.blocks.data() + y * wb * bpb, d.data() + start + y * pitch, wb * bpb);
        return r;
    }
    size_t gobBh = mipBlockHeight(hb, size_t(1) << t.bhLog2);
    r.blocks = deswizzle(d, start, wb, hb, bpb, gobBh, level);
    return r;
}

std::vector<std::string> bntxNames(const Bytes& data) {
    std::vector<std::string> out;
    if (!has(data, 0, "BNTX")) return out;
    for (auto& [p, nm] : textures(data)) out.push_back(nm);
    return out;
}

std::string bntxDiagnose(const Bytes& d, const std::string& want) {
    auto lst = textures(d);
    Info t = info(d, want);
    auto it = formats().find(t.format);
    std::string out = fmt("%s : %s %dx%d, %d mips, rangement %d, bloc 2^%d, canaux %08x\n", t.name.c_str(),
                          it != formats().end() ? it->second.name.c_str() : fmt("%#06x", t.format).c_str(), t.w, t.h, t.mips, t.tileMode, t.bhLog2, t.compSel);
    out += fmt("textures du fichier (%zu) : ", lst.size());
    for (size_t k = 0; k < lst.size() && k < 8; k++) {
        auto f2 = formats().find(rd32(d, lst[k].first + 0x1C));
        out += (k ? ", " : "") + lst[k].second + " " + (f2 != formats().end() ? f2->second.name : "?");
    }
    out += "\nmips : ";
    for (size_t k = 0; k < t.mipOffsets.size() && k < 6; k++) out += fmt("%s%#zx", k ? ", " : "", t.mipOffsets[k]);
    out += fmt(" ; fichier %#zx", d.size());
    return out;
}

}  // namespace te
