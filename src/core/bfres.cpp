// Port de triforce_modkit/bfres.py : les structures dont la place varie sont retrouvées par leurs signatures.
#include "core/bfres.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <functional>

namespace te {

namespace {
struct Fmt { char ch; int n; double norm; };   // (norm 0 : pas de normalisation)
const std::map<uint16_t, Fmt>& formats() {
    static const std::map<uint16_t, Fmt> f = {
        {0x516, {'f', 1, 0}}, {0x517, {'f', 2, 0}}, {0x518, {'f', 3, 0}}, {0x519, {'f', 4, 0}},
        {0x50A, {'e', 1, 0}}, {0x512, {'e', 2, 0}}, {0x515, {'e', 4, 0}},
        {0x215, {'h', 4, 32767.0}}, {0x115, {'H', 4, 65535.0}}, {0x212, {'h', 2, 32767.0}}, {0x112, {'H', 2, 65535.0}},
        {0x20B, {'b', 4, 127.0}}, {0x10B, {'B', 4, 255.0}}, {0x209, {'b', 2, 127.0}}, {0x109, {'B', 2, 255.0}},
        {0x302, {'B', 1, 0}}, {0x309, {'B', 2, 0}}, {0x30B, {'B', 4, 0}}, {0x30A, {'H', 1, 0}}, {0x312, {'H', 2, 0}},
        {0x315, {'H', 4, 0}}, {0x415, {'h', 4, 0}}, {0x314, {'I', 1, 0}}, {0x317, {'I', 2, 0}}, {0x318, {'I', 3, 0}},
        {0x319, {'I', 4, 0}}, {0x802, {'B', 1, 0}}, {0x804, {'B', 2, 0}}, {0x80B, {'B', 4, 0}}, {0x803, {'H', 1, 0}},
        {0x807, {'H', 2, 0}}, {0x80E, {'H', 4, 0}}, {0xA0E, {'h', 4, 0}},
    };
    return f;
}
int fmtSize(char c) { return c == 'f' || c == 'I' ? 4 : c == 'e' || c == 'h' || c == 'H' ? 2 : 1; }
bool isFloat(char c) { return c == 'f' || c == 'e'; }

float half(uint16_t h) {
    int s = h >> 15, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp(float(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(float(m + 1024), e - 25);
    return s ? -v : v;
}

struct R {
    const Bytes& d;
    size_t n;
    explicit R(const Bytes& b) : d(b), n(b.size()) {}
    uint8_t u8(size_t o) const { return rd8(d, o); }
    uint16_t u16(size_t o) const { return rd16(d, o); }
    int16_t i16(size_t o) const { return int16_t(rd16(d, o)); }
    uint32_t u32(size_t o) const { return rd32(d, o); }
    int32_t i32(size_t o) const { return int32_t(rd32(d, o)); }
    uint64_t u64(size_t o) const { return rd64(d, o); }
    float f(size_t o) const { return rdf(d, o); }
    bool magic(size_t o, const char* m) const { return o > 0 && o + 4 <= n && std::memcmp(d.data() + o, m, 4) == 0; }
    size_t ptr(size_t o) const {
        if (o + 8 > n) return 0;
        uint64_t p = u64(o);
        return p > 0 && p < n ? size_t(p) : 0;
    }
    bool str(size_t o, std::string& out) const {
        size_t p = ptr(o);
        if (!p || p + 2 > n) return false;
        size_t lim = std::min(n, p + 2 + 512);
        auto e = std::find(d.begin() + long(p + 2), d.begin() + long(lim), 0);
        if (e == d.begin() + long(lim)) return false;
        out.assign(d.begin() + long(p + 2), e);
        if (out.empty()) return false;
        for (unsigned char c : out) if (c < 32) return false;   // (texte UTF-8 : octets >= 0x80 acceptés)
        return true;
    }
    std::string str(size_t o) const { std::string s; return str(o, s) ? s : std::string(); }
    int dictCount(size_t o) const {
        size_t p = ptr(o);
        if (!p || p + 8 > n) return 0;
        int c = i32(p + 4);
        return c >= 0 && c < 100000 ? c : 0;
    }
};

size_t arrayStride(const R& r, size_t first, int count, const char* sig, size_t guess) {
    if (count <= 1) return guess;
    std::set<size_t> cands = {guess, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x98, 0xA0, 0xB0, 0xC0, 0x58, 0x50};
    for (size_t st : cands) {
        bool ok = true;
        for (int i = 0; i < count && ok; i++) ok = r.magic(first + size_t(i) * st, sig);
        if (ok) return st;
    }
    throw Error(std::string("tableau de ") + sig + " : taille des éléments introuvable");
}

std::pair<size_t, size_t> findPtr(const R& r, size_t base, size_t size, const char* sig) {
    for (size_t o = base + 8; o < base + size; o += 8) {
        size_t p = r.ptr(o);
        if (p && r.magic(p, sig)) return {o, p};
    }
    return {0, 0};
}

using M34 = std::array<double, 12>;
const M34 IDENT = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

std::array<double, 9> quatMat(double x, double y, double z, double w) {
    double nn = std::sqrt(x * x + y * y + z * z + w * w);
    if (nn == 0) nn = 1;
    x /= nn; y /= nn; z /= nn; w /= nn;
    return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
            2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
            2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)};
}
std::array<double, 9> eulerMat(double ex, double ey, double ez) {
    double cx = std::cos(ex), sx = std::sin(ex), cy = std::cos(ey), sy = std::sin(ey), cz = std::cos(ez), sz = std::sin(ez);
    return {cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
            sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
            -sy, cy * sx, cy * cx};
}
M34 compose(const M34& a, const M34& b) {
    M34 o{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++) {
            double v = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j];
            if (j == 3) v += a[i * 4 + 3];
            o[size_t(i * 4 + j)] = v;
        }
    return o;
}

void bones(const R& r, size_t skel, std::vector<M34>& world, std::vector<uint16_t>& table) {
    size_t arr = r.ptr(skel + 0x10);
    int count = r.u16(skel + 0x38);
    if (!arr || count <= 0 || count >= 4096) return;
    size_t base = 0, stride = 0;
    bool found = false;
    for (size_t b : {0x20, 0x18, 0x28, 0x10}) {
        for (size_t st : {0x58, 0x50, 0x60, 0x68}) {
            if (arr + size_t(count) * st > r.n) continue;
            bool ok = true;
            for (int i = 0; i < count && ok; i++) {
                size_t o = arr + size_t(i) * st;
                if (o + b + 0x10 + 40 > r.n) { ok = false; break; }
                int par = r.i16(o + b + 2);
                if (r.u16(o + b) != i || par < -1 || par >= count || par == i) { ok = false; break; }
                for (int k = 0; k < 10; k++) {
                    float v = r.f(o + b + 0x10 + 4 * size_t(k));
                    if (!std::isfinite(v) || std::fabs(v) >= 1e6f) { ok = false; break; }
                }
            }
            if (ok) { base = b; stride = st; found = true; break; }
        }
        if (found) break;
    }
    if (!found) throw Error("os du squelette illisibles");
    std::vector<M34> local(static_cast<size_t>(count));
    std::vector<int> parents(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        size_t o = arr + size_t(i) * stride + base;
        parents[size_t(i)] = r.i16(o + 2);
        uint32_t flags = r.u32(o + 0x0C);
        double sc[3] = {r.f(o + 0x10), r.f(o + 0x14), r.f(o + 0x18)};
        double rot[4] = {r.f(o + 0x1C), r.f(o + 0x20), r.f(o + 0x24), r.f(o + 0x28)};
        double pos[3] = {r.f(o + 0x2C), r.f(o + 0x30), r.f(o + 0x34)};
        auto m = (flags & 0x7000) == 0x1000 ? eulerMat(rot[0], rot[1], rot[2]) : quatMat(rot[0], rot[1], rot[2], rot[3]);
        local[size_t(i)] = {m[0] * sc[0], m[1] * sc[1], m[2] * sc[2], pos[0], m[3] * sc[0], m[4] * sc[1], m[5] * sc[2], pos[1],
                            m[6] * sc[0], m[7] * sc[1], m[8] * sc[2], pos[2]};
    }
    world.assign(size_t(count), IDENT);
    std::vector<char> done(size_t(count), 0);
    std::function<const M34&(int, int)> get = [&](int i, int depth) -> const M34& {
        if (!done[size_t(i)]) {
            int p = parents[size_t(i)];
            world[size_t(i)] = p < 0 || depth > 256 ? local[size_t(i)] : compose(get(p, depth + 1), local[size_t(i)]);
            done[size_t(i)] = 1;
        }
        return world[size_t(i)];
    };
    for (int i = 0; i < count; i++) get(i, 0);
    size_t nmat = size_t(r.u16(skel + 0x3A)) + r.u16(skel + 0x3C);
    size_t mtb = r.ptr(skel + 0x18);
    if (mtb && nmat && mtb + nmat * 2 <= r.n)
        for (size_t k = 0; k < nmat; k++) table.push_back(r.u16(mtb + 2 * k));
}

struct Attr { std::string name; bool named = false; uint16_t format = 0, offset = 0, buffer = 0; };
struct Buf { size_t start, stride, size; };

std::vector<std::array<double, 4>> attrValues(const R& r, const Buf& b, const Attr& a, size_t count) {
    auto it = formats().find(a.format);
    if (it == formats().end()) throw Error(fmt("format d'attribut %#x non pris en charge", a.format));
    const Fmt& f = it->second;
    size_t es = size_t(fmtSize(f.ch) * f.n);
    if (b.start + a.offset + (count - 1) * b.stride + es > b.start + b.size || b.start + b.size > r.n)
        throw Error("tampon de sommets hors du fichier");
    std::vector<std::array<double, 4>> out(count);
    for (size_t i = 0; i < count; i++) {
        size_t o = b.start + i * b.stride + a.offset;
        for (int k = 0; k < f.n; k++) {
            double v;
            size_t p = o + size_t(k * fmtSize(f.ch));
            switch (f.ch) {
                case 'f': v = r.f(p); break;
                case 'e': v = half(r.u16(p)); break;
                case 'h': v = r.i16(p); break;
                case 'H': v = r.u16(p); break;
                case 'b': v = int8_t(r.u8(p)); break;
                case 'B': v = r.u8(p); break;
                default: v = r.u32(p); break;
            }
            if (f.norm != 0) v = std::max(-1.0, v / f.norm);
            out[i][size_t(k)] = v;
        }
    }
    return out;
}

void vertexBuffer(const R& r, size_t vb, size_t bufferOffset, std::vector<Attr>& attrs, std::vector<Buf>& bufs, size_t& count) {
    if (!r.magic(vb, "FVTX")) throw Error("FVTX attendu");
    int nAttr = r.u8(vb + 0x4C), nBuf = r.u8(vb + 0x4D);
    count = r.u32(vb + 0x50);
    uint16_t align = r.u16(vb + 0x56);
    size_t attrsP = r.ptr(vb + 0x08), sizesP = r.ptr(vb + 0x30), stridesP = r.ptr(vb + 0x38);
    if (!(nAttr > 0 && nAttr < 64 && nBuf > 0 && nBuf < 32 && count > 0 && count < 10000000 && attrsP && sizesP && stridesP))
        throw Error(fmt("FVTX incohérent (%d attributs, %d tampons, %zu sommets)", nAttr, nBuf, count));
    for (int i = 0; i < nAttr; i++) {
        size_t o = attrsP + size_t(i) * 16;
        Attr a;
        a.named = r.str(o, a.name);
        a.format = rd16be(r.d, o + 8);
        a.offset = r.u16(o + 12);
        a.buffer = r.u16(o + 14);
        attrs.push_back(a);
    }
    size_t pos = size_t(int64_t(bufferOffset) + r.i32(vb + 0x48));
    for (int b = 0; b < nBuf; b++) {
        if (align > 1) pos = (pos + align - 1) / align * align;
        size_t size = r.u32(sizesP + size_t(b) * 16), stride = r.u32(stridesP + size_t(b) * 16);
        bufs.push_back({pos, stride, size});
        pos += size;
    }
}

const Attr* pickAttr(const std::vector<Attr>& attrs, const std::string& name) {
    for (auto& a : attrs) if (a.named && a.name == name) return &a;
    auto f = [](const Attr& a) -> const Fmt* { auto it = formats().find(a.format); return it == formats().end() ? nullptr : &it->second; };
    bool allUnnamed = std::all_of(attrs.begin(), attrs.end(), [](auto& a) { return !a.named; });
    if (name == "_p0") {
        for (auto& a : attrs) if (a.buffer == 0 && a.offset == 0 && f(a) && isFloat(f(a)->ch)) return &a;
        for (auto& a : attrs) if (f(a) && isFloat(f(a)->ch) && f(a)->n >= 3) return &a;
    }
    if (name == "_u0" && allUnnamed)
        for (auto& a : attrs) { auto x = f(a); if (x && x->n == 2 && (isFloat(x->ch) || x->norm != 0) && !(a.buffer == 0 && a.offset == 0)) return &a; }
    if (name == "_i0" && allUnnamed)
        for (auto& a : attrs) { auto x = f(a); if (x && (x->ch == 'B' || x->ch == 'H' || x->ch == 'I') && x->norm == 0) return &a; }
    return nullptr;
}

std::vector<std::vector<std::string>> materialTextures(const R& r, size_t mdl) {
    auto [mo, mats] = findPtr(r, mdl, 0x60, "FMAT");
    std::vector<std::vector<std::string>> out;
    if (!mats) return out;
    int count = r.dictCount(mo + 8);
    if (!count) return out;
    size_t stride;
    try { stride = arrayStride(r, mats, count, "FMAT", 0xB0); } catch (...) { return out; }
    auto okName = [](const std::string& s) {
        for (unsigned char c : s) if (!(std::isalnum(c) || c == '_' || c == '-' || c >= 0x80)) return false;
        return true;
    };
    for (int i = 0; i < count; i++) {
        size_t m = mats + size_t(i) * stride;
        std::vector<std::string> names;
        size_t arr = r.ptr(m + 0x20);
        int num = m + 0xA3 < r.n ? r.u8(m + 0xA3) : 0;
        bool any = false;
        if (arr && num > 0 && num <= 32)
            for (int k = 0; k < num; k++) { names.push_back(r.str(arr + 8 * size_t(k))); any |= !names.back().empty(); }
        if (!any) {
            names.clear();
            for (size_t o = m + 0x10; o < m + 0xA0; o += 8) {
                size_t a = r.ptr(o);
                std::string s;
                if (a && r.str(a, s) && okName(s)) {
                    while (names.size() < 16) {
                        std::string t;
                        if (!r.str(a + 8 * names.size(), t)) break;
                        names.push_back(t);
                    }
                    break;
                }
            }
        }
        out.push_back(names);
    }
    return out;
}
}  // namespace

