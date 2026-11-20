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
    fxSlots.clear();
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
        try { bake = bakeLoad(*game, bakePrefix(rel), &bakeStatus); } catch (const std::exception& e) { bake.clear(); bakeStatus = e.what(); }
        // (feuillage de la scène : lu à part, puis rangé par modèle avec la rotation de chaque plante)
        foliage.clear();
        foliageStatus = "feuillage : lecture…";
        ground.clear();
        lights.clear();
        groundStatus = "couverture du sol : lecture…";
        lightsStatus = "lumières de la scène : lecture…";
        {
            Game* g = game.get();
            std::string pre = bakePrefix(rel), forMap = rel;
            jobs.add(1, [this, g, pre, forMap]() -> std::function<void()> {
                auto gl = std::make_shared<std::vector<GroundLayer>>();
                auto li = std::make_shared<std::vector<SceneLight>>();
                auto st = std::make_shared<std::string>(), sl = std::make_shared<std::string>();
                try { *gl = groundLoad(*g, pre, st.get()); } catch (const std::exception& e) { *st = std::string("couverture du sol : ") + e.what(); }
                try { *li = lightsLoad(*g, pre, sl.get()); } catch (const std::exception& e) { *sl = std::string("lumières de la scène : ") + e.what(); }
                return [this, gl, li, st, sl, forMap] {
                    if (doc.path != forMap) return;
                    ground = std::move(*gl);
                    lights = std::move(*li);
                    groundStatus = *st;
                    lightsStatus = *sl;
                };
            });
        }
        {
            Game* g = game.get();
            std::string pre = bakePrefix(rel), forMap = rel;
            jobs.add(1, [this, g, pre, forMap]() -> std::function<void()> {
                auto out = std::make_shared<std::vector<FolDraw>>();
                auto st = std::make_shared<std::string>();
                try {
                    FoliageSet fs = foliageLoad(*g, pre, st.get());
                    std::map<std::string, size_t> at;
                    for (auto& t : fs.types) {
                        if (t.inst.empty()) continue;
                        auto [it, fresh] = at.emplace(t.model, out->size());
                        if (fresh) out->push_back({t.model, {}, {}});
                        FolDraw& d = (*out)[it->second];
                        for (auto& f : t.inst) {
                            V3 X(f.x[0], f.x[1], f.x[2]), Y(f.y[0], f.y[1], f.y[2]), Z = cross(X, Y);
                            M3 M{{{X.x, Y.x, Z.x}, {X.y, Y.y, Z.y}, {X.z, Y.z, Z.z}}};
                            d.posRot.push_back({V3(f.p[0], f.p[1], f.p[2]), matEuler(M, nullptr)});
                            d.scale.push_back(f.s);
                        }
                    }
                } catch (const std::exception& e) { *st = std::string("feuillage : ") + e.what(); }
                return [this, out, st, forMap] {
                    if (doc.path != forMap) return;
                    foliage = std::move(*out);
                    foliageStatus = *st;
                };
            });
        }
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
    struct Late { const GpuMesh* g; PC pc; std::vector<Renderer::SubTex> tex; };
    rend.fx.clear();
    rend.fxBatches.clear();
    static const auto fxT0 = std::chrono::steady_clock::now();
    float fxTime = std::fmod(std::chrono::duration<float>(std::chrono::steady_clock::now() - fxT0).count(), 3600.f);
    V3 camR(view.m[0], view.m[4], view.m[8]), camU(view.m[1], view.m[5], view.m[9]), eye = cam.eye();
    std::vector<Late> late;   // (parties transparentes, dessinées après tout le reste)
    if (hasMap) {
        rend.bindMesh(c);
        std::vector<Renderer::SubTex> sets;
        auto ready = [&](const std::string& n, bool optional = false) -> VkDescriptorSet {
            if (n.empty()) return VK_NULL_HANDLE;
            TexSlot* t = texture(n, optional);
            return t && t->st == TexSlot::Ok ? t->tex.set : VK_NULL_HANDLE;
        };
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
                // (effets de particules : dessinés en plus de la boîte, s'ils sont assez près)
                ModelSlot* ms = showModels && !name.empty() ? model(name) : nullptr;
                // (effets permanents : emplacements EftLoc, et objets dont l'effet tourne en continu — insectes, bougies…)
                if (showEffects && showModels && !name.empty() && len(p - eye) < 220 && rend.fx.size() < 120000) {
                    bool place = name.rfind("EftLoc", 0) == 0;
                    if (place || len(p - eye) < 120)
                        if (FxSlot* fs = effect(name); fs && fs->ready && fs->info.ambient && (place || !fs->info.file.empty()))
                            emitFx(*fs, p, eulerMat(ro), s, uint32_t(reinterpret_cast<uintptr_t>(r.get()) >> 4), fxTime, camR, camU,
                                   !place, ms && ms->st == ModelSlot::Ok ? ms->gpu.bbox : nullptr);
                }
                const GpuMesh* g = &rend.cube;
                const float* bb = rend.cube.bbox;
                if (ms && ms->st == ModelSlot::Ok) {
                    // (parties sans texture d'un vrai modèle : gris clair, à peine teinté par la catégorie ; la couleur
                    // franche reste aux boîtes)
                    for (int k = 0; k < 3; k++) pc.color[k] = 0.72f + 0.2f * (cc[k] - 0.72f);
                    g = &ms->gpu;
                    bb = g->bbox;
                    sets.assign(g->subs.size(), Renderer::SubTex{});
                    for (size_t i = 0; i < g->subs.size(); i++) {
                        if (!showTextures || g->subs[i].tex.empty()) continue;
                        auto& s = g->subs[i];
                        sets[i] = {ready(s.tex), ready(s.tex2, true), ready(s.detail, true)};
                    }
                    // (éclairage précalculé de cet objet, d'après son Hash : la carte de sa case et la place de chaque partie)
                    Renderer::Bake bk;
                    const Renderer::Bake* bkp = nullptr;
                    if (showBake && showTextures && !bake.empty())
                        if (const Value* hv = r->value.get("Hash"))
                            if (hv->t == T::U64 || hv->t == T::U32 || hv->t == T::I64) {
                                uint64_t h = hv->t == T::U64 ? hv->u : uint64_t(hv->i);
                                auto it = bake.find(h);
                                TexSlot* lt = it != bake.end() ? texture(it->second.tex, true) : nullptr;
                                // (cartes d'éclairage en BC5 : rangées comme « masques », mais bien utilisables ici)
                                if (lt && (lt->st == TexSlot::Ok || lt->st == TexSlot::Mask) && lt->tex.set) bk.map = lt->tex.set;
                                if (bk.map) {
                                    bk.sub.assign(g->subs.size(), {0, 0, 0, 0});
                                    for (size_t i = 0; i < g->subs.size(); i++)
                                        for (auto& bm : it->second.mats)
                                            if (bm.material == g->subs[i].mat || (bm.material.empty() && bm.index == g->subs[i].matIndex)) {
                                                bk.sub[i] = {bm.off[0], bm.off[1], bm.scale[0], bm.scale[1]};
                                                break;
                                            }
                                    bkp = &bk;
                                }
                            }
                    rend.drawMesh(c, *g, pc, sets, false, bkp);
                    if (g->translucent) late.push_back({g, pc, sets});
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
    // (feuillage de la scène : plantes et branches basses, près de la caméra seulement)
    if (hasMap && showFoliage && showModels && !foliage.empty()) {
        rend.bindMesh(c);
        std::vector<Renderer::SubTex> sets;
        auto ready = [&](const std::string& n, bool optional = false) -> VkDescriptorSet {
            if (n.empty()) return VK_NULL_HANDLE;
            TexSlot* t = texture(n, optional);
            return t && t->st == TexSlot::Ok ? t->tex.set : VK_NULL_HANDLE;
        };
        for (auto& f : foliage) {
            ModelSlot* ms = model(f.model);
            if (!ms || ms->st != ModelSlot::Ok) continue;
            const GpuMesh* g = &ms->gpu;
            sets.assign(g->subs.size(), Renderer::SubTex{});
            for (size_t i = 0; i < g->subs.size(); i++) {
                if (!showTextures || g->subs[i].tex.empty()) continue;
                auto& sb = g->subs[i];
                sets[i] = {ready(sb.tex), ready(sb.tex2, true), ready(sb.detail, true)};
            }
            for (size_t k = 0; k < f.posRot.size(); k++) {
                const V3& p = f.posRot[k].first;
                if (len(p - eye) > 180) continue;
                PC pc = base;
                pc.pos[0] = float(p.x); pc.pos[1] = float(p.y); pc.pos[2] = float(p.z); pc.pos[3] = 0;
                pc.rot[0] = float(f.posRot[k].second.x); pc.rot[1] = float(f.posRot[k].second.y); pc.rot[2] = float(f.posRot[k].second.z);
                pc.scale[0] = pc.scale[1] = pc.scale[2] = f.scale[k];
                pc.color[0] = pc.color[1] = pc.color[2] = 0.72f; pc.color[3] = 1;
                rend.drawMesh(c, *g, pc, sets, false, nullptr);
                if (g->translucent) late.push_back({g, pc, sets});
            }
        }
    }
    if (hasMap && showGround && showModels && !ground.empty()) emitGround(cam.eye(), camR, camU);
    if (hasMap && showLights && !lights.empty()) emitLights(cam.eye(), camR, camU, fxTime);
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
            TexSlot* t = texture(n, true);
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
        for (auto& l : late) rend.drawMesh(c, *l.g, l.pc, l.tex, true);
    }
    rend.drawFx(c, base);
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

// ---------------------------------------------------------------- couverture du sol et lumières de la scène
static void fxQuad(std::vector<FxVert>& fx, const V3& c, const V3& ax, const V3& ay, float r, float g, float b, float a) {
    if (a <= 0.003f) return;
    V3 q[4] = {c - ax - ay, c + ax - ay, c + ax + ay, c - ax + ay};
    static const float UV[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int k : {0, 1, 2, 0, 2, 3}) fx.push_back({{float(q[k].x), float(q[k].y), float(q[k].z)}, {UV[k][0], UV[k][1]}, {r, g, b, a}});
}
static float hash01(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return float(h & 0xFFFFFF) / float(0x1000000);
}

// Mousse (taches posées à plat sur la surface), gazon et sous-bois (touffes de brins), cailloux (petits galets),
// aux points que le jeu donne, près de la caméra seulement : les couches comptent jusqu'à 1,7 million de points.
void Editor::emitGround(const V3& eye, const V3& camR, const V3& camU) {
    V3 view = norm(cross(camU, camR));
    for (auto& L : ground) {
        std::string ln = lower(L.name);
        bool moss = ln.find("moss") != std::string::npos, turf = ln.find("turf") != std::string::npos, stone = ln.find("stone") != std::string::npos;
        bool blades = turf || (!moss && !stone);   // (sous-bois et couches inconnues : touffes)
        double R = moss ? 28 : turf ? 45 : stone ? 35 : 35;
        size_t budget = moss ? 45000 : 25000, used = 0;
        FxBatch b;
        b.first = uint32_t(rend.fx.size());
        b.mode = blades ? 7 : 0;
        // (textures du jeu pour ces couches, quand elles se lisent : les modèles Foliage.Foliage*.bfres ne sont que des
        // boîtes vides, la forme vient du nuanceur ; seules leurs textures disent l'aspect)
        const char* tn = moss ? "Foliage_Moss_Alb" : turf ? "Foliage_Turf_Alb" : stone ? "Foliage_Stone_Alb" : "Foliage_ShootLeaf_Alb";
        if (showTextures)
            if (TexSlot* t = texture(tn, true); t && t->st == TexSlot::Ok && t->tex.set) { b.tex = t->tex.set; b.mode = 1; }
        bool textured = b.tex != VK_NULL_HANDLE;
        auto tint = [&](float r, float g, float bl, float v, float out[3]) {
            if (textured) { out[0] = out[1] = out[2] = std::min(1.f, v); }
            else { out[0] = r * v; out[1] = g * v; out[2] = bl * v; }
        };
        float col[3];
        for (auto& cell : L.cells) {
            V3 cc(cell.c[0], cell.c[1], cell.c[2]);
            if (len(cc - eye) > R + 3.5) continue;
            for (uint32_t k = cell.begin; k < cell.end && used < budget; k++) {
                const GroundPt& g = L.pts[k];
                V3 p(g.p[0], g.p[1], g.p[2]), up(g.n[0], g.n[1], g.n[2]);
                double dist = len(p - eye);
                if (dist > R) continue;
                float fade = float(std::clamp((R - dist) / (R * 0.2), 0.0, 1.0));
                float r1 = hash01(k, 1), r2 = hash01(k, 2);
                used++;
                if (moss || stone) {
                    // (à plat sur la surface : deux axes perpendiculaires à sa normale)
                    V3 t1 = std::fabs(up.y) < 0.9 ? norm(cross(up, V3(0, 1, 0))) : norm(cross(up, V3(1, 0, 0)));
                    V3 t2 = cross(up, t1);
                    double ang = r1 * 6.283, ca = std::cos(ang), sa = std::sin(ang);
                    V3 a1 = t1 * ca + t2 * sa, a2 = t2 * ca - t1 * sa;
                    if (moss) {
                        double sz = 0.16 + g.a4 / 255.0 * 0.7 + r2 * 0.08;
                        float v = 0.8f + 0.35f * r2;
                        tint(0.23f, 0.33f, 0.1f, v, col);
                        fxQuad(rend.fx, p + up * 0.015, a1 * sz, a2 * sz, col[0], col[1], col[2], (textured ? 1.f : 0.85f) * fade);
                    } else {
                        double sz = 0.04 + g.a4 / 255.0 * 0.14;
                        float v = 0.75f + 0.4f * r2;
                        tint(0.45f, 0.43f, 0.39f, v, col);
                        fxQuad(rend.fx, p + up * (sz * 0.4), a1 * sz, a2 * (sz * 0.8), col[0], col[1], col[2], fade);
                    }
                } else {
                    // (touffe : quelques brins inclinés, tournés vers la caméra autour de leur axe)
                    int nb = turf ? 7 : 5;
                    double h0 = turf ? 0.22 : 0.3 + g.kind * 0.02;
                    for (int j = 0; j < nb; j++) {
                        float q1 = hash01(k, 10 + j), q2 = hash01(k, 30 + j), q3 = hash01(k, 50 + j);
                        V3 off = V3((q1 - 0.5) * 0.25, 0, (q2 - 0.5) * 0.25);
                        V3 bu = norm(up + V3((q1 - 0.5) * 0.6, 0, (q3 - 0.5) * 0.6));
                        double h = h0 * (0.6 + 0.8 * q3);
                        V3 side = cross(bu, view);
                        if (len(side) < 1e-3) side = camR;
                        side = norm(side) * (turf ? 0.018 : 0.035);
                        float v = 0.75f + 0.4f * q2;
                        float rr = turf ? 0.33f : 0.3f + 0.08f * float(g.kind % 3), gg = turf ? 0.5f : 0.45f, bb = turf ? 0.14f : 0.13f;
                        tint(rr, gg, bb, v, col);
                        fxQuad(rend.fx, p + off + bu * (h * 0.5), textured ? side * 1.6 : side, bu * (h * 0.5), col[0], col[1], col[2], fade);
                    }
                }
            }
        }
        b.count = uint32_t(rend.fx.size()) - b.first;
        if (b.count) rend.fxBatches.push_back(b);
    }
}

// Lumières de la scène : un halo de leur couleur (taille selon leur puissance) ; projecteurs avec leur faisceau ;
// lumières rectangulaires (ouvertures, fenêtres) en voile lumineux. Elles n'éclairent pas encore les modèles.
void Editor::emitLights(const V3& eye, const V3& camR, const V3& camU, float time) {
    V3 viewDir = norm(cross(camU, camR));
    FxBatch b;
    b.first = uint32_t(rend.fx.size());
    b.additive = true;
    b.mode = 0;
    FxBatch beams;
    std::vector<FxVert> beamV;
    for (size_t i = 0; i < lights.size(); i++) {
        auto& L = lights[i];
        V3 p(L.p[0], L.p[1], L.p[2]);
        if (len(p - eye) > 160) continue;
        float flick = 0.92f + 0.08f * float(std::sin(time * 2.3 + i * 1.7));
        if (L.kind == SceneLight::Rect) {
            V3 d = norm(V3(L.dir[0], L.dir[1], L.dir[2])), u = norm(V3(L.up[0], L.up[1], L.up[2]));
            V3 s = norm(cross(u, d));
            fxQuad(rend.fx, p, s * (L.a * 0.5), u * (L.b * 0.5), L.col[0], L.col[1], L.col[2], 0.07f);
            continue;
        }
        double sz = 0.25 + std::sqrt(std::max(0.f, L.intensity)) * (L.kind == SceneLight::Spot ? 0.0006 : 0.012);
        sz = std::min(sz, 2.5);
        fxQuad(rend.fx, p, camR * sz, camU * sz, L.col[0], L.col[1], L.col[2], 0.45f * flick);
        fxQuad(rend.fx, p, camR * (sz * 0.18), camU * (sz * 0.18), 1.f, 1.f, 1.f, 0.9f);
        if (L.kind == SceneLight::Spot) {
            V3 d = norm(V3(L.dir[0], L.dir[1], L.dir[2]));
            double lenB = 3.5, w = std::tan(std::clamp(double(L.b), 5.0, 60.0) * 3.14159 / 180) * lenB * 0.5;
            V3 side = cross(d, viewDir);
            if (len(side) < 1e-3) side = camR;
            fxQuad(beamV, p + d * (lenB * 0.5), norm(side) * w, d * (lenB * 0.5), L.col[0], L.col[1], L.col[2], 0.12f);
        }
    }
    b.count = uint32_t(rend.fx.size()) - b.first;
    if (b.count) rend.fxBatches.push_back(b);
    if (!beamV.empty()) {
        beams.first = uint32_t(rend.fx.size());
        beams.additive = true;
        beams.mode = 4;
        rend.fx.insert(rend.fx.end(), beamV.begin(), beamV.end());
        beams.count = uint32_t(beamV.size());
        rend.fxBatches.push_back(beams);
    }
}

// ---------------------------------------------------------------- effets de particules
Editor::FxSlot* Editor::effect(const std::string& actor) {
    auto it = fxSlots.find(actor);
    if (it != fxSlots.end()) return &it->second;
    if (!game) return nullptr;
    FxSlot& s = fxSlots[actor];
    Game* g = game.get();
    ModelIndex* mi = models.get();
    jobs.add(1, [this, actor, g, mi]() -> std::function<void()> {
        auto e = std::make_shared<EffectInfo>();
        std::string model;
        if (mi) try { if (auto inf = mi->info(actor)) model = inf->first; } catch (...) {}
        try { *e = effectLoad(*g, actor, model); } catch (const std::exception& x) { e->actor = actor; e->why = x.what(); }
        return [this, actor, e] {
            auto it2 = fxSlots.find(actor);
            if (it2 == fxSlots.end()) return;
            it2->second.info = *e;
            it2->second.ready = true;
        };
    });
    return &s;
}

static float fxRand(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return float(h & 0xFFFFFF) / float(0x1000000);
}

// Particules d'un effet placé, recalculées à chaque image d'après le temps (sans état : chaque particule suit un
// cycle de vie qui recommence, décalé au hasard). Le mouvement suit la famille de l'effet ; tailles en mètres,
// volumes multipliés par l'échelle de l'acteur, orientés comme lui.
void Editor::emitFx(const FxSlot& f, const V3& p, const M3& R, const V3& s, uint32_t seed, float time, const V3& camR, const V3& camU,
                    bool attached, const float* bbox) {
    const EffectInfo& e = f.info;
    auto tex = [&](const std::string& n) -> VkDescriptorSet {
        if (n.empty() || !showTextures) return VK_NULL_HANDLE;
        TexSlot* t = texture(n, true);
        return t && (t->st == TexSlot::Ok || t->st == TexSlot::Mask) && t->tex.set ? t->tex.set : VK_NULL_HANDLE;
    };
    FxBatch b;
    b.first = uint32_t(rend.fx.size());
    auto quad = [&](const V3& c, const V3& ax, const V3& ay, float r, float g, float bl, float a) {
        if (a <= 0.002f) return;
        V3 q[4] = {c - ax - ay, c + ax - ay, c + ax + ay, c - ax + ay};
        static const float UV[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int k : {0, 1, 2, 0, 2, 3})
            rend.fx.push_back({{float(q[k].x), float(q[k].y), float(q[k].z)}, {UV[k][0], UV[k][1]}, {r, g, bl, a}});
    };
    auto local = [&](double x, double y, double z) { return p + matVec(R, V3(x * s.x, y * s.y, z * s.z)); };
    auto bill = [&](const V3& c, double size, double ang, double squash = 1) {
        double ca = std::cos(ang), sa = std::sin(ang);
        V3 ax = (camR * ca + camU * sa) * (size * squash), ay = (camU * ca - camR * sa) * size;
        return std::make_pair(ax, ay);
    };
    auto fade = [](float f, float in, float out) { return std::min(1.f, f / in) * std::min(1.f, (1 - f) / out); };
    double sc = std::max(0.2, (std::fabs(s.x) + std::fabs(s.y) + std::fabs(s.z)) / 3);
    // (effet porté par un objet : à sa taille, sur l'objet lui-même)
    if (attached && (e.kind == FxKind::Fire || e.kind == FxKind::Glow)) {
        double top = bbox ? double(bbox[4]) : 0.3, cx = bbox ? 0.5 * (bbox[0] + bbox[3]) : 0, cz = bbox ? 0.5 * (bbox[2] + bbox[5]) : 0;
        auto halo = [&](const V3& c, double size, float r, float g, float bl, float a) {
            FxBatch h;
            h.first = uint32_t(rend.fx.size());
            h.additive = true;
            h.tex = tex(e.mask);
            h.mode = h.tex ? 6 : 0;
            auto [ax, ay] = bill(c, size, 0);
            quad(c, ax, ay, r, g, bl, a);
            h.count = uint32_t(rend.fx.size()) - h.first;
            if (h.count) rend.fxBatches.push_back(h);
        };
        if (e.kind == FxKind::Fire) {
            // (flamme de bougie au sommet de l'objet : texture de flamme du jeu, vacillante, et son halo)
            V3 c = local(cx, top + 0.03 / std::max(0.2, std::fabs(s.y)), cz);
            double fl = 1 + 0.12 * std::sin(time * 13 + seed % 7) + 0.06 * std::sin(time * 29 + seed % 5);
            b.additive = true;
            b.tex = tex(e.color);
            b.mode = b.tex ? 1 : 0;
            V3 up = norm(matVec(R, V3(0, 1, 0)));
            V3 side = norm(cross(up, cross(camR, camU)));
            if (len(side) < 0.5) side = camR;
            quad(c + up * (0.035 * sc * fl), side * (0.018 * sc), up * (0.04 * sc * fl), 1.f, 0.85f, 0.6f, 0.95f);
            b.count = uint32_t(rend.fx.size()) - b.first;
            if (b.count) rend.fxBatches.push_back(b);
            halo(c + up * (0.04 * sc), 0.22 * sc * fl, 1.f, 0.62f, 0.25f, 0.35f);
        } else {
            // (insecte lumineux ou lueur : un point qui vagabonde autour de l'objet, et son halo)
            double ph = (seed % 1000) * 0.00628;
            V3 c = local(cx + std::sin(time * 0.7 + ph) * 0.8 + std::sin(time * 1.9 + ph * 3) * 0.2, top * 0.5 + 0.4 + std::sin(time * 1.1 + ph * 2) * 0.25,
                         cz + std::cos(time * 0.6 + ph) * 0.8 + std::cos(time * 2.3 + ph) * 0.2);
            float a = 0.75f + 0.25f * float(std::sin(time * 3.1 + ph * 5));
            b.additive = true;
            b.tex = tex(e.mask);
            b.mode = b.tex ? 6 : 0;
            auto [ax, ay] = bill(c, 0.06 * sc, 0);
            quad(c, ax, ay, 1.f, 0.97f, 0.7f, a);
            b.count = uint32_t(rend.fx.size()) - b.first;
            if (b.count) rend.fxBatches.push_back(b);
            halo(c, 0.35 * sc, 0.75f, 0.95f, 0.45f, 0.3f * a);
        }
        return;
    }
    switch (e.kind) {
        case FxKind::Leaf: {
            // (feuilles qui tombent en tournoyant, poussées par le vent ; texture de feuille du jeu)
            // (sans texture de couleur, la forme vient de la texture d'intensité : plumes « _Fia »…)
            b.tex = tex(e.color);
            b.mode = b.tex ? 1 : 0;
            bool shapeOnly = false;
            if (!b.tex && (b.tex = tex(e.mask))) { b.mode = 6; shapeOnly = true; }
            const int N = 40;
            const float L = 9;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u);
                double x = (fxRand(seed, i, cyc * 3 + 1) * 2 - 1) * 10, z = (fxRand(seed, i, cyc * 3 + 2) * 2 - 1) * 10;
                double ph = fxRand(seed, i, 7) * 6.283;
                x += std::sin(time * 1.3 + ph) * 0.6 + fr * 2.5;
                z += std::cos(time * 1.1 + ph) * 0.4;
                V3 c = local(x, 7 - 8.5 * fr, z);
                auto [ax, ay] = bill(c, 0.13, time * 1.7 + ph, 0.25 + 0.75 * std::fabs(std::cos(time * 2.9 + ph)));
                float lum = 0.75f + 0.25f * fxRand(seed, i, 9);
                if (shapeOnly) quad(c, ax, ay, 0.95f * lum, 0.95f * lum, 0.92f * lum, fade(fr, 0.06f, 0.1f));
                else if (b.mode) quad(c, ax, ay, lum, lum, lum, fade(fr, 0.06f, 0.1f));
                else quad(c, ax, ay, 0.35f * lum, 0.55f * lum, 0.2f * lum, fade(fr, 0.06f, 0.1f));
            }
            break;
        }
        case FxKind::Fog: {
            // (brume : texture du jeu — de forme « _Fi(a) », sinon de couleur « _Fca » —. Émetteurs ponctuels : quelques
            // grandes nappes posées à l'emplacement, qui respirent ; sinon des nappes qui dérivent sur une zone)
            b.tex = tex(e.mask);
            b.mode = b.tex ? 2 : 0;
            if (!b.tex && (b.tex = tex(e.color))) b.mode = 1;
            bool point = e.modules.count("EmitterShapePoint") && !e.modules.count("EmitterShapeCube") && !e.modules.count("EmitterShapeSphereVolume");
            if (point) {
                int N = std::clamp(int(e.emitters.size()), 2, 6);
                for (int i = 0; i < N; i++) {
                    double ph = fxRand(seed, i, 9) * 6.283;
                    V3 c = local((fxRand(seed, i, 1) * 2 - 1) * 2 + std::sin(time * 0.05 + ph) * 0.8, 0.5 + fxRand(seed, i, 2) * 1.5,
                                 (fxRand(seed, i, 3) * 2 - 1) * 2 + std::cos(time * 0.04 + ph) * 0.6);
                    auto [ax, ay] = bill(c, (6 + 4 * fxRand(seed, i, 6)) * sc, ph + time * 0.015);
                    quad(c, ax, ay, 0.82f, 0.88f, 0.94f, 0.13f + 0.04f * float(std::sin(time * 0.2 + ph)));
                }
                break;
            }
            const int N = 10;
            const float L = 26;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u);
                double x = (fxRand(seed, i, cyc * 3 + 1) * 2 - 1) * 12 + (fr - 0.5) * 4, z = (fxRand(seed, i, cyc * 3 + 2) * 2 - 1) * 12 + (fr - 0.5) * 1.5;
                V3 c = local(x, 0.6 + fxRand(seed, i, 5) * 2, z);
                auto [ax, ay] = bill(c, (5 + 3 * fxRand(seed, i, 6)) * sc, fxRand(seed, i, 8) * 6.283 + time * 0.02);
                quad(c, ax, ay, 0.8f, 0.86f, 0.92f, 0.16f * std::sin(3.14159f * fr));
            }
            break;
        }
        case FxKind::Hidden:
            // (masque qui retire la brume volumique : rien à voir en jeu)
            return;
        case FxKind::Abyss: {
            // (abîme : grandes nappes très sombres et poussières noires qui montent lentement)
            b.tex = tex(e.mask);
            b.mode = b.tex ? 2 : 0;
            for (int i = 0; i < 6; i++) {
                double ph = fxRand(seed, i, 9) * 6.283;
                V3 c = local((fxRand(seed, i, 1) * 2 - 1) * 4, -1 + fxRand(seed, i, 2) * 3, (fxRand(seed, i, 3) * 2 - 1) * 4);
                auto [ax, ay] = bill(c, (5 + 4 * fxRand(seed, i, 6)) * sc, ph + time * 0.02);
                quad(c, ax, ay, 0.05f, 0.02f, 0.07f, 0.28f + 0.06f * float(std::sin(time * 0.3 + ph)));
            }
            const float L = 7;
            for (int i = 6; i < 40; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u);
                V3 c = local((fxRand(seed, i, cyc + 1) * 2 - 1) * 5, -2 + fr * 7, (fxRand(seed, i, cyc + 2) * 2 - 1) * 5);
                auto [ax, ay] = bill(c, 0.08 * sc, 0);
                quad(c, ax, ay, 0.04f, 0.02f, 0.05f, 0.8f * std::sin(3.14159f * fr));
            }
            break;
        }
        case FxKind::Bubbles: {
            // (courant sous l'eau : bulles du jeu qui montent en filant le long de l'axe X de l'acteur, vers la gauche ou la droite)
            b.tex = tex(e.mask);
            b.mode = b.tex ? 6 : 3;
            // (« …_WaterFlow_3m_L » : courant vers la gauche, sur 3 m)
            std::string ln = lower(e.name);
            bool flow = ln.find("waterflow") != std::string::npos || ln.find("divinggame") != std::string::npos;
            double dirx = ln.size() >= 2 && ln.compare(ln.size() - 2, 2, "_l") == 0 ? -1 : 1, reach = 5.6;
            if (auto mpos = ln.find("m_"); mpos != std::string::npos && mpos > 0 && std::isdigit(uint8_t(ln[mpos - 1]))) reach = ln[mpos - 1] - '0';
            const int N = 36;
            const float L = 3.5f;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u), t = fr * L;
                // (sans courant — marais empoisonné, passage sous l'eau — : les bulles montent sur place)
                V3 c = flow ? local(dirx * fr * reach + std::sin(time * 3 + i) * 0.05, (fxRand(seed, i, cyc + 1) * 2 - 1) * 0.6 + t * 0.35,
                                    (fxRand(seed, i, cyc + 2) * 2 - 1) * 0.8)
                            : local((fxRand(seed, i, cyc + 1) * 2 - 1) * 1.5 + std::sin(time * 3 + i) * 0.05, t * 0.5,
                                    (fxRand(seed, i, cyc + 2) * 2 - 1) * 1.5);
                auto [ax, ay] = bill(c, (0.03 + 0.05 * fxRand(seed, i, 5)) * sc, 0);
                quad(c, ax, ay, 0.85f, 0.95f, 1.f, 0.7f * fade(fr, 0.1f, 0.25f));
            }
            break;
        }
        case FxKind::Debris: {
            // (débris rocheux qui flottent et tournent lentement en s'élevant, texture de roche du jeu)
            b.tex = tex(e.color);
            b.mode = b.tex ? 1 : 0;
            const int N = 24;
            const float L = 18;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u);
                double a = fxRand(seed, i, cyc + 1) * 6.283, r = 3 + 12 * fxRand(seed, i, cyc + 2);
                V3 c = local(std::cos(a + time * 0.02) * r, fr * 10, std::sin(a + time * 0.02) * r);
                auto [ax, ay] = bill(c, (0.2 + 0.5 * fxRand(seed, i, 5)) * sc, time * (0.3 + fxRand(seed, i, 6)) + a);
                quad(c, ax, ay, 0.55f, 0.5f, 0.48f, fade(fr, 0.1f, 0.15f));
            }
            break;
        }
        case FxKind::Aura: {
            // (Chant de Saria : voile lumineux vert qui tourne en anneau autour de l'emplacement)
            b.additive = true;
            b.tex = tex(e.mask);
            b.mode = b.tex ? 6 : 4;
            V3 up = norm(matVec(R, V3(0, 1, 0)));
            const int N = 14;
            for (int i = 0; i < N; i++) {
                double a = time * 0.4 + i * 6.283 / N, a2 = a + 6.283 / N;
                V3 p0 = local(std::cos(a) * 1.4, 0.2 + 0.25 * std::sin(time + i), std::sin(a) * 1.4);
                V3 p1 = local(std::cos(a2) * 1.4, 0.2 + 0.25 * std::sin(time + i + 1), std::sin(a2) * 1.4);
                V3 c = (p0 + p1) * 0.5;
                quad(c + up * (0.6 * sc), (p1 - p0) * 0.5, up * (0.6 * sc), 0.45f, 1.f, 0.55f, 0.22f);
            }
            break;
        }
        case FxKind::Aurora: {
            // (aurore : rideaux lumineux verts et bleus qui ondulent, faits de bandes verticales)
            b.additive = true;
            b.mode = 4;
            V3 up = norm(matVec(R, V3(0, 1, 0)));
            for (int k = 0; k < 4; k++) {
                double z0 = (k - 1.5) * 4, h = (6 + 3 * fxRand(seed, k, 1)) * sc, y0 = 6 + 2 * fxRand(seed, k, 2);
                const int S = 18;
                for (int i = 0; i < S; i++) {
                    double x0 = (i - S / 2) * 1.2, x1 = x0 + 1.2;
                    auto wave = [&](double x) { return std::sin(x * 0.35 + time * 0.5 + k) * 1.5 + std::sin(x * 0.9 - time * 0.8) * 0.4; };
                    V3 p0 = local(x0, y0, z0 + wave(x0)), p1 = local(x1, y0, z0 + wave(x1));
                    float g = 0.5f + 0.5f * float(std::sin(x0 * 0.3 + time * 0.7 + k));
                    quad((p0 + p1) * 0.5 + up * (h * 0.5), (p1 - p0) * 0.5, up * (h * 0.5), 0.3f * g, 1.f, 0.6f + 0.4f * (1 - g), 0.18f);
                }
            }
            break;
        }
        case FxKind::Whirlpool: {
            // (tourbillon : écume et bulles qui tournent en spirale en descendant vers le centre, et anneaux à plat)
            b.tex = tex(e.mask);
            b.mode = b.tex ? 2 : 0;
            V3 ex = matVec(R, V3(1, 0, 0)), ez = matVec(R, V3(0, 0, 1));
            const int N = 50;
            const float L = 5;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                float fr = u - std::floor(u);
                double r = (1 - fr) * 4 + 0.2, a = fxRand(seed, i, 1) * 6.283 + fr * 9;
                V3 c = local(std::cos(a) * r, -fr * fr * 2.5, std::sin(a) * r);
                auto [ax, ay] = bill(c, (0.15 + 0.25 * (1 - fr)) * sc, a);
                quad(c, ax, ay, 0.85f, 0.93f, 1.f, 0.45f * fade(fr, 0.15f, 0.3f));
            }
            for (int k = 0; k < 3; k++) {
                double r = (1.5 + k * 1.2) * sc, rot = time * (0.9 - k * 0.2);
                V3 c = local(0, 0.02 - k * 0.15, 0);
                double cr = std::cos(rot), sr = std::sin(rot);
                quad(c, (ex * cr + ez * sr) * r, (ez * cr - ex * sr) * r, 0.8f, 0.9f, 1.f, 0.25f);
            }
            break;
        }
        case FxKind::Fish: {
            // (bancs de poissons : silhouettes sombres du jeu qui tournent sous l'eau, à plat)
            b.tex = tex(e.mask.empty() ? e.color : e.mask);
            b.mode = b.tex ? 6 : 0;
            const int N = 7;
            V3 up = norm(matVec(R, V3(0, 1, 0)));
            for (int i = 0; i < N; i++) {
                double rad = (1.5 + 2.5 * fxRand(seed, i, 1)) * sc, w = (0.25 + 0.2 * fxRand(seed, i, 2)) * (fxRand(seed, i, 3) < 0.5 ? -1 : 1);
                double a = time * w + fxRand(seed, i, 4) * 6.283, y = -0.3 - 0.4 * fxRand(seed, i, 5);
                V3 c = local(std::cos(a) * rad / sc, y, std::sin(a) * rad / sc);
                V3 dir = norm(matVec(R, V3(-std::sin(a) * w, 0, std::cos(a) * w)));
                V3 side = norm(cross(up, dir));
                double wig = 1 + 0.08 * std::sin(time * 6 + i);
                quad(c, dir * (0.22 * sc * wig), side * (0.09 * sc), 0.08f, 0.1f, 0.1f, 0.55f);
            }
            break;
        }
        case FxKind::Ripple: {
            // (cercles qui s'élargissent à plat dans le plan de l'acteur)
            b.tex = tex(e.mask);
            b.mode = !b.tex ? 3 : e.mask.find("Ripple") != std::string::npos ? 6 : 5;
            const int N = 5;
            const float L = 1.8f;
            V3 ex = matVec(R, V3(1, 0, 0)), ez = matVec(R, V3(0, 0, 1));
            for (int i = 0; i < N; i++) {
                float u = time / L + float(i) / N + fxRand(seed, i, 0) * 0.15f;
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u);
                V3 c = local((fxRand(seed, i, cyc * 3 + 1) * 2 - 1) * 0.5, 0.03, (fxRand(seed, i, cyc * 3 + 2) * 2 - 1) * 0.5);
                double r = (0.1 + 1.1 * fr) * sc;
                quad(c, ex * r, ez * r, 0.85f, 0.93f, 1.f, 0.55f * std::pow(1 - fr, 1.5f));
            }
            break;
        }
        case FxKind::Spray: {
            // (embruns : bouffées blanches projetées puis retombantes, qui gonflent et s'effacent)
            b.tex = tex(e.mask);
            b.mode = b.tex ? 2 : 0;
            if (!b.tex && (b.tex = tex(e.color))) b.mode = 1;
            bool line = e.modules.count("EmitterShapeLine") > 0;
            const int N = 30;
            const float L = 2.4f;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u), t = fr * L;
                double a = fxRand(seed, i, cyc * 3 + 1) * 6.283, v = 0.8 + fxRand(seed, i, cyc * 3 + 2) * 1.4;
                // (émetteur en ligne, comme le pied d'une cascade : départs répartis le long de l'axe X de l'acteur)
                double x0 = line ? (fxRand(seed, i, cyc * 3 + 3) * 2 - 1) * 2.5 : 0;
                V3 c = local(x0 + std::cos(a) * v * t, 2.2 * t - 1.6 * t * t * 0.5, std::sin(a) * v * t);
                auto [ax, ay] = bill(c, (0.4 + 1.8 * fr) * sc, a);
                quad(c, ax, ay, 0.92f, 0.96f, 1.f, 0.28f * fade(fr, 0.1f, 0.9f));
            }
            break;
        }
        case FxKind::Glow: {
            // (lueurs qui flottent et palpitent : insectes, poussières dans la lumière)
            b.additive = true;
            b.mode = 0;
            const int N = 16;
            for (int i = 0; i < N; i++) {
                double ph = fxRand(seed, i, 1) * 6.283;
                V3 c = local((fxRand(seed, i, 2) * 2 - 1) * 5 + std::sin(time * 0.35 + ph) * 1.2, 0.4 + fxRand(seed, i, 3) * 2.5 + std::sin(time * 0.6 + ph * 2) * 0.4,
                             (fxRand(seed, i, 4) * 2 - 1) * 5 + std::cos(time * 0.3 + ph) * 1.2);
                auto [ax, ay] = bill(c, 0.12, 0);
                float a = 0.5f + 0.5f * float(std::sin(time * 2.6 + ph * 3));
                quad(c, ax, ay, 1.f, 0.92f, 0.55f, 0.9f * a * a);
            }
            break;
        }
        case FxKind::Shaft: {
            // (rayons : longues bandes claires orientées selon l'acteur, tournées vers la caméra autour de leur axe)
            b.additive = true;
            b.mode = 4;
            V3 axis = norm(matVec(R, V3(0, -1, 0)));
            for (int i = 0; i < 5; i++) {
                V3 c = local((fxRand(seed, i, 1) * 2 - 1) * 2.5, 0, (fxRand(seed, i, 2) * 2 - 1) * 2.5) + axis * (6 * s.y);
                V3 side = norm(cross(axis, cross(camR, camU)));
                double w = (0.6 + fxRand(seed, i, 3) * 0.9) * sc;
                float a = 0.07f * (0.75f + 0.25f * float(std::sin(time * 0.4 + i * 1.7)));
                quad(c, side * w, axis * (6 * s.y), 1.f, 0.95f, 0.8f, a);
            }
            break;
        }
        case FxKind::Smoke: {
            b.tex = tex(e.mask);
            b.mode = b.tex ? 2 : 0;
            if (!b.tex && (b.tex = tex(e.color))) b.mode = 1;
            const int N = 14;
            const float L = 6;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u);
                V3 c = local((fxRand(seed, i, cyc + 1) * 2 - 1) * 0.4 + fr * 1.2, fr * 5, (fxRand(seed, i, cyc + 2) * 2 - 1) * 0.4);
                auto [ax, ay] = bill(c, (0.4 + 2.2 * fr) * sc, fxRand(seed, i, 5) * 6.283 + fr);
                quad(c, ax, ay, 0.6f, 0.6f, 0.62f, 0.3f * std::sin(3.14159f * fr));
            }
            break;
        }
        case FxKind::Fire: {
            b.additive = true;
            b.tex = tex(e.mask);
            b.mode = b.tex ? 2 : 0;
            const int N = 18;
            const float L = 0.9f;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u);
                V3 c = local((fxRand(seed, i, cyc + 1) * 2 - 1) * 0.15, fr * 0.8, (fxRand(seed, i, cyc + 2) * 2 - 1) * 0.15);
                auto [ax, ay] = bill(c, (0.25 - 0.18 * fr) * sc, fxRand(seed, i, 5) * 6.283);
                quad(c, ax, ay, 1.f, 0.55f + 0.3f * (1 - fr), 0.2f, 0.8f * fade(fr, 0.1f, 0.6f));
            }
            break;
        }
        case FxKind::Drip: {
            const int N = 8;
            const float L = 1.4f;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u), t = fr * L;
                V3 c = local((fxRand(seed, i, cyc + 1) * 2 - 1) * 1.5, -4.9 * t * t * 0.4, (fxRand(seed, i, cyc + 2) * 2 - 1) * 1.5);
                auto [ax, ay] = bill(c, 0.05, 0, 0.6);
                quad(c, ax, ay, 0.75f, 0.85f, 1.f, 0.7f * fade(fr, 0.05f, 0.2f));
            }
            break;
        }
        default: {
            b.additive = true;
            const int N = 10;
            const float L = 3;
            for (int i = 0; i < N; i++) {
                float u = time / L + fxRand(seed, i, 0);
                uint32_t cyc = uint32_t(std::floor(u));
                float fr = u - std::floor(u);
                V3 c = local((fxRand(seed, i, cyc + 1) * 2 - 1) * 2, 0.3 + fr * 1.5, (fxRand(seed, i, cyc + 2) * 2 - 1) * 2);
                auto [ax, ay] = bill(c, 0.1, 0);
                quad(c, ax, ay, 0.9f, 0.95f, 1.f, 0.6f * std::sin(3.14159f * fr));
            }
            break;
        }
    }
    b.count = uint32_t(rend.fx.size()) - b.first;
    if (b.count) rend.fxBatches.push_back(b);
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

