// Acteur -> modèle 3D (Pack/Actor/X.pack.zs, ModelInfo, Model/*.bfres.zs), comme Editor.model_info / model_path.
#pragma once
#include <map>
#include <mutex>
#include <optional>
#include <tuple>

#include "core/bfres.hpp"
#include "core/byml.hpp"
#include "core/game.hpp"

namespace te {

class ModelIndex {
public:
    explicit ModelIndex(Game& g) : game_(g) {}
    // (FmdbName, ModelProjectName)
    std::optional<std::pair<std::string, std::string>> info(const std::string& actor, int depth = 0);
    std::string path(const std::string& fmdb, const std::string& proj);
    Mesh load(const std::string& actor);   // (Error avec la raison si pas de modèle)

private:
    Game& game_;
    std::mutex m_;
    bool indexed_ = false;
    std::map<std::string, std::vector<std::string>> byName_;
    std::map<std::pair<std::string, std::string>, std::vector<std::string>> byPair_;
    std::shared_ptr<const Sarc> pack(const std::string& actor);
    std::optional<Value> member(const std::shared_ptr<const Sarc>& pack, const Value* ref, const std::string& owner);
    void index();
};

}  // namespace te
