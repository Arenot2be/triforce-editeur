// Une carte ouverte dans l'éditeur : ses couches d'objets placés, l'annulation, l'enregistrement dans le dossier de
// travail (même fichier .json que le modkit Python), comme triforce_modkit/editor.py et la page de l'éditeur.
#pragma once
#include <array>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>

#include "core/byml.hpp"
#include "core/game.hpp"

namespace te {

struct PathElem {
    bool isIndex = false;
    std::string key;
    size_t index = 0;
};
using VPath = std::vector<PathElem>;
std::string pathName(const VPath& p);

struct Rec {
    Value value;
    int src = -1;      // (n° dans la couche d'origine, -1 pour un objet nouveau)
    bool dup = false;  // (copie : nouveaux identifiants à l'enregistrement)
    int layer = 0;
};
using RecP = std::shared_ptr<Rec>;

struct Layer {
    VPath path;
    std::string name;
    bool visible = true;
    std::vector<RecP> recs;
};

// -- lecture des objets
const char* posKey(const Value& v);
const char* rotKey(const Value& v);
std::string nameOf(const Value& v);
std::string catOf(const std::string& name);
bool vec3(const Value& v, const char* key, double out[3]);
void setNum(Value& arr, size_t i, double x);   // (garde F64 si c'était un F64, sinon F32)
void setVec(Value& obj, const char* key, const double v[3]);

struct MapEntry { std::string path; bool edited = false; };

class MapDoc {
public:
    std::string path, origin;   // origin : "jeu" ou "travail"
    std::vector<Layer> layers;
    bool dirty = false;

    static std::vector<MapEntry> listMaps(const Game& game, const fs::path& work);
    static fs::path editPath(const fs::path& work, const std::string& rel);
    void open(Game& game, const fs::path& work, const std::string& rel);
    // écrit la carte dans le dossier de travail ; renvoie le chemin et remplit les avertissements
    fs::path save(Game& game, const fs::path& work, std::vector<std::string>& warnings);
    static void revert(const fs::path& work, const std::string& rel);

    // -- annuler / rétablir (comme la page : instantané des couches et des valeurs touchées)
    struct Tx {
        std::vector<std::vector<RecP>> lay;
        std::map<Rec*, std::pair<RecP, Value>> vals;
    };
    Tx begin() const;
    void touch(Tx& tx, const RecP& r) const;
    void commit(Tx tx);
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

private:
    struct Step { Tx before, after; };
    std::vector<Step> undo_, redo_;
    uint64_t rev_ = 0;
    void restore(const Tx& st);
};

}  // namespace te
