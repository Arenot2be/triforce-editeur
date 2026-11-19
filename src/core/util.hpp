// Outils communs : octets, fichiers (chemins UTF-8, aussi sous Windows), texte, CRC-32.
#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace te {

using Bytes = std::vector<uint8_t>;
namespace fs = std::filesystem;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// -- lecture petit-boutiste (bornée : hors limites -> Error)
inline void need(const Bytes& b, size_t o, size_t n) {
    if (o > b.size() || n > b.size() - o) throw Error("lecture hors du fichier");
}
inline uint8_t rd8(const Bytes& b, size_t o) { need(b, o, 1); return b[o]; }
inline uint16_t rd16(const Bytes& b, size_t o) { need(b, o, 2); return uint16_t(b[o] | b[o + 1] << 8); }
inline uint32_t rd32(const Bytes& b, size_t o) {
    need(b, o, 4);
    return uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16 | uint32_t(b[o + 3]) << 24;
}
inline uint64_t rd64(const Bytes& b, size_t o) { return uint64_t(rd32(b, o)) | uint64_t(rd32(b, o + 4)) << 32; }
inline float rdf(const Bytes& b, size_t o) { uint32_t u = rd32(b, o); float f; std::memcpy(&f, &u, 4); return f; }
inline uint16_t rd16be(const Bytes& b, size_t o) { need(b, o, 2); return uint16_t(b[o] << 8 | b[o + 1]); }
inline uint32_t rd32be(const Bytes& b, size_t o) {
    need(b, o, 4);
    return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | uint32_t(b[o + 3]);
}
inline bool has(const Bytes& b, size_t o, const char* magic, size_t n = 4) {
    return o <= b.size() && n <= b.size() - o && std::memcmp(b.data() + o, magic, n) == 0;
}

// -- écriture
struct Writer {
    Bytes out;
    bool big = false;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { if (big) { u8(v >> 8); u8(v & 255); } else { u8(v & 255); u8(v >> 8); } }
    void u24(uint32_t v) { if (big) { u8(v >> 16); u8(v >> 8); u8(v); } else { u8(v); u8(v >> 8); u8(v >> 16); } }
    void u32(uint32_t v) { if (big) { u16(v >> 16); u16(v & 0xFFFF); } else { u16(v & 0xFFFF); u16(v >> 16); } }
    void u64(uint64_t v) { if (big) { u32(uint32_t(v >> 32)); u32(uint32_t(v)); } else { u32(uint32_t(v)); u32(uint32_t(v >> 32)); } }
    void f32(float f) { uint32_t u; std::memcpy(&u, &f, 4); u32(u); }
    void put(const void* p, size_t n) { auto c = static_cast<const uint8_t*>(p); out.insert(out.end(), c, c + n); }
    void put(std::string_view s) { put(s.data(), s.size()); }
    void align(size_t n) { while (out.size() % n) out.push_back(0); }
    void set32(size_t at, uint32_t v) {
        for (int k = 0; k < 4; k++) out[at + k] = big ? uint8_t(v >> (24 - 8 * k)) : uint8_t(v >> (8 * k));
    }
    size_t size() const { return out.size(); }
};

// -- fichiers (texte des chemins en UTF-8)
fs::path u8path(std::string_view s);
std::string pathStr(const fs::path& p);
Bytes readFile(const fs::path& p);
Bytes readFileHead(const fs::path& p, size_t n);   // (les n premiers octets au plus)
std::string readText(const fs::path& p);
void writeFileAtomic(const fs::path& p, const void* data, size_t n);   // (dossiers créés ; écrit puis renomme)
inline void writeFileAtomic(const fs::path& p, const Bytes& b) { writeFileAtomic(p, b.data(), b.size()); }
inline void writeFileAtomic(const fs::path& p, const std::string& s) { writeFileAtomic(p, s.data(), s.size()); }
fs::path homeDir();
fs::path exeDir();

// -- texte
std::string lower(std::string s);
bool endsWith(std::string_view s, std::string_view suffix);
bool startsWith(std::string_view s, std::string_view prefix);
std::string replaceAll(std::string s, std::string_view from, std::string_view to);
std::vector<std::string> split(std::string_view s, char sep);
std::string hex(const uint8_t* p, size_t n);
std::string fmt(const char* f, ...);

uint32_t crc32(const void* data, size_t n);

}  // namespace te
