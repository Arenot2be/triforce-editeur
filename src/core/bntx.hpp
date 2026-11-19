// Textures BNTX (Tex/*.bntx.zs) : un niveau de mip remis dans l'ordre linéaire, comme triforce_modkit/bntx.py.
#pragma once
#include <string>

#include "core/util.hpp"

namespace te {

// codes : 1 RGBA8, 2 BC1, 3 BC2, 4 BC3, 5 BC4, 6 BC5, 7 BC7, 8 BC6H, 20+i ASTC (ASTC_DIMS[i])
extern const int ASTC_DIMS[14][2];

struct TexData {
    int code = 0, w = 0, h = 0;
    Bytes blocks;
    std::string format;
    bool srgb = false;
    uint32_t compSel = 0x05040302;   // (rouge, vert, bleu, alpha : 0 zéro, 1 un, 2..5 canal R G B A)
};

TexData bntxExtract(const Bytes& data, int maxSize = 512, const std::string& want = "");
std::vector<std::string> bntxNames(const Bytes& data);   // (noms des textures ; "" si hors des données)
std::string bntxDiagnose(const Bytes& data, const std::string& want = "");

size_t blockLinear(size_t xBytes, size_t y, size_t wgobs, size_t bh);   // (adresse dans le rangement de la Switch)

}  // namespace te
