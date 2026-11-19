#include "app/editor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

#include "core/astc.hpp"
#include "core/textmap.hpp"

namespace te {

static double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string fmtNum(double v) {
    std::string s = fmt("%.4f", v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

std::array<float, 3> hueColor(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    float hue = float(h % 360) / 60.f, sat = 0.55f, lig = 0.58f;
    float c = (1 - std::fabs(2 * lig - 1)) * sat, x = c * (1 - std::fabs(std::fmod(hue, 2.f) - 1)), m = lig - c / 2;
    float r = 0, g = 0, b = 0;
    int k = int(hue);
    if (k == 0) { r = c; g = x; } else if (k == 1) { r = x; g = c; } else if (k == 2) { g = c; b = x; }
    else if (k == 3) { g = x; b = c; } else if (k == 4) { r = x; b = c; } else { r = c; b = x; }
    return {r + m, g + m, b + m};
}

// ---------------------------------------------------------------- travaux en arrière-plan
Jobs::Jobs(int n) {
    for (int i = 0; i < n; i++)
        th_.emplace_back([this] {
            while (true) {
                J j;
                {
                    std::unique_lock<std::mutex> lk(m_);
                    cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
                    if (stop_) return;
                    auto it = std::min_element(q_.begin(), q_.end(), [](auto& a, auto& b) { return a.prio != b.prio ? a.prio < b.prio : a.seq < b.seq; });
                    j = std::move(*it);
                    q_.erase(it);
                    running_++;
                }
                std::function<void()> then;
                try { then = j.f(); } catch (...) {}
                std::lock_guard<std::mutex> lk(m_);
                running_--;
                if (then) done_.push_back(std::move(then));
            }
        });
}
Jobs::~Jobs() {
    stop_ = true;
    cv_.notify_all();
    for (auto& t : th_) t.join();
}
void Jobs::add(int prio, std::function<std::function<void()>()> job) {
    std::lock_guard<std::mutex> lk(m_);
    q_.push_back({prio, seq_++, std::move(job)});
    cv_.notify_one();
}
void Jobs::pump(double budgetMs) {
    double end = now() + budgetMs / 1000;
    while (true) {
        std::function<void()> f;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (done_.empty()) return;
            f = std::move(done_.front());
            done_.pop_front();
        }
        f();
        if (now() > end) return;
    }
}
void Jobs::clear() { std::lock_guard<std::mutex> lk(m_); q_.clear(); }
size_t Jobs::pending() const { std::lock_guard<std::mutex> lk(m_); return q_.size() + size_t(running_) + done_.size(); }

V3 Camera::eye() const {
    if (top) return target + V3(0, std::max(400.0, topSpan * 2), 0);
    return target + V3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)) * dist;
}

// ---------------------------------------------------------------- dossiers et jeu
Editor::Editor(Vk& v, Renderer& r) : vk(v), rend(r) {
    folders = Folders::defaults();
    loadSettings();
}
Editor::~Editor() {
    if (texSearch) texSearch->cancel = true;
    if (searchThread_.joinable()) searchThread_.join();
    if (buildThread_.joinable()) buildThread_.join();
    jobs.clear();
}

static fs::path settingsPath() { return homeDir() / u8path("Documents") / u8path("Triforce-modkit") / u8path("editeur.ini"); }

void Editor::loadSettings() {
    std::error_code ec;
    if (!fs::is_regular_file(settingsPath(), ec)) return;
    for (auto& line : split(readText(settingsPath()), '\n')) {
        std::string l = line;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        auto eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == "romfs") folders.romfs = u8path(v);
        else if (k == "travail") folders.work = u8path(v);
        else if (k == "mods") folders.mods = u8path(v);
        else if (k == "mod") modName = v;
    }
}
void Editor::saveSettings() {
    std::string s = "romfs=" + pathStr(folders.romfs) + "\ntravail=" + pathStr(folders.work) + "\nmods=" + pathStr(folders.mods) + "\nmod=" + modName + "\n";
    try { writeFileAtomic(settingsPath(), s); } catch (...) {}
}

void Editor::openGame() {
    jobs.clear();
    vkDeviceWaitIdle(vk.dev);
    freeAssets();
    hasMap = false;
    doc = MapDoc();
    sel.clear();
    game.reset();
    models.reset();
    maps.clear();
    scenes.clear();
    gameError.clear();
    try {
        game = std::make_unique<Game>(folders.romfs);
        models = std::make_unique<ModelIndex>(*game);
        maps = MapDoc::listMaps(*game, folders.work);
        scenes = terrainScenes(*game);
        if (maps.empty()) gameError = "Aucune carte (Banc/*.bcett.byml.zs) dans " + pathStr(folders.romfs);
    } catch (const std::exception& e) {
        gameError = e.what();
        game.reset();
    }
}

void Editor::freeAssets() {
    vkDeviceWaitIdle(vk.dev);
    for (auto& [k, s] : modelSlots) if (s.st == ModelSlot::Ok) rend.free(s.gpu);
    for (auto& [k, s] : texSlots) if (s.st == TexSlot::Ok) vk.free(s.tex);
    modelSlots.clear();
    texSlots.clear();
    rend.setTerrain({}, {});
    tiles.clear();
    terrainScene.clear();
}

// ---------------------------------------------------------------- carte
void Editor::openMap(const std::string& rel) {
    if (!game) return;
    mapError.clear();
    try {
        MapDoc d;
        d.open(*game, folders.work, rel);
        doc = std::move(d);
        hasMap = true;
        sel.clear();
        hover = nullptr;
        rebuildCats();
        fitAll();
        std::string sc = autoTerrain();
        if (sc != terrainScene) setTerrain(sc);
        say("Carte ouverte : " + rel + (doc.origin == "travail" ? " (ta version modifiée)" : " (version du jeu)"));
    } catch (const std::exception& e) {
        mapError = e.what();
        say("Impossible d'ouvrir la carte : " + mapError, true);
    }
}
void Editor::reloadMap() { if (hasMap) { std::string p = doc.path; openMap(p); } }

bool Editor::save() {
    if (!hasMap || !game) return false;
    try {
        std::vector<std::string> warn;
        fs::path p = doc.save(*game, folders.work, warn);
        for (auto& m : maps) if (m.path == doc.path) m.edited = true;
        say("Enregistré dans " + pathStr(p) + (warn.empty() ? "" : "\nAttention : " + warn[0] + (warn.size() > 1 ? fmt(" (et %zu autres)", warn.size() - 1) : "")),
            !warn.empty());
        sel.clear();
        return true;
    } catch (const std::exception& e) {
        say(std::string("Enregistrement impossible : ") + e.what(), true);
        return false;
    }
}

void Editor::revertMap() {
    if (!hasMap) return;
    MapDoc::revert(folders.work, doc.path);
    for (auto& m : maps) if (m.path == doc.path) m.edited = false;
    reloadMap();
    say("Version du jeu rétablie (ta version de travail est supprimée).");
}

