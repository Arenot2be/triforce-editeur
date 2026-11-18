// La table des tailles de ressources (System/Resource/ResourceSizeTable.*.rsizetable.zs), comme restbl.py.
#pragma once
#include <map>
#include <optional>

#include "core/util.hpp"

namespace te {

struct Restbl {
    uint32_t version = 1, nameLen = 160;
    std::map<uint32_t, uint32_t> hashed;
    std::map<std::string, uint32_t> named;

    static Restbl load(const Bytes& b);
    Bytes dump() const;
    static std::string key(std::string path);
    std::optional<uint32_t> get(const std::string& path) const;
    void set(const std::string& path, uint32_t size);
    // (taille de path portée à newSize : renvoie (ancienne, nouvelle) si elle change)
    std::optional<std::pair<std::optional<uint32_t>, uint32_t>> fit(const std::string& path, size_t newSize, std::optional<size_t> oldSize);
};

}  // namespace te