TexSlot* Editor::texture(const std::string& name, bool optional) {
    auto it = texSlots.find(name);
    if (it != texSlots.end()) {
        if (!optional) it->second.optional = false;
        return &it->second;
    }
    if (!game) return nullptr;
    TexSlot& s = texSlots[name];
    s.optional = optional;
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
            // (cartes d'éclairage précalculé : en entier, ce sont des atlas où chaque objet n'a que quelques pixels)
            r->t = bntxExtract(g->readTex(rel), want.rfind("RBake_", 0) == 0 ? 4096 : 1024, want);
            if (r->t.code == 5 || r->t.code == 6) r->kind = 2;   // (BC4, BC5 : masques ou reliefs, pas des couleurs)
            // (BC6H : images en virgule flottante, comme les cartes d'éclairage précalculé ; envoyées telles quelles)
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
                case 8: f = r->t.format.find("sign") != std::string::npos ? VK_FORMAT_BC6H_SFLOAT_BLOCK : VK_FORMAT_BC6H_UFLOAT_BLOCK; break;
                default: f = VK_FORMAT_R8G8B8A8_UNORM; data = r->rgba.data(); size = r->rgba.size(); mips = true; break;
            }
            if (r->t.code >= 2 && r->t.code <= 8 && !(vk.bc && vk.formatOk(f))) {
                if (mask) { s2.st = TexSlot::Mask; s2.why = "masque ou relief (" + r->t.format + ")"; return; }
                s2.st = TexSlot::None;
                s2.why = "format " + r->t.format + " refusé par la carte graphique";
                return;
            }
            try {
                s2.tex = vk.texture(f, r->t.w, r->t.h, data, size, swz, mips);
                s2.st = mask ? TexSlot::Mask : TexSlot::Ok;
                if (mask) s2.why = (name.rfind("RBake_", 0) == 0 ? "carte d'éclairage précalculé (" : "masque ou relief (") + r->t.format + ")";
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
        if (!t.optional && ((t.st == TexSlot::None && t.why.rfind("introuvable", 0) == 0) || (t.st == TexSlot::Ok && !t.standIn.empty()))) v.push_back(n);
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
