#include "core/game.hpp"
#include "core/meshcodec.hpp"
#include "core/bntx.hpp"

#include <fstream>
#include <sstream>

namespace te {

Folders Folders::defaults() {
    Folders f;
    fs::path home = homeDir();
    f.romfs = home / u8path("Games") / u8path("Triforce") / u8path("romfs");
    f.work = home / u8path("Documents") / u8path("Triforce-modkit") / u8path("travail");
    f.mods = defaultMods(f.romfs);
    return f;
}

fs::path Folders::defaultMods(const fs::path& romfs) {
    std::error_code ec;
    fs::path game = fs::absolute(romfs, ec).parent_path();
    for (auto folder : {game, game / u8path("bin")})
        if (fs::is_directory(folder / u8path("mods"), ec)) return folder / u8path("mods");
    for (auto folder : {game / u8path("bin"), game}) {
        if (!fs::is_directory(folder, ec)) continue;
        for (auto& e : fs::directory_iterator(folder, ec))
            if (lower(pathStr(e.path().extension())) == ".exe") return folder / u8path("mods");
    }
    return game / u8path("mods");
}

Game::Game(const fs::path& r) : romfs(r), zs(r) {
    std::error_code ec;
    if (!fs::is_directory(r, ec)) throw Error("Dossier romfs introuvable : " + pathStr(r));
}

fs::path Game::path(const std::string& rel) const {
    fs::path p = romfs;
    for (auto& part : split(rel, '/')) if (!part.empty()) p /= u8path(part);
    return p;
}
bool Game::exists(const std::string& rel) const { std::error_code ec; return fs::is_regular_file(path(rel), ec); }
Bytes Game::raw(const std::string& rel) const { return readFile(path(rel)); }
Bytes Game::read(const std::string& rel) const {
    Bytes r = raw(rel);
    if (isMeshCodec(r)) return meshCodecUnpack(r);   // (.bfres.mc)
    return zs.unpack(r);
}

std::string Game::texFile(const std::string& name) {
    std::string rel = "Tex/" + name + ".bntx.zs";
    if (exists(rel)) return rel;
    std::lock_guard<std::mutex> lk(texM_);
    if (!texIndexed_) {
        texIndexed_ = true;
        try { buildTexIndex(); } catch (...) {}
    }
    auto it = texIndex_.find(lower(name));
    return it == texIndex_.end() ? "" : it->second;
}

// (seules les textures rangées sous un autre nom que le leur sont retenues ; l'en-tête d'un BNTX, avec les noms, est
// au début du fichier : on n'en décompresse que le début, et tout le fichier seulement si un nom est plus loin)
void Game::buildTexIndex() {
    std::error_code ec;
    fs::path dir = romfs / "Tex";
    std::vector<std::pair<std::string, uintmax_t>> files;
    uintmax_t total = 0;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::string rel = pathStr(fs::relative(it->path(), romfs, ec));
        for (auto& c : rel) if (c == '\\') c = '/';
        if (!endsWith(rel, ".bntx.zs")) continue;
        uintmax_t sz = it->file_size(ec);
        files.emplace_back(rel, sz);
        total += sz;
    }
    std::sort(files.begin(), files.end());
    std::string key = fmt("v1 %zu %llu ", files.size(), (unsigned long long)total) + pathStr(romfs);
    fs::path cache = homeDir() / u8path("Documents") / u8path("Triforce-modkit") / u8path("cache") / u8path("index-textures.txt");
    {
        std::ifstream f(cache, std::ios::binary);
        std::string line;
        if (f && std::getline(f, line) && line == key) {
            while (std::getline(f, line)) {
                auto t = line.find('\t');
                if (t != std::string::npos) texIndex_[line.substr(0, t)] = line.substr(t + 1);
            }
            return;
        }
    }
    for (auto& [rel, sz] : files) {
        try {
            std::string stem = rel.substr(rel.rfind('/') + 1);
            stem = lower(stem.substr(0, stem.size() - 8));
            Bytes head = zs.unpack(readFileHead(path(rel), 16384), 65536);
            auto names = bntxNames(head);
            bool cut = names.empty();
            for (auto& n : names) if (n.empty()) cut = true;
            if (cut) names = bntxNames(read(rel));
            for (auto& n : names) {
                std::string k = lower(n);
                if (!k.empty() && k != stem && !texIndex_.count(k)) texIndex_[k] = rel;
            }
        } catch (...) {
        }
    }
    std::ostringstream o;
    o << key << "\n";
    for (auto& [k, v] : texIndex_) o << k << "\t" << v << "\n";
    fs::create_directories(cache.parent_path(), ec);
    std::string txt = o.str();
    try { writeFileAtomic(cache, Bytes(txt.begin(), txt.end())); } catch (...) {}
}

std::shared_ptr<const Sarc> Game::pack(const std::string& rel) {
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = packs_.find(rel);
        if (it != packs_.end()) return it->second;
    }
    auto s = std::make_shared<const Sarc>(Sarc::load(read(rel)));
    std::lock_guard<std::mutex> lk(m_);
    if (packs_.size() > 256) packs_.clear();
    packs_[rel] = s;
    return s;
}
void Game::forget(const std::string& rel) { std::lock_guard<std::mutex> lk(m_); packs_.erase(rel); }

bool isArchive(const std::string& name) {
    for (auto s : {".pack.zs", ".sarc.zs", ".blarc.zs", ".bfarc.zs", ".pack", ".sarc"}) if (endsWith(name, s)) return true;
    return false;
}

fs::path workPath(const fs::path& work, const std::string& rel) {
    fs::path p = work;
    std::string r = rel;
    auto colon = r.find(':');
    if (colon != std::string::npos) r = r.substr(0, colon) + "/" + r.substr(colon + 1);
    for (auto& part : split(r, '/')) if (!part.empty()) p /= u8path(part);
    return p;
}

}  // namespace te
