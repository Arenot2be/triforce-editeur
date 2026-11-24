// Le terrain du jeu (TerrainArc : scènes .tscb, tuiles .hght / .mate), comme triforce_modkit/terrain.py.
#pragma once
#include <string>
#include <vector>

#include "core/game.hpp"

namespace te {

struct TerrainTile {
    float cx = 0, cz = 0, side = 0;
    int n = 0;
    std::vector<float> h;       // n x n hauteurs (mètres ; NaN = trou)
    std::vector<uint8_t> mat;   // n x n numéros de matière
    std::vector<uint8_t> ids, wts;   // n x n x 4 : matières (127 : aucune) et poids (somme 255) ; vides si inconnus
};

std::vector<std::string> terrainScenes(const Game& game);

// Table des matières du .tscb : pour la matière k des .mate, sa couche de Tex/MaterialAlb et ses répétitions par
// mètre (u, v). Vide si introuvable.
struct TerrainMaterial { int layer = 0; float u = 0.25f, v = 0.25f; };
std::vector<TerrainMaterial> terrainMaterials(const Game& game, const std::string& scene);
std::vector<TerrainTile> terrainBuild(const Game& game, const std::string& scene, int budget = 1600);

// Ombre ambiante précalculée du terrain (<tuile>.<variante>.bake_ao.extm : TARB, BC5 260 × 260 rangé à la Switch
// (17 GOB de large, blocs de 8 GOB), canal R = occlusion ; la tuile occupe les texels 2 à 258). Seules les tuiles
// grossières en ont (Kokiri : niveaux 0 à 2) ; variante « child » / « adult » d'abord, puis sans variante.
struct TerrainBake { float cx = 0, cz = 0, side = 0; std::vector<uint8_t> ao; };   // (ao : 260 × 260)
std::vector<TerrainBake> terrainBakes(const Game& game, const std::string& scene, const std::string& variant);
// (occlusion au point (x, z) d'après la tuile précalculée la plus fine qui le couvre ; 1 si aucune)
float terrainBakeAo(const std::vector<TerrainBake>& bakes, float x, float z);

}  // namespace te
