// Feuillage posé par la scène (Foliage/Placement/Scene/<dossier>/<scène>.<couche>.flnt.zs) : plantes et branches
// basses (couches « Ornament », « LowTree »…). Un fichier d'instances commence par le nombre de sortes et
// d'instances, puis une fiche de 0x100 octets par sorte (chemin de sa ressource Work/Foliage/ResShoot…/<modèle>…,
// précédé de sa longueur), puis 64 octets par instance : position, échelle, deux axes (X puis Y, le haut), numéro
// de sorte. Les couches de couverture du sol (Moss, Turf, Stone, Undergrowth…) sont des grilles de cases générées
// par le moteur : pas encore lues.
#pragma once
#include <string>
#include <unordered_map>
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
// (d'après le nuanceur du jeu, Shader/Foliage.*.bfsha : octet 3 = normale du sol, un des 256 points d'une spirale de
// Fibonacci sur la sphère ; octet 4 = rayon de la touffe ; octet 5 = feuille ou sorte ; octet 6 = ouverture du cône des
// brins ; octet 8 = taille des brins de mousse (0 = 255))
struct GroundPt { float p[3]; float n[3]; uint8_t a3, a4, kind, a6, a8, a11; };
struct GroundCell { float c[3]; uint32_t begin, end; };
// Sorte de couverture, d'après Pack/Foliage.pack (ResMossList, ResStoneList, ResTurfList,
// ResShootLeafUndergrowthList) : couches de la texture en tableau (Foliage_<Moss|Stone|Turf|ShootLeaf>_Alb, rangées
// comme ResTexArray/<…>), teinte, taille. Le numéro de sorte d'un point (octet 5 ; octet 11 pour le gazon) y renvoie.
struct GroundKind {
    std::string res;               // (fiche du jeu, pour le bilan)
    std::vector<int> texLayers;    // (couches de la texture en tableau ; vide = inconnue)
    float color[3] = {1, 1, 1};    // (teinte multipliée à la texture : ColorScale…)
    float width = 1, height = 1;   // (tailles relatives : WidthRate/HeightRate des cailloux, ScaleMin..Max du sous-bois…)
    int leaves = 4;                // (sous-bois : feuilles par touffe)
};
struct GroundLayer {
    std::string name, texArray;    // (texArray : « Foliage_Moss_Alb »…)
    std::vector<GroundCell> cells;
    std::vector<GroundPt> pts;
    std::vector<GroundKind> kinds;
};
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

// Sondes de lumière (Bake/Probe/Scene/<scène>/) : meta.bkprmb (origine, taille des blocs), resident.bkprcb (niveau le
// plus grossier, 4³ cellules) et X*_Y*_Z*_<niveau>.bkprb (niveaux 0 à 2 : 32³, 16³, 8³ cellules de 48 octets).
// Après les cellules : u32 4, u32 nombre, 24 octets nuls, puis 72 octets par sonde : position (3 flottants), u32,
// 28 demi-flottants = trois jeux d'harmoniques sphériques d'ordre 1 (Y00, Y1y, Y1z, Y1x) : lumière A (RVB des
// groupes 0-3), visibilité du ciel (le 4e de ces groupes), lumière B (RVB, demi-flottants 16 à 27).
struct ProbeSet {
    std::vector<float> pos;   // (x, y, z par sonde)
    std::vector<float> sh;    // (28 par sonde)
    size_t size() const { return pos.size() / 3; }
    // (éclairement ambiant autour d'un point, sous forme linéaire a + M·n, rapporté à l'éclairement médian vers le
    //  haut : out = (a.r, a.g, a.b, M.b.x, M.r.x, M.r.y, M.r.z, M.b.y, M.g.x, M.g.y, M.g.z, M.b.z) ; false sans sonde)
    bool ambient(const float p[3], float out[12]) const;
    float sky[3] = {0.045f, 0.06f, 0.085f};   // (luminance du ciel supposée, en attendant Env/GameScene)
    float ref = 1;                            // (éclairement médian vers le haut)
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;   // (cases de 4 m)
    void index();
};
ProbeSet probesParse(const std::vector<Bytes>& files);
ProbeSet probesLoad(Game& g, const std::string& scenePrefix, std::string* status);

FoliageSet foliageParse(const Bytes& flnt, const std::string& layer);   // (Error si ce n'est pas un fichier d'instances)
FoliageSet foliageLoad(Game& g, const std::string& scenePrefix, std::string* status);

}  // namespace te
