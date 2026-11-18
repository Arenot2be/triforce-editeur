// Archives SARC (.pack, .sarc…) : lecture et écriture, comme triforce_modkit/sarc.py.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/util.hpp"

namespace te {

struct Sarc {
    std::vector<std::pair<std::string, Bytes>> files;   // (dans l'ordre d'origine)
    std::map<std::string, size_t> index;
    std::map<std::string, uint32_t> aligns;
    uint32_t key = 0x65;
    bool big = false;
    std::vector<std::string> badNames;

    static Sarc load(const Bytes& b);
    Bytes dump() const;
    const Bytes* get(const std::string& name) const {
        auto it = index.find(name);
        return it == index.end() ? nullptr : &files[it->second].second;
    }
    void set(const std::string& name, Bytes data) {
        auto it = index.find(name);
        if (it == index.end()) { index[name] = files.size(); files.emplace_back(name, std::move(data)); }
        else files[it->second].second = std::move(data);
    }
};

uint32_t sarcHash(const std::string& name, uint32_t key = 0x65);

}  // namespace te
