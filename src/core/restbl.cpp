#include "core/restbl.hpp"

#include <algorithm>

namespace te {

Restbl Restbl::load(const Bytes& b) {
    if (!has(b, 0, "RESTBL", 6)) throw Error("pas une table RESTBL");
    Restbl r;
    r.version = rd32(b, 6);
    r.nameLen = rd32(b, 10);
    uint32_t nh = rd32(b, 14), nn = rd32(b, 18);
    size_t at = 22;
    for (uint32_t i = 0; i < nh; i++) r.hashed[rd32(b, at + 8 * i)] = rd32(b, at + 8 * i + 4);
    at += 8 * size_t(nh);
    for (uint32_t i = 0; i < nn; i++) {
        size_t o = at + (r.nameLen + 4) * size_t(i);
        need(b, o, r.nameLen + 4);
        auto e = std::find(b.begin() + long(o), b.begin() + long(o + r.nameLen), 0);
        r.named[std::string(b.begin() + long(o), e)] = rd32(b, o + r.nameLen);
    }
    return r;
}

Bytes Restbl::dump() const {
    Writer w;
    w.put("RESTBL");
    w.u32(version); w.u32(nameLen); w.u32(uint32_t(hashed.size())); w.u32(uint32_t(named.size()));
    for (auto& [h, s] : hashed) { w.u32(h); w.u32(s); }
    for (auto& [n, s] : named) {
        std::string raw = n.substr(0, nameLen - 1);
        w.put(raw);
        for (size_t k = raw.size(); k < nameLen; k++) w.u8(0);
        w.u32(s);
    }
    return w.out;
}

std::string Restbl::key(std::string path) {
    path = replaceAll(path, "\\", "/");
    if (endsWith(path, ".zs")) path.resize(path.size() - 3);
    return path;
}

std::optional<uint32_t> Restbl::get(const std::string& path) const {
    std::string k = key(path);
    if (auto it = named.find(k); it != named.end()) return it->second;
    if (auto it = hashed.find(crc32(k.data(), k.size())); it != hashed.end()) return it->second;
    return std::nullopt;
}

void Restbl::set(const std::string& path, uint32_t size) {
    std::string k = key(path);
    if (auto it = named.find(k); it != named.end()) it->second = size;
    else hashed[crc32(k.data(), k.size())] = size;
}

std::optional<std::pair<std::optional<uint32_t>, uint32_t>> Restbl::fit(const std::string& path, size_t newSize, std::optional<size_t> oldSize) {
    auto have = get(path);
    size_t want;
    if (!have) want = (newSize + 31) / 32 * 32 + std::max<size_t>(4096, newSize / 4);
    else if (oldSize) want = newSize + (*have > *oldSize ? *have - *oldSize : 0);
    else want = newSize + 4096;
    if (have && want <= *have) return std::nullopt;
    set(path, uint32_t(want));
    return std::make_pair(have, uint32_t(want));
}

}  // namespace te
