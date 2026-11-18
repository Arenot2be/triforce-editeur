#include "core/game.hpp"
#include "core/meshcodec.hpp"

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