// ---------------------------------------------------------------- objets
void Editor::rebuildCats() {
    catCount.clear();
    for (auto& L : doc.layers)
        for (auto& r : L.recs) {
            std::string c = catOf(nameOf(r->value));
            catCount[c]++;
            if (!catOn.count(c)) catOn[c] = true;
            if (!catColor.count(c)) catColor[c] = hueColor(c);
        }
}

bool Editor::visible(const Rec& r) const {
    if (r.layer < 0 || size_t(r.layer) >= doc.layers.size() || !doc.layers[size_t(r.layer)].visible) return false;
    auto it = catOn.find(catOf(nameOf(r.value)));
    return it == catOn.end() || it->second;
}

std::vector<RecP> Editor::allRecs() const {
    std::vector<RecP> out;
    for (auto& L : doc.layers) out.insert(out.end(), L.recs.begin(), L.recs.end());
    return out;
}
RecP Editor::findRec(const Rec* p) const {
    for (auto& L : doc.layers) for (auto& r : L.recs) if (r.get() == p) return r;
    return nullptr;
}

V3 Editor::posOf(const Rec& r) const {
    double p[3] = {0, 0, 0};
    vec3(r.value, posKey(r.value), p);
    return {p[0], p[1], p[2]};
}
V3 Editor::rotOf(const Rec& r) const {
    double p[3] = {0, 0, 0};
    if (auto k = rotKey(r.value)) vec3(r.value, k, p);
    return {p[0], p[1], p[2]};
}
V3 Editor::scaleOf(const Rec& r) const {
    const Value* s = r.value.get("Scale");
    if (s && s->t == T::Array && s->a->size() == 3) {
        V3 o((*s->a)[0].num(), (*s->a)[1].num(), (*s->a)[2].num());
        for (int k = 0; k < 3; k++) if (!(std::fabs(o[k]) > 1e-6) || !std::isfinite(o[k])) o[k] = 1;
        return o;
    }
    if (s && (s->t == T::F32 || s->t == T::F64) && std::fabs(s->f) > 1e-6) return {s->f, s->f, s->f};
    return {1, 1, 1};
}
void Editor::setPos(Rec& r, const V3& p) {
    const char* k = posKey(r.value);
    if (!k) return;
    double v[3] = {p.x, p.y, p.z};
    setVec(r.value, k, v);
}
void Editor::setRot(Rec& r, const V3& e) {
    const char* k = rotKey(r.value);
    if (!k) {
        if (std::fabs(e.x) < 1e-9 && std::fabs(e.y) < 1e-9 && std::fabs(e.z) < 1e-9) return;
        k = "Rotate";
    }
    double v[3] = {e.x, e.y, e.z};
    setVec(r.value, k, v);
}

RecP Editor::primary() const {
    if (sel.size() != 1) return nullptr;
    return findRec(*sel.begin());
}
V3 Editor::selCenter() const {
    V3 c;
    int n = 0;
    for (auto r : sel) if (posKey(r->value)) { c = c + posOf(*r); n++; }
    return n ? c * (1.0 / n) : c;
}
void Editor::select(Rec* r, bool add) {
    if (!add) sel.clear();
    if (!r) return;
    if (add && sel.count(r)) sel.erase(r);
    else sel.insert(r);
}

void Editor::moveSel(const V3& d) {
    auto tx = doc.begin();
    for (auto r : sel) if (posKey(r->value)) { doc.touch(tx, findRec(r)); setPos(*r, posOf(*r) + d); }
    doc.commit(std::move(tx));
}

void Editor::rotateSel(int axis, double a) {
    if (sel.empty()) return;
    auto tx = doc.begin();
    V3 c = selCenter();
    M3 Rw = axisMat(axis, a);
    bool many = sel.size() > 1;
    for (auto r : sel) {
        doc.touch(tx, findRec(r));
        V3 r0 = rotOf(*r);
        setRot(*r, matEuler(matMul(Rw, eulerMat(r0)), &r0));
        if (many && posKey(r->value)) setPos(*r, c + matVec(Rw, posOf(*r) - c));
    }
    doc.commit(std::move(tx));
}

static double headingOf(const V3& rot) {
    V3 f = matVec(eulerMat(rot), {0, 0, 1});
    return std::hypot(f.x, f.z) > 1e-4 ? std::atan2(f.x, f.z) : 0;
}

void Editor::levelSel(bool keepYaw) {
    if (sel.empty()) return;
    auto tx = doc.begin();
    for (auto r : sel) {
        doc.touch(tx, findRec(r));
        setRot(*r, keepYaw ? V3(0, headingOf(rotOf(*r)), 0) : V3());
    }
    doc.commit(std::move(tx));
}

void Editor::duplicateSel() {
    if (sel.empty()) return;
    auto tx = doc.begin();
    std::set<Rec*> fresh;
    for (auto& L : doc.layers) {
        std::vector<RecP> out;
        for (auto& r : L.recs) {
            out.push_back(r);
            if (!sel.count(r.get())) continue;
            auto c = std::make_shared<Rec>(*r);
            c->dup = true;
            c->src = -1;
            if (posKey(c->value)) setPos(*c, posOf(*c) + V3(1, 0, 0));
            out.push_back(c);
            fresh.insert(c.get());
        }
        L.recs = out;
    }
    doc.commit(std::move(tx));
    sel = fresh;
    rebuildCats();
    say(fmt("%zu objet(s) dupliqué(s) : ils recevront de nouveaux identifiants à l'enregistrement.", fresh.size()));
}

void Editor::deleteSel() {
    if (sel.empty()) return;
    auto tx = doc.begin();
    size_t n = sel.size();
    for (auto& L : doc.layers) L.recs.erase(std::remove_if(L.recs.begin(), L.recs.end(), [&](auto& r) { return sel.count(r.get()) > 0; }), L.recs.end());
    doc.commit(std::move(tx));
    sel.clear();
    hover = nullptr;
    rebuildCats();
    say(fmt("%zu objet(s) supprimé(s).", n));
}

void Editor::dropToGround() {
    if (sel.empty()) return;
    if (tiles.empty()) { say("Pas de terrain chargé : choisis une scène de terrain d'abord.", true); return; }
    auto tx = doc.begin();
    int n = 0;
    for (auto r : sel) {
        if (!posKey(r->value)) continue;
        V3 p = posOf(*r);
        double h;
        if (!groundAt(p.x, p.z, h)) continue;
        doc.touch(tx, findRec(r));
        setPos(*r, {p.x, h, p.z});
        n++;
    }
    doc.commit(std::move(tx));
    say(fmt("%d objet(s) posé(s) sur le terrain.", n));
}

