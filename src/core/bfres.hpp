// Géométrie des modèles BFRES Switch (versions 9 et 10), comme triforce_modkit/bfres.py.
#pragma once
#include <string>
#include <vector>

#include "core/util.hpp"

namespace te {

struct Mesh {
    struct Sub { uint32_t start = 0, count = 0; std::string tex; };
    std::vector<float> positions, uvs;   // (x, y, z) ; (u, v)
    std::vector<uint32_t> indices;
    std::vector<Sub> subs;
    float bbox[6] = {0, 0, 0, 0, 0, 0};
    std::string name;
    std::vector<std::string> skipped;
    int shapes = 0;
};

Mesh bfresRead(const Bytes& data, const std::string& prefer = "", size_t maxTriangles = 2000000);
std::string albedo(const std::vector<std::string>& names);

}  // namespace te
