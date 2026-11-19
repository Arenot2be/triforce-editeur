// Géométrie des modèles BFRES Switch (versions 9 et 10), comme triforce_modkit/bfres.py.
#pragma once
#include <string>
#include <vector>

#include "core/util.hpp"

namespace te {

struct Mesh {
    // kind (parties sans texture de couleur) : 0 opaque, 1 eau, 2 fondu ou lumière, 3 verre ou glace
    struct Sub { uint32_t start = 0, count = 0; std::string tex; int kind = 0; std::string material; };
    std::vector<float> positions, uvs;   // (x, y, z) ; (u, v)
    std::vector<uint32_t> indices;
    std::vector<Sub> subs;
    float bbox[6] = {0, 0, 0, 0, 0, 0};
    std::string name;
    std::vector<std::string> skipped;
    int shapes = 0;
    int meshletShapes = 0;
    std::vector<std::string> notes;   // (formes lues incomplètement : sans coordonnées de texture…)   // (dont en meshlets : sans coordonnées de texture)
};

extern const char* const MESHLETS;       // (raison des formes en meshlets)
extern const char* const MESHLETS_ALL;
Mesh bfresRead(const Bytes& data, const std::string& prefer = "", size_t maxTriangles = 2000000);
int surfaceKind(const std::string& material, const std::vector<std::string>& textures);   // (voir Mesh::Sub::kind)
std::string albedo(const std::vector<std::string>& names);

}  // namespace te
