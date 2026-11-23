#include "core/matshader.hpp"

#include <cstdio>
#include <cstring>
#include <mutex>

namespace te {
namespace {
std::mutex mu;
std::shared_ptr<const ShaderNames> names_;
uint64_t r64(const Bytes& d, size_t o) { uint64_t v = 0; if (o + 8 <= d.size()) std::memcpy(&v, d.data() + o, 8); return v; }
uint32_t r32(const Bytes& d, size_t o) { uint32_t v = 0; if (o + 4 <= d.size()) std::memcpy(&v, d.data() + o, 4); return v; }
std::string rstr(const Bytes& d, size_t o) {
    if (o + 2 > d.size()) return "";
    uint16_t n = uint16_t(d[o] | d[o + 1] << 8);
    if (o + 2 + n > d.size()) return "";
    return std::string(reinterpret_cast<const char*>(d.data() + o + 2), n);
}
}  // namespace

std::shared_ptr<const ShaderNames> ebsParse(const Bytes& d) {
    auto out = std::make_shared<ShaderNames>();
    if (d.size() < 0x100 || std::memcmp(d.data(), "FRES", 4) != 0) return out;
    size_t hashes = size_t(r64(d, 0xB8)), dict = size_t(r64(d, 0xC0));
    if (!hashes || !dict || dict + 8 > d.size()) return out;
    uint32_t n = r32(d, dict + 4);
    if (n > 100000 || hashes + size_t(n) * 8 > d.size()) return out;
    for (uint32_t k = 0; k < n; k++) {
        size_t e = dict + 8 + 16 * size_t(k + 1);
        (*out)[r64(d, hashes + 8 * size_t(k))] = rstr(d, size_t(r64(d, e + 8)));
    }
    return out;
}
void setShaderNames(std::shared_ptr<const ShaderNames> n) { std::lock_guard l(mu); names_ = std::move(n); }
std::shared_ptr<const ShaderNames> shaderNames() { std::lock_guard l(mu); return names_; }
std::string shaderName(uint64_t h) {
    auto n = shaderNames();
    if (n) if (auto it = n->find(h); it != n->end()) return it->second;
    char b[24];
    std::snprintf(b, sizeof b, "#%016llx", (unsigned long long)h);
    return b;
}

int MatShader::optInt(const std::string& k, int d) const {
    auto it = opts.find(k);
    if (it == opts.end()) return d;
    try { return std::stoi(it->second); } catch (...) { return d; }
}
int MatShader::select(const std::string& what, int d) const { return optInt("ns_" + what + "_select", d); }

}  // namespace te
