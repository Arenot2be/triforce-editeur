#include "core/models.hpp"

#include <algorithm>

namespace te {

void ModelIndex::index() {
    std::lock_guard<std::mutex> lk(m_);
    if (indexed_) return;
    indexed_ = true;
    std::error_code ec;
    fs::path folder = game_.path("Model");
    if (!fs::is_directory(folder, ec)) return;
    std::vector<std::string> names;
    for (auto& e : fs::directory_iterator(folder, ec)) names.push_back(pathStr(e.path().filename()));
    // (le jeu ouvre toujours <modèle>.bfres.zs : quand le même modèle existe aussi en .bfres.mc, c'est le .zs qui
    //  compte — les .mc sont d'anciennes copies, dont certaines dans un format MeshCodec 1.0 que le jeu ne lit pas)
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        bool az = endsWith(a, ".zs"), bz = endsWith(b, ".zs");
        if (az != bz) return az;
        return a < b;
    });
    for (auto& n : names) {
        if (!(endsWith(n, ".bfres.zs") || endsWith(n, ".bfres.mc")) || n.find(".anim.") != std::string::npos) continue;
        auto parts = split(n.substr(0, n.size() - 9), '.');
        if (parts.back() == "Skeleton" || endsWith(parts.back(), "_Animation")) continue;
        std::string rel = "Model/" + n;
        byName_[parts.back()].push_back(rel);
        byName_[lower(parts.back())].push_back(rel);
        if (parts.size() == 2) {
            byPair_[{parts[0], parts[1]}].push_back(rel);
            byPair_[{lower(parts[0]), lower(parts[1])}].push_back(rel);
        }
    }
}

std::shared_ptr<const Sarc> ModelIndex::pack(const std::string& actor) {
    std::string rel = "Pack/Actor/" + actor + ".pack.zs";
    if (!game_.exists(rel)) return nullptr;
    return game_.pack(rel);
}

std::optional<Value> ModelIndex::member(const std::shared_ptr<const Sarc>& p, const Value* ref, const std::string& owner) {
    if (!ref || ref->t != T::String) return std::nullopt;
    std::string name = startsWith(ref->s, "Work/") ? ref->s.substr(5) : ref->s;
    if (endsWith(name, ".gyml")) name = name.substr(0, name.size() - 5) + ".bgyml";
    for (int pass = 0; pass < 2; pass++) {
        std::shared_ptr<const Sarc> s = p;
        if (pass == 1) {
            auto slash = name.rfind('/');
            std::string stem = name.substr(slash == std::string::npos ? 0 : slash + 1);
            stem = stem.substr(0, stem.find('.'));
            if (stem == owner) break;
            try { s = pack(stem); } catch (...) { s = nullptr; }
        }
        if (s) {
            if (auto f = s->get(name)) {
                try { return bymlLoad(game_.zs.unpack(*f)).root; } catch (...) { return std::nullopt; }
            }
        }
    }
    return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> ModelIndex::info(const std::string& actor, int depth) {
    if (depth > 6) return std::nullopt;
    auto p = pack(actor);
    if (!p) return std::nullopt;
    std::optional<Value> param;
    for (auto& [n, d] : p->files) {
        if (startsWith(n, "Actor/") && endsWith(n, ".engine__actor__ActorParam.bgyml")) {
            try { param = bymlLoad(game_.zs.unpack(d)).root; } catch (...) {}
            break;
        }
    }
    std::vector<Value> refs;
    if (param && param->isMap())
        if (auto c = param->get("Components"); c && c->isMap())
            if (auto r = c->get("ModelInfoRef")) refs.push_back(*r);
    std::vector<std::string> mi;
    for (auto& [n, d] : p->files) if (n.find("/ModelInfo/") != std::string::npos && endsWith(n, ".bgyml")) mi.push_back(n);
    std::stable_sort(mi.begin(), mi.end(), [&](auto& a, auto& b) {
        bool ia = a.find(actor) != std::string::npos, ib = b.find(actor) != std::string::npos;
        return ia && !ib;
    });
    for (auto& n : mi) refs.push_back(Value::str(n));
    std::map<std::string, std::string> found;
    for (auto& ref : refs) {
        auto d = member(p, &ref, actor);
        int seen = 0;
        while (d && d->isMap() && seen < 6) {
            for (auto k : {"FmdbName", "ModelProjectName"}) {
                auto v = d->get(k);
                if (!found.count(k) && v && v->t == T::String && !v->s.empty()) found[k] = v->s;
            }
            if (found.count("FmdbName")) break;
            Value par = d->get("$parent") ? *d->get("$parent") : Value();
            d = member(p, &par, actor);
            seen++;
        }
        if (found.count("FmdbName")) return std::make_pair(found["FmdbName"], found.count("ModelProjectName") ? found["ModelProjectName"] : "");
    }
    if (param && param->isMap()) {
        if (auto par = param->get("$parent"); par && par->t == T::String) {
            std::string parent = par->s.substr(par->s.rfind('/') == std::string::npos ? 0 : par->s.rfind('/') + 1);
            parent = parent.substr(0, parent.find('.'));
            if (!parent.empty() && parent != actor) return info(parent, depth + 1);
        }
    }
    return std::nullopt;
}

std::string ModelIndex::path(const std::string& fmdb, const std::string& proj) {
    index();
    if (!proj.empty()) {
        if (auto it = byPair_.find({proj, fmdb}); it != byPair_.end()) return it->second[0];
    }
    if (auto it = byName_.find(fmdb); it != byName_.end()) return it->second[0];
    if (!proj.empty()) {
        if (auto it = byPair_.find({lower(proj), lower(fmdb)}); it != byPair_.end()) return it->second[0];
    }
    if (auto it = byName_.find(lower(fmdb)); it != byName_.end()) return it->second[0];
    return "";
}

std::string ModelIndex::file(const std::string& actor) {
    if (actor.empty() || actor.find_first_of("\\/:*?\"<>|") != std::string::npos) throw Error("nom d'acteur invalide");
    auto inf = info(actor);
    if (!inf) {
        // (sans ModelInfo : un modèle de Model/ qui porte le nom de l'acteur, s'il y en a un)
        std::string rel = path(actor, "");
        if (!rel.empty()) return rel;
        // (pour comprendre : ce que contient le paquet de l'acteur)
        std::string inside;
        if (auto p = pack(actor)) {
            int k = 0;
            for (auto& [n, d] : p->files) {
                if (k++ < 12) inside += (inside.empty() ? "" : ", ") + n;
            }
            inside = " ; son paquet contient " + std::to_string(p->files.size()) + " fichier(s) : " + inside;
        } else {
            inside = " ; pas de paquet Pack/Actor/" + actor + ".pack.zs";
        }
        throw Error("pas de ModelInfo pour " + actor + " (pas de modèle à son nom dans Model/ non plus)" + inside);
    }
    std::string rel = path(inf->first, inf->second);
    if (rel.empty()) throw Error("modèle " + inf->second + "." + inf->first + " introuvable dans Model/");
    return rel;
}

Mesh ModelIndex::load(const std::string& actor) {
    std::string rel = file(actor);
    auto inf = info(actor);
    try {
        return bfresRead(game_.read(rel), inf ? inf->first : actor);
    } catch (const std::exception& e) {
        throw Error(std::string("modèle illisible : ") + e.what());
    }
}

}  // namespace te
