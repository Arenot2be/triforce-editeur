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

// Couverture du sol (Moss, Turf, Stone, Undergrowth…) : le moteur y fait pousser mousse, gazon, cailloux et
// sous-bois. Fichier : nombre de cases, puis une case par u32 (octets x, y, z de la case de 4 m ; monde =
// (x-128)·4, (y-127)·4, (z-128)·4), puis le début de chaque case dans la liste des points (n+1 u32), puis 12 octets
// par point : position dans la case (3 octets, 1/64 m), deux octets de réglage (taille…), sorte, orientation de
// la surface (octet 6 : direction autour de la verticale ; octet 7 : 255 = surface horizontale), autres réglages.
// Vérifié sur la forêt Kokiri : les plantes posées tombent à quelques centimètres de la mousse et du sous-bois.
struct GroundPt { float p[3]; float n[3]; uint8_t a3, a4, kind; };
struct GroundCell { float c[3]; uint32_t begin, end; };
struct GroundLayer { std::string name; std::vector<GroundCell> cells; std::vector<GroundPt> pts; };
GroundLayer groundParse(const Bytes& flnt, const std::string& name);   // (Error si ce n'en est pas une)
std::vector<GroundLayer> groundLoad(Game& g, const std::string& scenePrefix, std::string* status);

// Lumières de la scène (Bake/Light/<scène>.bklight.zs, « BKLT ») : nombre de lumières ponctuelles, de projecteurs et
// de lumières rectangulaires, puis leurs fiches (28, 52 et 60 octets) : position, direction, couleur × intensité,
// angles ou taille.
struct SceneLight {
    enum Kind { Point, Spot, Rect } kind = Point;
    float p[3] = {0, 0, 0}, dir[3] = {0, -1, 0}, up[3] = {0, 1, 0}, col[3] = {1, 1, 1};
    float intensity = 0, a = 0, b = 0;   // (projecteur : angles ; rectangle : largeur, hauteur ; ponctuelle : rayon de la source)
};
std::vector<SceneLight> lightsParse(const Bytes& bklight);
std::vector<SceneLight> lightsLoad(Game& g, const std::string& scenePrefix, std::string* status);

FoliageSet foliageParse(const Bytes& flnt, const std::string& layer);   // (Error si ce n'est pas un fichier d'instances)
FoliageSet foliageLoad(Game& g, const std::string& scenePrefix, std::string* status);

}  // namespace te