bool Editor::applyProps(Rec& r, const std::string& text, std::string& err) {
    try {
        Value v = fromWeb(jsonParse(text), &r.value);
        if (v.t != T::Map) throw Error("un objet { ... } est attendu");
        auto tx = doc.begin();
        doc.touch(tx, findRec(&r));
        r.value = std::move(v);
        doc.commit(std::move(tx));
        rebuildCats();
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

void Editor::editRot(Rec& r, int axis, double deg) {
    auto tx = doc.begin();
    doc.touch(tx, findRec(&r));
    V3 e = rotOf(r);
    e[axis] = deg * 3.14159265358979323846 / 180;
    setRot(r, e);
    doc.commit(std::move(tx));
}
void Editor::editPos(Rec& r, int axis, double v) {
    if (!posKey(r.value)) return;
    auto tx = doc.begin();
    doc.touch(tx, findRec(&r));
    V3 p = posOf(r);
    p[axis] = v;
    setPos(r, p);
    doc.commit(std::move(tx));
}

void Editor::undo() {
    if (doc.undo()) {
        std::set<Rec*> keep;
        for (auto r : sel) if (findRec(r)) keep.insert(r);
        sel = keep;
        hover = nullptr;
        rebuildCats();
    }
}
void Editor::redo() {
    if (doc.redo()) {
        std::set<Rec*> keep;
        for (auto r : sel) if (findRec(r)) keep.insert(r);
        sel = keep;
        hover = nullptr;
        rebuildCats();
    }
}

// ---------------------------------------------------------------- vue 3D
void Editor::updateMatrices() {
    double aspect = double(vpW) / std::max(1, vpH);
    if (cam.top) {
        V3 e = cam.eye();
        view = lookAt(e, cam.target, {0, 0, -1});
        double hw = cam.topSpan / 2, hh = hw / aspect;
        proj = ortho(-hw, hw, -hh, hh, 1, e.y - cam.target.y + 4000);
    } else {
        view = lookAt(cam.eye(), cam.target, {0, 1, 0});
        proj = perspective(50 * 3.14159265358979323846 / 180, aspect, std::max(0.05, cam.dist * 0.002), cam.dist * 30 + 6000);
    }
    vp = mul(proj, view);
    invert(vp, invVp);
}

bool Editor::project(const V3& p, float& sx, float& sy) const {
    const float* m = vp.m;
    float x = m[0] * float(p.x) + m[4] * float(p.y) + m[8] * float(p.z) + m[12];
    float y = m[1] * float(p.x) + m[5] * float(p.y) + m[9] * float(p.z) + m[13];
    float w = m[3] * float(p.x) + m[7] * float(p.y) + m[11] * float(p.z) + m[15];
    if (w <= 1e-5f) return false;
    sx = float(vpX) + (x / w * 0.5f + 0.5f) * float(vpW);
    sy = float(vpY) + (1 - (y / w * 0.5f + 0.5f)) * float(vpH);
    return true;
}

void Editor::ray(float sx, float sy, V3& o, V3& d) const {
    float nx = (sx - float(vpX)) / float(vpW) * 2 - 1, ny = 1 - (sy - float(vpY)) / float(vpH) * 2;
    auto un = [&](float z) {
        const float* m = invVp.m;
        float x = m[0] * nx + m[4] * ny + m[8] * z + m[12], y = m[1] * nx + m[5] * ny + m[9] * z + m[13];
        float zz = m[2] * nx + m[6] * ny + m[10] * z + m[14], w = m[3] * nx + m[7] * ny + m[11] * z + m[15];
        return V3(x / w, y / w, zz / w);
    };
    V3 a = un(0), b = un(1);
    o = a;
    d = norm(b - a);
}

static bool rayBox(const V3& o, const V3& d, const float* bb, double& t) {
    double t0 = -1e30, t1 = 1e30;
    for (int k = 0; k < 3; k++) {
        double lo = bb[k], hi = bb[3 + k];
        if (std::fabs(d[k]) < 1e-12) { if (o[k] < lo || o[k] > hi) return false; continue; }
        double a = (lo - o[k]) / d[k], b = (hi - o[k]) / d[k];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    if (t1 < 0) return false;
    t = t0 > 0 ? t0 : t1;
    return true;
}

Rec* Editor::pick(float sx, float sy) const {
    if (!hasMap) return nullptr;
    V3 o, d;
    ray(sx, sy, o, d);
    Rec* best = nullptr;
    double bt = 1e30;
    for (auto& L : doc.layers) {
        if (!L.visible) continue;
        for (auto& r : L.recs) {
            if (!posKey(r->value) || !visible(*r)) continue;
            V3 p = posOf(*r), s = scaleOf(*r);
            M3 Rt = transpose(eulerMat(rotOf(*r)));
            V3 lo = matVec(Rt, o - p), ld = matVec(Rt, d);
            for (int k = 0; k < 3; k++) { lo[k] /= s[k]; ld[k] /= s[k]; }
            float bb[6] = {-0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
            auto it = modelSlots.find(nameOf(r->value));
            if (showModels && it != modelSlots.end() && it->second.st == ModelSlot::Ok) std::memcpy(bb, it->second.gpu.bbox, sizeof bb);
            double t;
            if (rayBox(lo, ld, bb, t)) {
                // (distance réelle le long du rayon du monde)
                V3 hit = p + matVec(eulerMat(rotOf(*r)), {lo.x + ld.x * t, lo.y + ld.y * t, lo.z + ld.z * t}) - o;
                (void)hit;
                double tw = t * len(V3(ld.x * s.x, ld.y * s.y, ld.z * s.z));
                if (tw < bt) { bt = tw; best = r.get(); }
            }
        }
    }
    return best;
}

void Editor::focus(const std::vector<Rec*>& recs) {
    V3 lo(1e30, 1e30, 1e30), hi(-1e30, -1e30, -1e30);
    int n = 0;
    for (auto r : recs) {
        if (!posKey(r->value)) continue;
        V3 p = posOf(*r);
        for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]); }
        n++;
    }
    if (!n) return;
    cam.target = (lo + hi) * 0.5;
    double span = std::max({hi.x - lo.x, hi.z - lo.z, hi.y - lo.y, 6.0});
    cam.dist = std::clamp(span * 1.2, 8.0, 20000.0);
    cam.topSpan = std::clamp(span * 1.4, 20.0, 40000.0);
}

void Editor::fitAll() {
    std::vector<Rec*> v;
    for (auto& r : allRecs()) if (visible(*r)) v.push_back(r.get());
    focus(v);
    cam.dist *= 0.8;
}

double Editor::gizmoLen() const {
    return cam.top ? cam.topSpan * 0.07 : cam.dist * 0.16;
}

static void boxLines(std::vector<LineVert>& out, const V3& p, const M3& R, const V3& s, const float* bb, const float col[4]) {
    V3 c[8];
    for (int i = 0; i < 8; i++) {
        V3 l(bb[(i & 1) ? 3 : 0] * s.x, bb[(i & 2) ? 4 : 1] * s.y, bb[(i & 4) ? 5 : 2] * s.z);
        c[i] = p + matVec(R, l);
    }
    static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& e : E)
        for (int k : {e[0], e[1]}) out.push_back({{float(c[k].x), float(c[k].y), float(c[k].z)}, {col[0], col[1], col[2], col[3]}});
}

