// Feuillage posé par la scène (Foliage/Placement/Scene/<dossier>/<scène>.<couche>.flnt.zs) : plantes et branches
// basses (couches « Ornament », « LowTree »…). Un fichier d'instances commence par le nombre de sortes et
// d'instances, puis une fiche de 0x100 octets par sorte (chemin de sa ressource Work/Foliage/ResShoot…/<modèle>…,
// précédé de sa longueur), puis 64 octets par instance : position, échelle, deux axes (X puis Y, le haut), numéro
// de sorte. Les couches de couverture du sol (Moss, Turf, Stone, Undergrowth…) sont des grilles de cases générées
// par le moteur : pas encore lues.
#pragma once
#include <string>
#include <vector>

#include "core/game.hpp"

namespace te {

struct FoliageInst { float p[3], x[3], y[3], s; };
struct FoliageType { std::string model, res, layer; std::vector<FoliageInst> inst; };
struct FoliageSet {
    std::vector<FoliageType> types;
    std::vector<std::string> groundCover;   // (fichiers de couverture du sol pas encore lus)
    size_t count = 0;
};

FoliageSet foliageParse(const Bytes& flnt, const std::string& layer);   // (Error si ce n'est pas un fichier d'instances)
FoliageSet foliageLoad(Game& g, const std::string& scenePrefix, std::string* status);

}  // namespace te
