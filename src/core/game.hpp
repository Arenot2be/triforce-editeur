// Les fichiers du jeu (romfs) et les dossiers du modkit, comme triforce_modkit/cli.py.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "core/sarc.hpp"
#include "core/zs.hpp"

namespace te {

struct Folders {
    fs::path romfs, work, mods;
    static Folders defaults();
    static fs::path defaultMods(const fs::path& romfs);
};

class Game {
public:
    explicit Game(const fs::path& romfs);
    fs::path romfs;
    Zs zs;
    fs::path path(const std::string& rel) const;
    bool exists(const std::string& rel) const;
    Bytes raw(const std::string& rel) const;
    Bytes read(const std::string& rel) const;    // (décompressé)
    std::shared_ptr<const Sarc> pack(const std::string& rel);   // (gardé en mémoire)
    void forget(const std::string& rel);
    // Le fichier Tex/… qui contient la texture `name` : Tex/<name>.bntx.zs, sinon un autre fichier de Tex/ qui la
    // contient aussi (index construit à la première absence, gardé dans Documents\Triforce-modkit\cache). "" si aucun.
    std::string texFile(const std::string& name);
    // (fichiers du romfs dont le nom contient `name`, toutes extensions : pour dire où est une texture introuvable)
    std::vector<std::string> findAnywhere(const std::string& name, size_t max = 4);
    // (texture voisine d'une texture absent du jeu, de la même famille de noms : Insect_C -> Insect_A,
    //  GraphicVine_A_01 -> GraphicVine_A_00… ; {nom, fichier}, ou {"", ""} si aucune n'est assez proche)
    std::pair<std::string, std::string> similarTex(const std::string& name);

private:
    void buildTexIndex();
    std::mutex m_, texM_;
    bool texIndexed_ = false;
    std::map<std::string, std::string> texIndex_;   // (nom en minuscules -> chemin)
    bool allListed_ = false;
    std::vector<std::string> all_;   // (tous les fichiers du romfs, chemins relatifs)
    std::map<std::string, std::shared_ptr<const Sarc>> packs_;
};

bool isArchive(const std::string& name);
fs::path workPath(const fs::path& work, const std::string& rel);   // (romfs/chemin, ou romfs/archive/membre pour « archive:membre »)

// « Construire le mod » : les fichiers du dossier de travail -> mods/<nom>/romfs ; log reçoit chaque ligne du journal
int buildMod(Game& game, const fs::path& work, const fs::path& mods, const std::string& name,
             const std::function<void(const std::string&)>& log);

}  // namespace te