static const char* QUIET[] = {"Land", "EftLoc", "Tree", "Foliage", "Grass"};

void Editor::render(VkCommandBuffer c) {
    updateMatrices();
    VkViewport v{float(vpX), float(vpY + vpH), float(vpW), -float(vpH), 0, 1};
    VkRect2D sc{{vpX, vpY}, {uint32_t(vpW), uint32_t(vpH)}};
    vkCmdSetViewport(c, 0, 1, &v);
    vkCmdSetScissor(c, 0, 1, &sc);
    PC base{};
    std::memcpy(base.vp, vp.m, 64);
    base.scale[0] = base.scale[1] = base.scale[2] = 1;
    if (showTerrain) rend.drawTerrain(c, base);
    rend.lines.clear();
    rend.linesTop.clear();
    // grille autour du point visé
    {
        double step = cam.top ? std::pow(10.0, std::floor(std::log10(std::max(1.0, cam.topSpan / 8)))) : std::pow(10.0, std::floor(std::log10(std::max(1.0, cam.dist / 4))));
        int n = 20;
        double gx = std::floor(cam.target.x / step) * step, gz = std::floor(cam.target.z / step) * step;
        double y = cam.target.y;
        if (!tiles.empty()) { double h; if (groundAt(cam.target.x, cam.target.z, h)) y = h; }
        float col[4] = {1, 1, 1, 0.08f};
        for (int i = -n; i <= n; i++) {
            float a = float(gx + i * step), b = float(gz + i * step);
            rend.lines.push_back({{a, float(y), float(gz - n * step)}, {col[0], col[1], col[2], col[3]}});
            rend.lines.push_back({{a, float(y), float(gz + n * step)}, {col[0], col[1], col[2], col[3]}});
            rend.lines.push_back({{float(gx - n * step), float(y), b}, {col[0], col[1], col[2], col[3]}});
            rend.lines.push_back({{float(gx + n * step), float(y), b}, {col[0], col[1], col[2], col[3]}});
        }
    }
    struct Late { const GpuMesh* g; PC pc; };
    std::vector<Late> late;   // (parties transparentes, dessinées après tout le reste)
    if (hasMap) {
        rend.bindMesh(c);
        std::vector<VkDescriptorSet> sets;
        for (auto& L : doc.layers) {
            if (!L.visible) continue;
            for (auto& r : L.recs) {
                if (!posKey(r->value) || !visible(*r)) continue;
                V3 p = posOf(*r), ro = rotOf(*r), s = scaleOf(*r);
                std::string name = nameOf(r->value);
                PC pc = base;
                pc.pos[0] = float(p.x); pc.pos[1] = float(p.y); pc.pos[2] = float(p.z);
                pc.pos[3] = sel.count(r.get()) ? 1.f : hover == r.get() ? 2.f : 0.f;
                pc.rot[0] = float(ro.x); pc.rot[1] = float(ro.y); pc.rot[2] = float(ro.z);
                pc.scale[0] = float(s.x); pc.scale[1] = float(s.y); pc.scale[2] = float(s.z);
                auto& cc = catColor[catOf(name)];
                pc.color[0] = cc[0]; pc.color[1] = cc[1]; pc.color[2] = cc[2]; pc.color[3] = 1;
                ModelSlot* ms = showModels && !name.empty() ? model(name) : nullptr;
                const GpuMesh* g = &rend.cube;
                const float* bb = rend.cube.bbox;
                if (ms && ms->st == ModelSlot::Ok) {
                    // (parties sans texture d'un vrai modèle : gris clair, à peine teinté par la catégorie ; la couleur
                    // franche reste aux boîtes)
                    for (int k = 0; k < 3; k++) pc.color[k] = 0.72f + 0.2f * (cc[k] - 0.72f);
                    g = &ms->gpu;
                    bb = g->bbox;
                    sets.assign(g->subs.size(), VK_NULL_HANDLE);
                    for (size_t i = 0; i < g->subs.size(); i++) {
                        if (!showTextures || g->subs[i].tex.empty()) continue;
                        TexSlot* t = texture(g->subs[i].tex);
                        if (t && t->st == TexSlot::Ok) sets[i] = t->tex.set;
                    }
                    rend.drawMesh(c, *g, pc, sets);
                    if (g->translucent) late.push_back({g, pc});
                } else {
                    bool quiet = false;
                    for (auto q : QUIET) if (catOf(name) == q) quiet = true;
                    if (quiet && !(ms && ms->st == ModelSlot::None)) { /* (en attente du modèle) */ }
                    sets.clear();
                    rend.drawMesh(c, rend.cube, pc, sets);
                }
                if (sel.count(r.get())) {
                    float col[4] = {1.f, 0.85f, 0.3f, 0.9f};
                    boxLines(rend.linesTop, p, eulerMat(ro), s, bb, col);
                }
            }
        }
    }
    if (!late.empty()) {
        // (eau d'abord, avec sa carte de normales du jeu ; puis fondus et verre)
        static const auto t0 = std::chrono::steady_clock::now();
        float time = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
        time = std::fmod(time, 3600.f);
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, rend.waterPipe);
        std::vector<VkDescriptorSet> nrm, flow;
        std::vector<char> sgn;
        auto loaded = [&](const std::string& n) -> TexSlot* {
            if (n.empty() || !showTextures) return nullptr;
            TexSlot* t = texture(n);
            return t && (t->st == TexSlot::Ok || t->st == TexSlot::Mask) && t->tex.set ? t : nullptr;
        };
        for (auto& l : late) {
            nrm.assign(l.g->subs.size(), VK_NULL_HANDLE);
            flow.assign(l.g->subs.size(), VK_NULL_HANDLE);
            sgn.assign(l.g->subs.size(), 0);
            for (size_t i = 0; i < l.g->subs.size(); i++) {
                if (l.g->subs[i].kind != 1) continue;
                if (TexSlot* t = loaded(l.g->subs[i].nrm)) {
                    nrm[i] = t->tex.set;
                    sgn[i] = t->format.find("sign") != std::string::npos;
                }
                if (TexSlot* t = loaded(l.g->subs[i].flow)) flow[i] = t->tex.set;
            }
            rend.drawWater(c, *l.g, l.pc, nrm, flow, sgn, time);
        }
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, rend.meshBlendPipe);
        std::vector<VkDescriptorSet> none;
        for (auto& l : late) rend.drawMesh(c, *l.g, l.pc, none, true);
    }
    rend.drawLines(c, base);
}

