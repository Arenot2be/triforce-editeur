// Compression zstd du jeu : ses dictionnaires (Pack/ZsDic.pack.zs), choisis par l'identifiant que porte chaque fichier.
#pragma once
#include <map>
#include <memory>
#include <mutex>

#include "core/util.hpp"

namespace te {

class Zs {
public:
    Zs() = default;
    explicit Zs(const fs::path& romfs);
    ~Zs();
    Zs(const Zs&) = delete;
    Zs& operator=(const Zs&) = delete;

    static bool isZstd(const Bytes& b) { return b.size() >= 4 && b[0] == 0x28 && b[1] == 0xB5 && b[2] == 0x2F && b[3] == 0xFD; }
    static uint32_t dictId(const Bytes& raw);
    Bytes unpack(const Bytes& raw) const;               // (sans compression : rendu tel quel)
    static Bytes unpackPlain(const uint8_t* p, size_t n); // (sans dictionnaire)
    Bytes pack(const Bytes& data, uint32_t dictId, int level = 10) const;
    uint32_t dictForName(const std::string& name) const;

private:
    struct Dict;
    std::map<uint32_t, std::shared_ptr<Dict>> byId_;
    std::map<std::string, std::shared_ptr<Dict>> byName_;
};

}  // namespace te
