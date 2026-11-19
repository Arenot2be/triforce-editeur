// Recherche approfondie de textures dans tout le jeu : chaque fichier est décompressé (et chaque archive ouverte,
// membre par membre) pour y chercher le nom des textures introuvables, et toute texture BNTX qui les contient,
// où qu'elle soit rangée (dans un modèle, un pack, un autre fichier…).
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace te {

class Game;

struct TexSearch {
    std::vector<std::string> names;   // (textures cherchées)
    std::atomic<size_t> done{0}, total{0}, bytes{0}, errors{0};
    std::atomic<bool> cancel{false}, finished{false};
    std::mutex m;
    struct Seen { std::string text, where; };
    std::vector<std::vector<Seen>> seen;          // (par nom : textes du jeu qui contiennent sa famille de noms)
    std::vector<std::vector<std::string>> usable; // (par nom : emplacements d'une texture BNTX qui la contient)
    std::vector<size_t> files;                    // (par nom : nombre de fichiers où sa famille apparaît)
    std::string report() const;
};

// (bloquant ; à lancer dans un fil à part ; threads : fils de travail)
void searchTextures(Game& g, TexSearch& s, int threads);

}  // namespace te