// ---------------------------------------------------------------- poignées
Gizmo Editor::hitGizmo(float sx, float sy) const {
    if (sel.empty()) return {};
    V3 c = selCenter();
    float cx, cy;
    if (!project(c, cx, cy)) return {};
    double L = gizmoLen();
    auto segDist = [](float px, float py, float ax, float ay, float bx, float by) {
        float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
        float t = l2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / l2, 0.f, 1.f) : 0;
        float qx = ax + t * dx - px, qy = ay + t * dy - py;
        return std::sqrt(qx * qx + qy * qy);
    };
    // carré du plan horizontal (au centre)
    if (std::fabs(sx - cx) < 9 && std::fabs(sy - cy) < 9) return {Gizmo::Plane, 1};
    for (int a = 0; a < 3; a++) {
        if (cam.top && a == 1) continue;
        V3 e = c;
        e[a] += L;
        float ex, ey;
        if (project(e, ex, ey) && segDist(sx, sy, cx, cy, ex, ey) < 7) return {Gizmo::Axis, a};
    }
    for (int i : {1, 0, 2}) {
        if (i != 1 && !tilt) continue;
        double rad = L * (i == 1 ? 0.75 : 0.62);
        float px = 0, py = 0;
        bool have = false;
        for (int k = 0; k <= 64; k++) {
            double a = k / 64.0 * 6.283185307179586, u = std::sin(a) * rad, w = std::cos(a) * rad;
            V3 off = i == 1 ? V3(u, 0, w) : i == 0 ? V3(0, u, w) : V3(u, w, 0);
            float qx, qy;
            if (!project(c + off, qx, qy)) { have = false; continue; }
            if (have && segDist(sx, sy, px, py, qx, qy) < 6) return {Gizmo::Ring, i};
            px = qx; py = qy; have = true;
        }
    }
    return {};
}

static bool rayPlaneY(const V3& o, const V3& d, double y, V3& hit) {
    if (std::fabs(d.y) < 1e-9) return false;
    double t = (y - o.y) / d.y;
    if (t < 0) return false;
    hit = o + d * t;
    return true;
}

void Editor::beginDrag(const Gizmo& g, float sx, float sy) {
    ds = DragState();
    ds.g = g;
    ds.center = selCenter();
    ds.sx0 = sx;
    ds.sy0 = sy;
    for (auto r : sel) ds.start.emplace_back(r, posOf(*r), rotOf(*r));
    if (g.kind == Gizmo::Plane) {
        V3 o, d;
        ray(sx, sy, o, d);
        if (!rayPlaneY(o, d, ds.center.y, ds.h0)) return;
    }
    if (g.kind == Gizmo::Ring) {
        if (!project(ds.center, ds.p0x, ds.p0y)) return;
        ds.last = std::atan2(sy - ds.p0y, sx - ds.p0x);
        ds.acc = 0;
        V3 ax;
        ax[g.axis] = 1;
        V3 e = cam.eye();
        ds.sign = dot(ax, e - ds.center) >= 0 ? -1 : 1;
    }
    ds.tx = doc.begin();
    for (auto r : sel) doc.touch(ds.tx, findRec(r));
    ds.active = true;
    drag = g;
}

void Editor::updateDrag(float sx, float sy, bool snap) {
    if (!ds.active) return;
    if (std::fabs(sx - ds.sx0) + std::fabs(sy - ds.sy0) > 1) ds.moved = true;
    V3 delta;
    if (ds.g.kind == Gizmo::Axis) {
        float cx, cy, ex, ey;
        V3 e = ds.center;
        double L = gizmoLen();
        e[ds.g.axis] += L;
        if (!project(ds.center, cx, cy) || !project(e, ex, ey)) return;
        double ax = ex - cx, ay = ey - cy, l2 = ax * ax + ay * ay;
        if (l2 < 1) return;
        double t = ((sx - ds.sx0) * ax + (sy - ds.sy0) * ay) / l2 * L;
        if (snap) t = std::round(t);
        delta[ds.g.axis] = t;
    } else if (ds.g.kind == Gizmo::Plane) {
        V3 o, d, h;
        ray(sx, sy, o, d);
        if (!rayPlaneY(o, d, ds.center.y, h)) return;
        delta = {h.x - ds.h0.x, 0, h.z - ds.h0.z};
        if (snap) { delta.x = std::round(delta.x); delta.z = std::round(delta.z); }
    } else if (ds.g.kind == Gizmo::Ring) {
        float a = std::atan2(sy - ds.p0y, sx - ds.p0x);
        ds.acc += float(wrapA(a - ds.last));
        ds.last = a;
        double da = ds.acc * ds.sign;
        if (snap) da = std::round(da / (3.14159265358979323846 / 12)) * (3.14159265358979323846 / 12);
        ds.shown = da * 180 / 3.14159265358979323846;
        M3 Rw = axisMat(ds.g.axis, da);
        bool many = ds.start.size() > 1;
        for (auto& [r, p0, r0] : ds.start) {
            setRot(*r, matEuler(matMul(Rw, eulerMat(r0)), &r0));
            if (many && posKey(r->value)) setPos(*r, ds.center + matVec(Rw, p0 - ds.center));
        }
        return;
    }
    for (auto& [r, p0, r0] : ds.start) if (posKey(r->value)) setPos(*r, p0 + delta);
}

void Editor::endDrag() {
    if (ds.active && ds.moved) doc.commit(std::move(ds.tx));
    else if (ds.active) for (auto& [p, rv] : ds.tx.vals) rv.first->value = rv.second;
    ds.active = false;
    drag = {};
}

// ---------------------------------------------------------------- modèles et textures
ModelSlot* Editor::model(const std::string& actor) {
    auto it = modelSlots.find(actor);
    if (it != modelSlots.end()) return &it->second;
    if (!models) return nullptr;
    ModelSlot& s = modelSlots[actor];
    ModelIndex* mi = models.get();
    jobs.add(1, [this, actor, mi]() -> std::function<void()> {
        auto mesh = std::make_shared<Mesh>();
        std::string why;
        try { *mesh = mi->load(actor); } catch (const std::exception& e) { why = e.what(); }
        return [this, actor, mesh, why] {
            auto it2 = modelSlots.find(actor);
            if (it2 == modelSlots.end()) return;
            if (!why.empty()) { it2->second.st = ModelSlot::None; it2->second.why = why; return; }
            try {
                it2->second.gpu = rend.upload(*mesh);
                it2->second.st = ModelSlot::Ok;
                it2->second.notes = mesh->notes;
                for (auto& s : mesh->skipped) it2->second.notes.push_back("ignorée : " + s);
            } catch (const std::exception& e) {
                it2->second.st = ModelSlot::None;
                it2->second.why = e.what();
            }
        };
    });
    return &s;
}

