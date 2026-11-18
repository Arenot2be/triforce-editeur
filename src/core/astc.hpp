// Décodeur ASTC (2D) : port de triforce_modkit/astc.js (vérifié pixel par pixel contre astcenc d'ARM).
#pragma once
#include <map>
#include <string>

#include "core/util.hpp"

namespace te {

struct AstcStats {
    int n = 0, hdr = 0, empty = 0, patched = 0;
    std::map<std::string, int> why;
};

// RGBA 8 bits ; hdr : profil HDR (ramené entre 0 et 1) ; patch : blocs vides ou invalides à la couleur moyenne
Bytes astcDecode(const uint8_t* blocks, size_t size, int w, int h, int bw, int bh, bool srgb, bool hdr, bool patch, AstcStats* st = nullptr);

}  // namespace te
