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
std::vector<TerrainTile> terrainBuild(const Game& game, const std::string& scene, int budget = 1600);

}  // namespace te