// (pour l'analyse des formats pas encore lus : le modèle décompressé, dans Documents\Triforce-modkit\analyse)
void Editor::exportModel(const std::string& actor) {
    if (!models || !game) return;
    try {
        std::string rel = models->file(actor);
        Bytes data = game->read(rel);
        std::string name = rel.substr(rel.rfind('/') + 1);
        for (auto ext : {".zs", ".mc"}) if (endsWith(name, ext)) name = name.substr(0, name.size() - 3);
        fs::path dir = homeDir() / u8path("Documents") / u8path("Triforce-modkit") / u8path("analyse");
        std::error_code ec;
        fs::create_directories(dir, ec);
        writeFileAtomic(dir / u8path(name), data);
        // (et ses textures, décompressées, pour vérifier les coordonnées de texture)
        int nt = 0;
        auto it = modelSlots.find(actor);
        if (it != modelSlots.end())
            for (auto& sb : it->second.gpu.subs) {
                if (sb.tex.empty()) continue;
                std::string t = game->texFile(sb.tex);
                if (t.empty()) continue;
                try {
                    writeFileAtomic(dir / u8path(sb.tex + ".bntx"), game->readTex(t));
                    nt++;
                } catch (...) {}
            }
        say("Modèle décompressé écrit dans Documents\\Triforce-modkit\\analyse\\" + name + (nt ? fmt(" (et %d texture(s))", nt) : ""));
    } catch (const std::exception& e) {
        say(std::string("Export impossible : ") + e.what(), true);
    }
}

