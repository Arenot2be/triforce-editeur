// « Construire le mod » : comme cmd_build de triforce_modkit/cli.py (mêmes règles, même journal).
#include <algorithm>

#include "core/byml.hpp"
#include "core/game.hpp"
#include "core/restbl.hpp"
#include "core/textmap.hpp"

namespace te {

namespace {
struct Edit { std::string rel; fs::path full; };

std::vector<Edit> editedFiles(const fs::path& work) {
    std::vector<Edit> out;
    std::error_code ec;
    if (!fs::is_directory(work, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(work, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        std::string rel = pathStr(fs::relative(it->path(), work, ec));
        rel = replaceAll(rel, "\\", "/");
        if (endsWith(rel, ".tmp")) continue;
        if (endsWith(rel, ".json")) rel.resize(rel.size() - 5);
        auto parts = split(rel, '/');
        for (size_t i = 0; i + 1 < parts.size(); i++) {
            if (isArchive(parts[i])) {
                std::string a, b;
                for (size_t k = 0; k <= i; k++) a += (k ? "/" : "") + parts[k];
                for (size_t k = i + 1; k < parts.size(); k++) b += (k > i + 1 ? "/" : "") + parts[k];
                rel = a + ":" + b;
                break;
            }
        }
        out.push_back({rel, it->path()});
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.rel < b.rel; });
    return out;
}

struct Loaded { Bytes data; bool isDoc = false; Document doc; };

Loaded loadEdit(const fs::path& full) {
    Loaded l;
    if (endsWith(pathStr(full), ".json")) {
        l.doc = fromText(readText(full));
        l.data = bymlDump(l.doc);
        l.isDoc = true;
    } else {
        l.data = readFile(full);
    }
    return l;
}
}  // namespace

int buildMod(Game& game, const fs::path& work, const fs::path& mods, const std::string& name,
             const std::function<void(const std::string&)>& say) {
    if (name.empty() || name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
        say("Nom de mod invalide : « " + name + " »");
        return 1;
    }
    auto edits = editedFiles(work);
    if (edits.empty()) {
        say("Le dossier de travail est vide : extrais ou modifie d'abord des fichiers (" + pathStr(work) + ")");
        return 1;
    }
    fs::path outRoot = mods / u8path(name) / u8path("romfs");
    // (table des tailles de départ : celle de Triforce s'il en a une, sinon celle du jeu)
    std::error_code ec;
    std::string tableName;
    for (auto& e : fs::directory_iterator(game.path("System/Resource"), ec)) {
        std::string n = pathStr(e.path().filename());
        if (startsWith(n, "ResourceSizeTable.") && endsWith(n, ".rsizetable.zs") && (tableName.empty() || n < tableName)) tableName = n;
    }
    if (tableName.empty()) { say("Pas de table des tailles dans le jeu (System/Resource)"); return 1; }
    std::string tableRel = "System/Resource/" + tableName, tableFrom = "celle du jeu";
    Restbl table;
    fs::path gameDir = fs::absolute(game.romfs, ec).parent_path();
    bool found = false;
    for (auto cand : {gameDir / u8path("prompts") / u8path("rsizetable.zs"), gameDir / u8path("bin") / u8path("prompts") / u8path("rsizetable.zs")}) {
        if (fs::is_regular_file(cand, ec)) {
            table = Restbl::load(game.zs.unpack(readFile(cand)));
            tableFrom = "celle de Triforce (" + pathStr(cand) + ")";
            found = true;
            break;
        }
    }
    if (!found) table = Restbl::load(game.read(tableRel));

    std::map<std::string, Loaded> loose;
    std::map<std::string, std::map<std::string, Loaded>> members;
    std::vector<std::string> errors;
    for (auto& e : edits) {
        try {
            Loaded l = loadEdit(e.full);
            auto c = e.rel.find(':');
            if (c != std::string::npos) members[e.rel.substr(0, c)][e.rel.substr(c + 1)] = std::move(l);
            else loose[e.rel] = std::move(l);
        } catch (const std::exception& x) {
            errors.push_back(pathStr(fs::relative(e.full, work, ec)) + " : " + x.what());
        }
    }
    if (!errors.empty()) {
        say("ERREURS, rien n'a été construit :");
        for (auto& x : errors) say("  - " + x);
        return 1;
    }
    std::vector<std::string> written;
    std::vector<std::pair<std::string, std::pair<std::optional<uint32_t>, uint32_t>>> sizes;
    auto unchanged = [](const Bytes* orig, const Loaded& l) {
        if (!orig) return false;
        if (l.isDoc && isByml(*orig)) {
            try { return same(bymlLoad(*orig).root, l.doc.root); } catch (...) { return false; }
        }
        return *orig == l.data;
    };
    auto emit = [&](const std::string& rel, const Bytes& payload, uint32_t dict) {
        fs::path dst = outRoot;
        for (auto& p : split(rel, '/')) if (!p.empty()) dst /= u8path(p);
        Bytes blob = endsWith(rel, ".zs") ? game.zs.pack(payload, dict) : payload;
        writeFileAtomic(dst, blob);
        written.push_back(rel);
    };
    for (auto& [rel, l] : loose) {
        bool exists = game.exists(rel);
        Bytes orig;
        if (exists) orig = game.read(rel);
        if (unchanged(exists ? &orig : nullptr, l)) continue;
        uint32_t did = exists ? Zs::dictId(game.raw(rel)) : game.zs.dictForName(rel);
        emit(rel, l.data, did);
        if (auto r = table.fit(rel, l.data.size(), exists ? std::optional<size_t>(orig.size()) : std::nullopt)) sizes.emplace_back(rel, *r);
    }
    for (auto& [packRel, changed] : members) {
        if (!game.exists(packRel)) { say("  (archive inconnue du jeu, ignorée : " + packRel + ")"); continue; }
        Sarc s = *game.pack(packRel);
        size_t origSize = game.read(packRel).size();
        bool touched = false;
        for (auto& [member, l] : changed) {
            const Bytes* old = s.get(member);
            Bytes oldPlain;
            if (old) oldPlain = game.zs.unpack(*old);
            if (unchanged(old ? &oldPlain : nullptr, l)) continue;
            Bytes data = l.data;
            if (old && Zs::isZstd(*old)) data = game.zs.pack(data, Zs::dictId(*old));
            bool hadOld = old != nullptr;
            s.set(member, data);
            touched = true;
            if (table.get(member)) {
                if (auto r = table.fit(member, data.size(), hadOld ? std::optional<size_t>(oldPlain.size()) : std::nullopt))
                    sizes.emplace_back(packRel + ":" + member, *r);
            }
        }
        if (!touched) continue;
        Bytes rebuilt = s.dump();
        emit(packRel, rebuilt, Zs::dictId(game.raw(packRel)));
        if (auto r = table.fit(packRel, rebuilt.size(), origSize)) sizes.emplace_back(packRel, *r);
    }
    if (written.empty()) { say("Aucun fichier modifié par rapport au jeu : rien à construire."); return 0; }
    if (!sizes.empty()) emit(tableRel, table.dump(), 0);
    say("Mod construit : " + pathStr(mods / u8path(name)));
    for (auto& rel : written) say("  écrit   " + rel);
    for (auto& [rel, r] : sizes)
        say("  taille  " + rel + " : " + (r.first ? std::to_string(*r.first) : std::string("nouvelle")) + " -> " + std::to_string(r.second) + " octets");
    if (!sizes.empty()) {
        say("");
        say("Table des tailles mise à jour, à partir de " + tableFrom + ".");
        say("Un seul mod à la fois peut fournir cette table : celui dont le nom est le dernier dans l'ordre alphabétique gagne.");
    }
    say("Pour désactiver le mod : renomme son dossier en le faisant commencer par _ .");
    return 0;
}

}  // namespace te