std::string albedo(const std::vector<std::string>& names) {
    static const char* NOT[] = {"_nrm", "_spm", "_thc", "_emm", "_mtl", "_rgh", "_ao", "_msk", "_mask", "_hgt", "_ems", "_opa",
                                "_trm", "_nrm2", "_cmb", "_dpt", "_flw"};
    for (auto& n : names) {
        if (n.empty()) continue;
        std::string l = lower(n);
        if (endsWith(l, "_alb") || l.find("_alb_") != std::string::npos || endsWith(l, "_albedo")) return n;
    }
    for (auto& n : names) {
        if (n.empty()) continue;
        std::string l = lower(n);
        bool bad = endsWith(l, "cmb") || endsWith(l, "_fx") || endsWith(l, "mask");
        for (auto k : NOT) if (endsWith(l, k) || l.find(std::string(k) + "_") != std::string::npos) bad = true;
        if (!bad) return n;
    }
    return "";
}

Mesh bfresRead(const Bytes& data, const std::string& prefer, size_t maxTriangles) {
    R r(data);
    if (!has(data, 0, "FRES") || data.size() < 0xF0) throw Error("pas un fichier BFRES");
    if (!(data[0x0C] == 0xFF && data[0x0D] == 0xFE)) throw Error("BFRES gros-boutiste (Wii U) : non pris en charge");
    uint32_t version = r.u32(8) >> 16;
    if (version < 9) throw Error(fmt("BFRES version %u : seules les versions 9 et 10 sont prises en charge", version));
    uint8_t flags = r.u8(0xEE);
    uint32_t fileSize = r.u32(0x1C);
    Mesh m;
    size_t bufferOffset;
    if (flags & (1 << 3)) {
        if (data.size() <= fileSize) throw Error(fmt("données graphiques dans un fichier séparé (drapeau %#x)", flags));
        bufferOffset = fileSize + 288;
    } else {
        size_t bi = r.ptr(0xB0);
        if (!bi) throw Error("pas de tampon de géométrie (BufferInfo)");
        uint64_t bo = r.u64(bi + 8);
        if (!(bo > 0 && bo < data.size())) throw Error("BufferInfo hors du fichier");
        bufferOffset = size_t(bo);
    }
    size_t modelsP = r.ptr(0x28);
    int nModels = r.dictCount(0x30);
    if (!nModels) nModels = r.u16(0xDC);
    if (!modelsP || !r.magic(modelsP, "FMDL") || !nModels) throw Error("aucun modèle (FMDL) dans le fichier");
    size_t stride = arrayStride(r, modelsP, nModels, "FMDL", 0x78);
    size_t mdl = modelsP;
    for (int i = 0; i < nModels; i++) {
        size_t o = modelsP + size_t(i) * stride;
        if (!prefer.empty() && r.str(o + 8) == prefer) mdl = o;
    }
    m.name = r.str(mdl + 8);
    auto [_, skel] = findPtr(r, mdl, 0x60, "FSKL");
    auto [so, shapesP] = findPtr(r, mdl, 0x60, "FSHP");
    if (!shapesP) throw Error("le modèle n'a pas de formes (FSHP)");
    int nShapes = r.dictCount(so + 8);
    if (!nShapes) throw Error("nombre de formes illisible");
    size_t sStride = arrayStride(r, shapesP, nShapes, "FSHP", 0x68);
    std::vector<M34> world;
    std::vector<uint16_t> table;
    if (skel) bones(r, skel, world, table);
    auto materials = materialTextures(r, mdl);
    size_t vtxArr = r.ptr(mdl + 0x20), vtxStride = 0x60;
    if (vtxArr && r.magic(vtxArr, "FVTX")) {
        for (size_t o = vtxArr + 4; o + 4 <= std::min(data.size(), vtxArr + 0x200); o++)
            if (std::memcmp(data.data() + o, "FVTX", 4) == 0) { if ((o - vtxArr) % 8 == 0) vtxStride = o - vtxArr; break; }
    } else {
        vtxArr = 0;
    }
    size_t tris = 0;
    for (int si = 0; si < nShapes; si++) {
        size_t sh = shapesP + size_t(si) * sStride;
        try {
            auto [vo, vb] = findPtr(r, sh, 0x40, "FVTX");
            if (!vb) {
                vo = sh + 0x10;
                uint16_t vbi = r.u16(vo + 0x40 + 6);
                size_t cand = vtxArr ? vtxArr + vbi * vtxStride : 0;
                if (!(cand && r.magic(cand, "FVTX"))) throw Error("forme sans FVTX");
                vb = cand;
            }
            size_t meshP = r.ptr(vo + 8);
            size_t tail = vo + 0x40;
            int idx = r.u16(tail), bone = r.u16(tail + 4), skin = r.u8(tail + 10), nMesh = r.u8(tail + 11);
            int matIdx = r.u16(tail + 2);
            if (idx != si || nMesh <= 0 || nMesh > 16 || skin > 8 || !meshP)
                throw Error(fmt("en-tête de forme incohérent (n°%d, %d maillages, %d influences)", idx, nMesh, skin));
            std::vector<Attr> attrs;
            std::vector<Buf> bufs;
            size_t vcount;
            vertexBuffer(r, vb, bufferOffset, attrs, bufs, vcount);
            const Attr* pa = pickAttr(attrs, "_p0");
            if (!pa) throw Error("pas d'attribut de position");
            if (pa->buffer >= bufs.size()) throw Error("tampon de position absent");
            auto pts = attrValues(r, bufs[pa->buffer], *pa, vcount);
            const Attr* ua = pickAttr(attrs, "_u0");
            std::vector<std::array<double, 4>> uvs;
            if (ua && ua != pa && ua->buffer < bufs.size()) {
                try { uvs = attrValues(r, bufs[ua->buffer], *ua, vcount); } catch (...) { uvs.clear(); }
            }
            std::vector<const M34*> mats;
            if (!world.empty()) {
                if (skin == 0 && size_t(bone) < world.size()) mats.assign(vcount, &world[size_t(bone)]);
                else if (skin == 1) {
                    const Attr* ia = pickAttr(attrs, "_i0");
                    if (ia && ia->buffer < bufs.size()) {
                        auto ids = attrValues(r, bufs[ia->buffer], *ia, vcount);
                        mats.resize(vcount);
                        for (size_t k = 0; k < vcount; k++) {
                            size_t v0 = size_t(ids[k][0]);
                            mats[k] = v0 < table.size() && table[v0] < world.size() ? &world[table[v0]] : &IDENT;
                        }
                    }
                }
            }
            size_t mb = meshP;
            uint32_t faceOff = r.u32(mb + 0x20), prim = r.u32(mb + 0x24), ifmt = r.u32(mb + 0x28), icount = r.u32(mb + 0x2C), first = r.u32(mb + 0x30);
            if ((prim != 3 && prim != 4) || ifmt > 2 || icount == 0 || icount >= 50000000)
                throw Error(fmt("maillage non pris en charge (primitive %u, index %u)", prim, ifmt));
            size_t isize = ifmt == 0 ? 1 : ifmt == 1 ? 2 : 4;
            size_t start = bufferOffset + faceOff;
            if (start + icount * isize > data.size()) throw Error("index hors du fichier");
            std::vector<uint32_t> ids(icount);
            for (size_t k = 0; k < icount; k++)
                ids[k] = isize == 1 ? data[start + k] : isize == 2 ? rd16(data, start + 2 * k) : rd32(data, start + 4 * k);
            if (prim == 4) {
                std::vector<uint32_t> tri;
                for (size_t k = 0; k + 2 < ids.size(); k++) {
                    uint32_t a = ids[k], b = ids[k + 1], c = ids[k + 2];
                    if (a != b && b != c && a != c) {
                        if (k % 2 == 0) { tri.push_back(a); tri.push_back(b); tri.push_back(c); }
                        else { tri.push_back(b); tri.push_back(a); tri.push_back(c); }
                    }
                }
                ids.swap(tri);
            }
            if (tris + ids.size() / 3 > maxTriangles) { m.skipped.push_back(fmt("forme %d : trop de triangles", si)); continue; }
            for (uint32_t v : ids) if (v + first >= vcount) throw Error("index de sommet hors limites");
            uint32_t base = uint32_t(m.positions.size() / 3), ibase = uint32_t(m.indices.size());
            for (size_t k = 0; k < vcount; k++) {
                double x = pts[k][0], y = pts[k][1], z = pts[k][2];
                if (!mats.empty()) {
                    const M34& M = *mats[k];
                    double nx = M[0] * x + M[1] * y + M[2] * z + M[3], ny = M[4] * x + M[5] * y + M[6] * z + M[7], nz = M[8] * x + M[9] * y + M[10] * z + M[11];
                    x = nx; y = ny; z = nz;
                }
                m.positions.push_back(float(x)); m.positions.push_back(float(y)); m.positions.push_back(float(z));
                m.uvs.push_back(uvs.empty() ? 0.f : float(uvs[k][0]));
                m.uvs.push_back(uvs.empty() ? 0.f : float(uvs[k][1]));
            }
            for (uint32_t v : ids) m.indices.push_back(base + v + first);
            std::string tex = !uvs.empty() && size_t(matIdx) < materials.size() ? albedo(materials[size_t(matIdx)]) : "";
            m.subs.push_back({ibase, uint32_t(m.indices.size()) - ibase, tex});
            tris += ids.size() / 3;
            m.shapes++;
        } catch (const std::exception& e) {
            m.skipped.push_back(fmt("forme %d : ", si) + e.what());
        }
    }
    m.indices.resize(m.indices.size() - m.indices.size() % 3);
    if (!m.shapes) {
        std::string why;
        for (size_t k = 0; k < m.skipped.size() && k < 3; k++) why += (k ? "; " : "") + m.skipped[k];
        throw Error("aucune forme lisible : " + why);
    }
    float lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY};
    for (size_t k = 0; k < m.positions.size(); k += 3)
        for (int c = 0; c < 3; c++) { lo[c] = std::min(lo[c], m.positions[k + size_t(c)]); hi[c] = std::max(hi[c], m.positions[k + size_t(c)]); }
    for (int c = 0; c < 3; c++) { m.bbox[c] = lo[c]; m.bbox[3 + c] = hi[c]; }
    return m;
}

}  // namespace te