TexSlot* Editor::texture(const std::string& name) {
    auto it = texSlots.find(name);
    if (it != texSlots.end()) return &it->second;
    if (!game) return nullptr;
    TexSlot& s = texSlots[name];
    Game* g = game.get();
    jobs.add(2, [this, name, g]() -> std::function<void()> {
        struct R { TexData t; Bytes rgba; std::string why, standIn; AstcStats st; int kind = 0; };
        auto r = std::make_shared<R>();
        try {
            std::string rel = g->texFile(name), want = name;
            if (rel.empty()) {
                // (absente du jeu : une voisine de la même famille, signalée comme remplaçante)
                auto [sn, sf] = g->similarTex(name);
                if (!sn.empty()) { rel = sf; want = sn; r->standIn = sn; }
            }
            if (rel.empty()) {
                auto found = g->findAnywhere(name);
                std::string where;
                for (auto& f : found) where += (where.empty() ? "" : ", ") + f;
                throw Error("introuvable : ni Tex/" + name + ".bntx.zs, ni dans un autre fichier de Tex/" +
                            (where.empty() ? std::string(" ; aucun fichier du jeu ne porte ce nom") : " ; fichiers à ce nom : " + where));
            }
            r->t = bntxExtract(g->readTex(rel), 512, want);
            if (r->t.code == 5 || r->t.code == 6) r->kind = 2;   // (BC4, BC5 : masques ou reliefs, pas des couleurs)
            else if (r->t.code == 8) throw Error("format BC6H (HDR) non affiché");
            else if (r->t.code >= 20) {
                int i = r->t.code - 20;
                r->rgba = astcDecode(r->t.blocks.data(), r->t.blocks.size(), r->t.w, r->t.h, ASTC_DIMS[i][0], ASTC_DIMS[i][1], r->t.srgb, true, true, &r->st);
            }
        } catch (const std::exception& e) {
            r->why = e.what();
        }
        return [this, name, r] {
            auto it2 = texSlots.find(name);
            if (it2 == texSlots.end()) return;
            TexSlot& s2 = it2->second;
            s2.format = r->t.format;
            s2.w = r->t.w;
            s2.h = r->t.h;
            s2.invalid = r->st.n;
            s2.empty = r->st.empty;
            s2.reasons = r->st.why;
            s2.standIn = r->standIn;
            if (!r->why.empty()) { s2.st = TexSlot::None; s2.why = r->why; return; }
            bool mask = r->kind == 2;   // (BC4, BC5 : chargées quand même, pour les normales de l'eau)
            VkComponentSwizzle swz[4];
            static const VkComponentSwizzle MAP[6] = {VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE, VK_COMPONENT_SWIZZLE_R,
                                                      VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
            for (int k = 0; k < 4; k++) swz[k] = MAP[std::min<uint32_t>(5, (r->t.compSel >> (8 * k)) & 0xFF)];
            VkFormat f;
            const void* data = r->t.blocks.data();
            size_t size = r->t.blocks.size();
            bool mips = false;
            switch (r->t.code) {
                case 1: f = VK_FORMAT_R8G8B8A8_UNORM; mips = true; break;
                case 2: f = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
                case 3: f = VK_FORMAT_BC2_UNORM_BLOCK; break;
                case 4: f = VK_FORMAT_BC3_UNORM_BLOCK; break;
                case 5: f = r->t.format.find("sign") != std::string::npos ? VK_FORMAT_BC4_SNORM_BLOCK : VK_FORMAT_BC4_UNORM_BLOCK; break;
                case 6: f = r->t.format.find("sign") != std::string::npos ? VK_FORMAT_BC5_SNORM_BLOCK : VK_FORMAT_BC5_UNORM_BLOCK; break;
                case 7: f = VK_FORMAT_BC7_UNORM_BLOCK; break;
                default: f = VK_FORMAT_R8G8B8A8_UNORM; data = r->rgba.data(); size = r->rgba.size(); mips = true; break;
            }
            if (r->t.code >= 2 && r->t.code <= 7 && !(vk.bc && vk.formatOk(f))) {
                if (mask) { s2.st = TexSlot::Mask; s2.why = "masque ou relief (" + r->t.format + ")"; return; }
                s2.st = TexSlot::None;
                s2.why = "format " + r->t.format + " refusé par la carte graphique";
                return;
            }
            try {
                s2.tex = vk.texture(f, r->t.w, r->t.h, data, size, swz, mips);
                s2.st = mask ? TexSlot::Mask : TexSlot::Ok;
                if (mask) s2.why = "masque ou relief (" + r->t.format + ")";
            } catch (const std::exception& e) {
                s2.st = TexSlot::None;
                s2.why = e.what();
            }
        };
    });
    return &s;
}

// ---------------------------------------------------------------- recherche approfondie des textures
std::vector<std::string> Editor::missingTextures() const {
    std::vector<std::string> v;
    for (auto& [n, t] : texSlots)
        if ((t.st == TexSlot::None && t.why.rfind("introuvable", 0) == 0) || (t.st == TexSlot::Ok && !t.standIn.empty())) v.push_back(n);
    return v;
}

void Editor::startTexSearch() {
    if (!game || (texSearch && !texSearch->finished)) return;
    auto names = missingTextures();
    if (names.empty()) { say("Aucune texture introuvable à chercher"); return; }
    if (searchThread_.joinable()) searchThread_.join();
    texSearch = std::make_shared<TexSearch>();
    texSearch->names = names;
    texSearchReport.clear();
    auto s = texSearch;
    Game* g = game.get();
    int th = int(std::max(2u, std::min(12u, std::thread::hardware_concurrency())));
    searchThread_ = std::thread([g, s, th] { try { searchTextures(*g, *s, th); } catch (...) { s->finished = true; } });
    say(fmt("Recherche de %zu textures dans tout le jeu…", names.size()));
}

bool Editor::pollTexSearch() {
    if (!texSearch || !texSearch->finished || !texSearchReport.empty()) return false;
    if (searchThread_.joinable()) searchThread_.join();
    int got = 0;
    {
        std::lock_guard<std::mutex> lk(texSearch->m);
        texSearchReport = texSearch->report();
        for (size_t k = 0; k < texSearch->names.size(); k++)
            if (k < texSearch->usable.size() && !texSearch->usable[k].empty()) {
                game->addFound(texSearch->names[k], texSearch->usable[k][0]);
                texSlots.erase(texSearch->names[k]);   // (rechargée avec la vraie texture)
                got++;
            }
    }
    fs::path dir = homeDir() / u8path("Documents") / u8path("Triforce-modkit") / u8path("analyse");
    std::error_code ec;
    fs::create_directories(dir, ec);
    try { writeFileAtomic(dir / u8path("recherche-textures.txt"), Bytes(texSearchReport.begin(), texSearchReport.end())); } catch (...) {}
    say(fmt("Recherche terminée : %d texture(s) retrouvée(s) sur %zu", got, texSearch->names.size()), got == 0);
    return true;
}

// ---------------------------------------------------------------- terrain
std::string Editor::autoTerrain() const {
    if (scenes.empty() || !hasMap) return "";
    auto clean = [](const std::string& s) { std::string o; for (char c : lower(s)) if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o += c; return o; };
    std::string p = clean(doc.path);
    auto lcs = [](const std::string& a, const std::string& b) {
        size_t best = 0;
        std::vector<size_t> prev(b.size() + 1, 0), cur(b.size() + 1, 0);
        for (size_t i = 1; i <= a.size(); i++) {
            for (size_t j = 1; j <= b.size(); j++) { cur[j] = a[i - 1] == b[j - 1] ? prev[j - 1] + 1 : 0; best = std::max(best, cur[j]); }
            std::swap(prev, cur);
        }
        return best;
    };
    std::string best;
    double bs = 0.75;
    for (auto& s : scenes) {
        std::string k = clean(s);
        if (k.size() < 5) continue;
        double sco = double(lcs(p, k)) / double(k.size());
        if (sco > bs) { bs = sco; best = s; }
    }
    return best;
}

// (les matières du terrain ne sont pas encore reliées à leurs textures : une teinte naturelle par numéro de matière,
// prise dans une palette d'herbes, de terres et de sables ; le shader ajoute la roche dans les pentes)
static std::array<float, 3> matColor(int m, float) {
    static const float P[10][3] = {{0.34f, 0.43f, 0.22f}, {0.29f, 0.39f, 0.20f}, {0.40f, 0.46f, 0.26f}, {0.45f, 0.38f, 0.27f},
                                   {0.38f, 0.32f, 0.24f}, {0.52f, 0.47f, 0.36f}, {0.31f, 0.36f, 0.24f}, {0.42f, 0.42f, 0.30f},
                                   {0.36f, 0.30f, 0.22f}, {0.47f, 0.50f, 0.32f}};
    uint32_t k = uint32_t(m) * 2654435761u;
    const float* c = P[(k >> 16) % 10];
    return {c[0], c[1], c[2]};
}

// Textures du sol : Tex/MaterialAlb.bntx.zs, une texture en tableau dont la couche k est la matière k des .mate.
// Lue une fois (niveau de mip ≤ 512 pixels), décodée sur le processeur si ASTC. La table des matières du .tscb
// donne pour chaque numéro sa couche et ses répétitions par mètre.
void Editor::loadTerrainMaterials() {
    if (terrainMatAsked || !game) return;
    terrainMatAsked = true;
    terrainMatStatus = "textures du sol : chargement…";
    Game* g = game.get();
    // (MaterialAlb : couleurs ; MaterialCmb : relief, en R et G une normale, en B une hauteur ; facultatif)
    for (const char* name : {"MaterialAlb", "MaterialCmb"}) {
        std::string nm = name;
        bool cmb = nm == "MaterialCmb";
        jobs.add(0, [this, g, nm, cmb]() -> std::function<void()> {
            struct R { int w = 0, h = 0, layers = 0; VkFormat f = VK_FORMAT_R8G8B8A8_UNORM; Bytes data; std::string why; };
            auto r = std::make_shared<R>();
            try {
                std::string rel = g->texFile(nm);
                if (rel.empty()) throw Error("Tex/" + nm + ".bntx.zs introuvable");
                Bytes raw = g->read(rel);
                TexData t0 = bntxExtract(raw, 512, "", 0);
                r->layers = t0.layers;
                r->w = t0.w;
                r->h = t0.h;
                bool astc = t0.code >= 20;
                if (!astc) {
                    switch (t0.code) {
                        case 1: r->f = VK_FORMAT_R8G8B8A8_UNORM; break;
                        case 2: r->f = VK_FORMAT_BC1_RGBA_UNORM_BLOCK; break;
                        case 3: r->f = VK_FORMAT_BC2_UNORM_BLOCK; break;
                        case 4: r->f = VK_FORMAT_BC3_UNORM_BLOCK; break;
                        case 7: r->f = VK_FORMAT_BC7_UNORM_BLOCK; break;
                        default: throw Error("format " + t0.format + " non pris en charge");
                    }
                }
                for (int L = 0; L < r->layers; L++) {
                    TexData t = L ? bntxExtract(raw, 512, "", L) : t0;
                    if (t.w != r->w || t.h != r->h) throw Error("couches de tailles différentes");
                    if (astc) {
                        int i = t.code - 20;
                        Bytes px = astcDecode(t.blocks.data(), t.blocks.size(), t.w, t.h, ASTC_DIMS[i][0], ASTC_DIMS[i][1], t.srgb, true, true, nullptr);
                        r->data.insert(r->data.end(), px.begin(), px.end());
                    } else {
                        r->data.insert(r->data.end(), t.blocks.begin(), t.blocks.end());
                    }
                }
                r->why = fmt("%s : %d matières %dx%d (%s)", cmb ? "relief du sol" : "textures du sol", r->layers, r->w, r->h, t0.format.c_str());
            } catch (const std::exception& e) {
                r->layers = 0;
                r->why = std::string(cmb ? "relief du sol : " : "textures du sol : ") + e.what();
            }
            return [this, r, cmb] {
                std::string& st = cmb ? terrainCmbStatus : terrainMatStatus;
                st = r->why;
                if (!r->layers) return;
                if (r->f != VK_FORMAT_R8G8B8A8_UNORM && !(vk.bc && vk.formatOk(r->f))) { st += " ; format refusé par la carte graphique"; return; }
                try {
                    rend.setTerrainMaterials(r->f, r->w, r->h, r->layers, r->data.data(), r->data.size(), cmb);
                } catch (const std::exception& e) {
                    st = std::string(cmb ? "relief du sol : " : "textures du sol : ") + e.what();
                }
            };
        });
    }
}

void Editor::setTerrain(const std::string& scene) {
    loadTerrainMaterials();
    terrainScene = scene;
    tiles.clear();
    rend.setTerrain({}, {});
    if (scene.empty() || !game) { terrainStatus = scene.empty() ? "aucun terrain" : ""; return; }
    terrainStatus = "chargement…";
    Game* g = game.get();
    jobs.add(0, [this, scene, g]() -> std::function<void()> {
        auto tl = std::make_shared<std::vector<TerrainTile>>();
        auto vv = std::make_shared<std::vector<TerrVert>>();
        auto ii = std::make_shared<std::vector<uint32_t>>();
        auto ids = std::make_shared<std::vector<uint8_t>>(), wts = std::make_shared<std::vector<uint8_t>>();
        auto grid = std::make_shared<int>(0);
        auto table = std::make_shared<std::vector<std::array<float, 3>>>();
        std::string why;
        try {
            for (auto& m : terrainMaterials(*g, scene)) table->push_back({float(m.layer), m.u, m.v});
            *tl = terrainBuild(*g, scene);
            bool allMat = !tl->empty();
            for (auto& t : *tl) { *grid = std::max(*grid, t.n); if (t.ids.empty()) allMat = false; }
            if (allMat) {
                size_t G = size_t(*grid);
                ids->assign(G * G * 4 * tl->size(), 127);
                wts->assign(ids->size(), 0);
                for (size_t k = 0; k < tl->size(); k++) {
                    auto& t = (*tl)[k];
                    for (int y = 0; y < t.n; y++)
                        for (int x = 0; x < t.n; x++)
                            for (int c = 0; c < 4; c++) {
                                size_t dst = ((k * G + size_t(y)) * G + size_t(x)) * 4 + size_t(c), src = (size_t(y * t.n + x)) * 4 + size_t(c);
                                (*ids)[dst] = t.ids[src];
                                (*wts)[dst] = t.wts[src];
                            }
                }
            } else {
                ids->clear();
                wts->clear();
            }
            for (size_t ti = 0; ti < tl->size(); ti++) {
                auto& t = (*tl)[ti];
                uint32_t base = uint32_t(vv->size());
                int n = t.n;
                for (int y = 0; y < n; y++)
                    for (int x = 0; x < n; x++) {
                        float h = t.h[size_t(y * n + x)];
                        float px = t.cx - t.side / 2 + t.side * x / (n - 1), pz = t.cz - t.side / 2 + t.side * y / (n - 1);
                        auto c = matColor(t.mat[size_t(y * n + x)], std::isfinite(h) ? h : 0);
                        vv->push_back({{px, std::isfinite(h) ? h : 0.f, pz}, {c[0], c[1], c[2], float(n)},
                                       {float(x) / float(n - 1), float(y) / float(n - 1), float(ti)}});
                    }
                for (int y = 0; y + 1 < n; y++)
                    for (int x = 0; x + 1 < n; x++) {
                        uint32_t a = base + uint32_t(y * n + x), b = a + 1, c2 = a + uint32_t(n), d = c2 + 1;
                        if (!std::isfinite(t.h[size_t(y * n + x)]) || !std::isfinite(t.h[size_t(y * n + x + 1)]) ||
                            !std::isfinite(t.h[size_t((y + 1) * n + x)]) || !std::isfinite(t.h[size_t((y + 1) * n + x + 1)]))
                            continue;
                        for (uint32_t k : {a, c2, b, b, c2, d}) ii->push_back(k);
                    }
            }
        } catch (const std::exception& e) {
            why = e.what();
        }
        return [this, scene, tl, vv, ii, ids, wts, grid, table, why] {
            if (terrainScene != scene) return;
            if (!why.empty()) { terrainStatus = "terrain illisible : " + why; return; }
            size_t nt = tl->size();
            tiles = std::move(*tl);
            rend.setTerrain(*vv, *ii, *ids, *wts, *grid, int(nt));
            rend.setTerrainTable(*table);
            terrainMatTable = table->size();
            terrainStatus = fmt("%zu tuiles", tiles.size());
        };
    });
}

bool Editor::groundAt(double x, double z, double& h) const {
    const TerrainTile* best = nullptr;
    for (auto& t : tiles)
        if (std::fabs(x - t.cx) <= t.side / 2 && std::fabs(z - t.cz) <= t.side / 2 && (!best || t.side < best->side)) best = &t;
    if (!best) return false;
    const TerrainTile& t = *best;
    double fx = (x - (t.cx - t.side / 2)) / t.side * (t.n - 1), fz = (z - (t.cz - t.side / 2)) / t.side * (t.n - 1);
    int ix = std::clamp(int(fx), 0, t.n - 2), iz = std::clamp(int(fz), 0, t.n - 2);
    double ax = fx - ix, az = fz - iz;
    auto H = [&](int a, int b) { return double(t.h[size_t(b * t.n + a)]); };
    double v = H(ix, iz) * (1 - ax) * (1 - az) + H(ix + 1, iz) * ax * (1 - az) + H(ix, iz + 1) * (1 - ax) * az + H(ix + 1, iz + 1) * ax * az;
    if (!std::isfinite(v)) return false;
    h = v;
    return true;
}

std::vector<std::string> Editor::logCopy() {
    std::lock_guard<std::mutex> lk(logM_);
    return buildLog;
}

// ---------------------------------------------------------------- messages, construction
void Editor::say(const std::string& s, bool warn) {
    toast = s;
    toastWarn = warn;
    toastUntil = now() + (warn ? 9 : 5);
}

void Editor::buildMod() {
    if (building || !game) return;
    if (buildThread_.joinable()) buildThread_.join();
    {
        std::lock_guard<std::mutex> lk(logM_);
        buildLog.clear();
    }
    building = true;
    saveSettings();
    Game* g = game.get();
    fs::path work = folders.work, mods = folders.mods;
    std::string name = modName;
    buildThread_ = std::thread([this, g, work, mods, name] {
        try {
            te::buildMod(*g, work, mods, name, [this](const std::string& line) {
                std::lock_guard<std::mutex> lk(logM_);
                buildLog.push_back(line);
            });
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(logM_);
            buildLog.push_back(std::string("ERREUR : ") + e.what());
        }
        building = false;
    });
}

}  // namespace te
