#include "core/sarc.hpp"

#include <algorithm>

namespace te {

uint32_t sarcHash(const std::string& name, uint32_t key) {
    uint32_t h = 0;
    for (char c : name) h = h * key + uint32_t(int32_t(int8_t(c)));   // (char signé, comme le jeu)
    return h;
}

static uint32_t alignOf(size_t pos) {
    uint32_t a = 1;
    while (a < 0x2000 && pos % (size_t(a) * 2) == 0) a *= 2;
    return std::max<uint32_t>(a, 4);
}

Sarc Sarc::load(const Bytes& b) {
    if (!has(b, 0, "SARC")) throw Error("pas une archive SARC");
    Sarc s;
    s.big = rd16(b, 6) != 0xFEFF;
    auto u16 = [&](size_t o) { return s.big ? rd16be(b, o) : rd16(b, o); };
    auto u32 = [&](size_t o) { return s.big ? rd32be(b, o) : rd32(b, o); };
    size_t hl = u16(4);
    size_t data = u32(12);
    if (!has(b, hl, "SFAT")) throw Error("table SFAT absente");
    size_t n = u16(hl + 6);
    s.key = u32(hl + 8);
    size_t nodes = hl + 12, names = nodes + 16 * n;
    if (!has(b, names, "SFNT")) throw Error("table SFNT absente");
    names += 8;
    auto nameAt = [&](size_t off, std::string& out) {
        size_t o = names + off * 4;
        if (o < names || o >= data || o >= b.size()) return false;
        auto e = std::find(b.begin() + long(o), b.begin() + long(std::min(data, b.size())), 0);
        if (e == b.end()) return false;
        out.assign(b.begin() + long(o), e);
        return true;
    };
    for (size_t i = 0; i < n; i++) {
        uint32_t h = u32(nodes + 16 * i), attr = u32(nodes + 16 * i + 4);
        uint32_t start = u32(nodes + 16 * i + 8), end = u32(nodes + 16 * i + 12);
        std::string name;
        if (attr & 0x01000000) {
            bool ok = false;
            for (uint32_t off : {attr & 0xFFFFFFu, attr & 0xFFFFu}) {
                std::string c;
                if (nameAt(off, c) && sarcHash(c, s.key) == h) { name = c; ok = true; break; }
            }
            if (!ok) {
                if (!nameAt(attr & 0xFFFFFF, name)) name = fmt("0x%08x", h);
                s.badNames.push_back(name);
            }
        } else {
            name = fmt("0x%08x", h);
        }
        if (data + end > b.size() || start > end) throw Error("membre hors de l'archive : " + name);
        Bytes f(b.begin() + long(data + start), b.begin() + long(data + end));
        s.aligns[name] = alignOf(data + start);
        s.set(name, std::move(f));
    }
    return s;
}

Bytes Sarc::dump() const {
    std::vector<const std::pair<std::string, Bytes>*> entries;
    for (auto& f : files) entries.push_back(&f);
    std::stable_sort(entries.begin(), entries.end(),
                     [&](auto a, auto b) { return sarcHash(a->first, key) < sarcHash(b->first, key); });
    Bytes names;
    std::vector<size_t> nameOff;
    for (auto e : entries) {
        nameOff.push_back(names.size() / 4);
        names.insert(names.end(), e->first.begin(), e->first.end());
        names.push_back(0);
        while (names.size() % 4) names.push_back(0);
    }
    size_t headLen = 0x14 + 12 + 16 * entries.size() + 8 + names.size();
    std::vector<uint32_t> al;
    uint32_t first = 4;
    for (auto e : entries) {
        auto it = aligns.find(e->first);
        al.push_back(it != aligns.end() ? it->second : 8);
        first = std::max(first, al.back());
    }
    size_t dataAt = (headLen + first - 1) / first * first;
    Bytes dat;
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    for (size_t i = 0; i < entries.size(); i++) {
        while ((dataAt + dat.size()) % al[i]) dat.push_back(0);
        uint32_t st = uint32_t(dat.size());
        dat.insert(dat.end(), entries[i]->second.begin(), entries[i]->second.end());
        spans.emplace_back(st, uint32_t(dat.size()));
    }
    Writer w;
    w.big = big;
    w.put("SARC");
    w.u16(0x14); w.u16(0xFEFF); w.u32(0); w.u32(uint32_t(dataAt)); w.u16(0x100); w.u16(0);
    w.put("SFAT");
    w.u16(12); w.u16(uint16_t(entries.size())); w.u32(key);
    std::map<uint32_t, int> count, rank;
    std::vector<uint32_t> hashes;
    for (auto e : entries) { hashes.push_back(sarcHash(e->first, key)); count[hashes.back()]++; }
    for (size_t i = 0; i < entries.size(); i++) {
        if (nameOff[i] > 0xFFFFFF) throw Error("table des noms trop grande");
        uint32_t attr = 0x01000000u | uint32_t(nameOff[i]);
        if (count[hashes[i]] > 1 && nameOff[i] <= 0xFFFF) attr |= uint32_t(++rank[hashes[i]]) << 16;
        w.u32(hashes[i]); w.u32(attr); w.u32(spans[i].first); w.u32(spans[i].second);
    }
    w.put("SFNT");
    w.u16(8); w.u16(0);
    w.put(names.data(), names.size());
    while (w.size() < dataAt) w.u8(0);
    w.put(dat.data(), dat.size());
    w.set32(8, uint32_t(w.size()));
    return w.out;
}

}  // namespace te
