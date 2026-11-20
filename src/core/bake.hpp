// Éclairage précalculé des scènes (Bake/Scene/<scène>_G_x_y.bkres.zs) : pour chaque objet posé (par son Hash dans la
// carte), la texture d'éclairage de sa case de grille et, par matériau, où sa coordonnée « bake » tombe dans cette texture.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace te {

class Game;

struct BakeMat { std::string material; int index = 0; float off[2] = {0, 0}, scale[2] = {0, 0}; };
struct BakeEntry { std::string tex, model; std::vector<BakeMat> mats; };

// (prefix : nom de la scène, ex. KokiriForest_Child ; status : compte rendu)
std::map<uint64_t, BakeEntry> bakeLoad(Game& g, const std::string& prefix, std::string* status = nullptr);
// (nom de scène tiré du chemin d'une carte : Banc/KokiriForest/KokiriForest_Child_Dynamic.bcett.byml.zs -> KokiriForest_Child)
std::string bakePrefix(const std::string& mapPath);

}  // namespace te
