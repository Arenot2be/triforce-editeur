#include "core/mapdoc.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "core/textmap.hpp"

namespace te {

static const char* POS_KEYS[] = {"Translate", "Trans", "Position", "Pos"};
static const char* ROT_KEYS[] = {"Rotate", "Rot", "Rotation"};
static const char* NAME_KEYS[] = {"Gyaml", "Name", "ActorName", "UnitConfigName", "InstanceName", "Type"};

std::string pathName(const VPath& p) {
    if (p.empty()) return "(racine)";
    std::string s;
    for (size_t k = 0; k < p.size(); k++) s += (k ? " / " : "") + (p[k].isIndex ? std::to_string(p[k].index) : p[k].key);
    return s;
}

static bool isPos(const Value* p) {
    if (!p || p->t != T::Array || p->a->size() != 3) return false;
    for (auto& x : *p->a) if (!(x.t == T::F32 || x.t == T::F64 || x.t == T::I32)) return false;
    return true;
}
const char* posKey(const Value& v) {
    if (v.t != T::Map) return nullptr;
    for (auto k : POS_KEYS) if (isPos(v.get(k))) return k;
    return nullptr;
}
const char* rotKey(const Value& v) {
    if (v.t != T::Map) return nullptr;
    for (auto k : ROT_KEYS) if (isPos(v.get(k))) return k;
    return nullptr;
}
std::string nameOf(const Value& v) {
    if (v.t != T::Map) return "";
    for (auto k : NAME_KEYS) if (auto x = v.get(k); x && x->t == T::String) return x->s;
    return "";
}
std::string catOf(const std::string& name) {
    if (name.empty()) return "(sans nom)";
    auto i = name.find('_');
    return i != std::string::npos && i > 0 ? name.substr(0, i) : name;
}
bool vec3(const Value& v, const char* key, double out[3]) {
    auto p = key ? v.get(key) : nullptr;
    if (!isPos(p)) return false;
    for (int k = 0; k < 3; k++) out[k] = (*p->a)[size_t(k)].num();
    return true;
}
void setNum(Value& arr, size_t i, double x) {
    Value& e = (*arr.a)[i];
    e = e.t == T::F64 ? Value::f64(x) : Value::f32(x);
}
void setVec(Value& obj, const char* key, const double v[3]) {
    Value* a = obj.get(key);
    if (!a || a->t != T::Array || a->a->size() != 3) {
        Value n = Value::array();
        for (int k = 0; k < 3; k++) n.a->push_back(Value::f32(0));
        (*obj.d)[key] = n;
        a = obj.get(key);
    }
    for (int k = 0; k < 3; k++) setNum(*a, size_t(k), v[k]);
}

// ---------------------------------------------------------------- carte
std::vector<MapEntry> MapDoc::listMaps(const Game& game, const fs::path& work) {
    std::vector<MapEntry> out;
    std::error_code ec;
    fs::path banc = game.path("Banc");
    if (!fs::is_directory(banc, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(banc, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        std::string n = pathStr(it->path().filename());
        if (!endsWith(n, ".bcett.byml.zs")) continue;
        std::string rel = replaceAll(pathStr(fs::relative(it->path(), game.romfs, ec)), "\\", "/");
        out.push_back({rel, fs::is_regular_file(editPath(work, rel), ec)});
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.path < b.path; });
    return out;
}

fs::path MapDoc::editPath(const fs::path& work, const std::string& rel) {
    fs::path p = workPath(work, rel);
    p += u8path(".json");
    return p;
}

static uint64_t fnv(const Bytes& b) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t c : b) { h ^= c; h *= 1099511628211ull; }
    return h ^ b.size();
}

static Document base(Game& game, const fs::path& work, const std::string& rel, std::string& origin, uint64_t& rev) {
    fs::path ep = MapDoc::editPath(work, rel);
    std::error_code ec;
    if (fs::is_regular_file(ep, ec)) {
        Bytes raw = readFile(ep);
        rev = fnv(raw);
        origin = "travail";
        try {
            std::string t(raw.begin(), raw.end());
            if (t.size() >= 3 && (uint8_t)t[0] == 0xEF) t.erase(0, 3);
            return fromText(t);
        } catch (const std::exception& e) {
            throw Error("le fichier de travail " + pathStr(ep) + " est mal écrit : " + e.what());
        }
    }
    Bytes raw = game.read(rel);
    rev = fnv(raw);
    origin = "jeu";
    return bymlLoad(raw);
}

static void findLayers(const Value& v, VPath& path, std::vector<VPath>& out) {
    if (v.t == T::Array) {
        for (auto& x : *v.a) if (posKey(x)) { out.push_back(path); return; }
        for (size_t i = 0; i < v.a->size(); i++) {
            path.push_back({true, "", i});
            findLayers((*v.a)[i], path, out);
            path.pop_back();
        }
    } else if (v.t == T::Map) {
        for (auto& [k, x] : *v.d) {
            path.push_back({false, k, 0});
            findLayers(x, path, out);
            path.pop_back();
        }
    }
}

static Value* getAt(Value& root, const VPath& p) {
    Value* v = &root;
    for (auto& e : p) {
        if (e.isIndex) { if (v->t != T::Array || e.index >= v->a->size()) return nullptr; v = &(*v->a)[e.index]; }
        else { v = v->get(e.key); if (!v) return nullptr; }
    }
    return v;
}

void MapDoc::open(Game& game, const fs::path& work, const std::string& rel) {
    Document doc = base(game, work, rel, origin, rev_);
    std::vector<VPath> paths;
    VPath cur;
    findLayers(doc.root, cur, paths);
    std::vector<Layer> ls;
    for (auto& p : paths) {
        Layer L;
        L.path = p;
        L.name = pathName(p);
        Value* arr = getAt(doc.root, p);
        for (size_t i = 0; i < arr->a->size(); i++) {
            auto r = std::make_shared<Rec>();
            r->value = std::move((*arr->a)[i]);
            r->src = int(i);
            r->layer = int(ls.size());
            L.recs.push_back(r);
        }
        ls.push_back(std::move(L));
    }
    path = rel;
    layers = std::move(ls);
    undo_.clear();
    redo_.clear();
    dirty = false;
}

static void allIds(const Value& v, std::set<uint64_t>& out) {
    switch (v.t) {
        case T::U64: out.insert(v.u); break;
        case T::I64: case T::U32: out.insert(uint64_t(v.i)); break;
        case T::Map: for (auto& [k, x] : *v.d) allIds(x, out); break;
        case T::Hash: for (auto& [k, x] : *v.h) allIds(x, out); break;
        case T::VHash: for (auto& [k, x] : *v.vh) allIds(x.first, out); break;
        case T::Array: for (auto& x : *v.a) allIds(x, out); break;
        default: break;
    }
}

static void freshIds(Value& item, std::set<uint64_t>& used, std::mt19937_64& rng) {
    if (item.t != T::Map) return;
    for (auto key : {"Hash", "SRTHash", "InstanceID", "HashId"}) {
        Value* old = item.get(key);
        if (!old || !(old->t == T::U64 || old->t == T::U32 || old->t == T::I64)) continue;
        int bits = old->t == T::U32 ? 32 : 63;
        uint64_t n;
        do { n = rng() & ((bits == 32 ? 0xFFFFFFFFull : 0x7FFFFFFFFFFFFFFFull)); } while (!n || used.count(n));
        used.insert(n);
        if (old->t == T::U64) *old = Value::u64(n);
        else if (old->t == T::U32) *old = Value::u32(uint32_t(n));
        else *old = Value::i64(int64_t(n));
    }
}

fs::path MapDoc::save(Game& game, const fs::path& work, std::vector<std::string>& warnings) {
    std::string org;
    uint64_t rev;
    Document doc = base(game, work, path, org, rev);
    if (rev != rev_) throw Error("la carte a changé depuis son ouverture (modifiée ailleurs) : recharge-la");
    std::vector<VPath> known;
    VPath cur;
    findLayers(doc.root, cur, known);
    if (known.size() != layers.size()) throw Error("les couches de la carte ont changé : recharge-la");
    std::set<uint64_t> used;
    allIds(doc.root, used);
    std::mt19937_64 rng(std::random_device{}());
    std::map<uint64_t, std::string> gone;
    for (size_t li = 0; li < layers.size(); li++) {
        Value* old = getAt(doc.root, layers[li].path);
        if (!old || old->t != T::Array) throw Error("couche introuvable : " + layers[li].name);
        std::set<int> kept;
        Value fresh = Value::array();
        for (auto& r : layers[li].recs) {
            Value v = r->value;
            if (r->dup) freshIds(v, used, rng);
            else if (r->src >= 0) kept.insert(r->src);
            fresh.a->push_back(std::move(v));
        }
        for (size_t i = 0; i < old->a->size(); i++) {
            if (kept.count(int(i))) continue;
            const Value& x = (*old->a)[i];
            for (auto key : {"Hash", "SRTHash", "InstanceID", "HashId"})
                if (auto h = x.get(key); h && (h->t == T::U64 || h->t == T::I64))
                    gone[h->t == T::U64 ? h->u : uint64_t(h->i)] = nameOf(x).empty() ? "(sans nom)" : nameOf(x);
        }
        *old = std::move(fresh);
    }
    if (!gone.empty()) {
        std::set<uint64_t> left;
        allIds(doc.root, left);
        for (auto& [h, n] : gone)
            if (left.count(h))
                warnings.push_back("un objet restant cite encore « " + n + " » (Hash " + std::to_string(h) + "), qui a été supprimé : le jeu peut mal le prendre");
    }
    bymlDump(doc);   // (vérifie que la carte peut s'écrire : Error sinon)
    std::string text = toText(doc);
    fs::path ep = editPath(work, path);
    writeFileAtomic(ep, text);
    rev_ = fnv(Bytes(text.begin(), text.end()));
    origin = "travail";
    // (les copies ont reçu leurs identifiants : elles deviennent des objets ordinaires de la carte)
    for (size_t li = 0; li < layers.size(); li++) {
        Value* arr = getAt(doc.root, layers[li].path);
        for (size_t i = 0; i < layers[li].recs.size(); i++) {
            auto& r = layers[li].recs[i];
            r->value = (*arr->a)[i];
            r->src = int(i);
            r->dup = false;
        }
    }
    undo_.clear();
    redo_.clear();
    dirty = false;
    return ep;
}

void MapDoc::revert(const fs::path& work, const std::string& rel) {
    std::error_code ec;
    fs::remove(editPath(work, rel), ec);
}

// ---------------------------------------------------------------- annuler
MapDoc::Tx MapDoc::begin() const {
    Tx t;
    for (auto& L : layers) t.lay.push_back(L.recs);
    return t;
}
void MapDoc::touch(Tx& tx, const RecP& r) const {
    if (!tx.vals.count(r.get())) tx.vals[r.get()] = {r, r->value};
}
void MapDoc::commit(Tx tx) {
    Tx after;
    for (auto& L : layers) after.lay.push_back(L.recs);
    for (auto& [p, rv] : tx.vals) after.vals[p] = {rv.first, rv.first->value};
    undo_.push_back({std::move(tx), std::move(after)});
    if (undo_.size() > 300) undo_.erase(undo_.begin());
    redo_.clear();
    dirty = true;
}
void MapDoc::restore(const Tx& st) {
    for (size_t i = 0; i < layers.size() && i < st.lay.size(); i++) layers[i].recs = st.lay[i];
    for (auto& [p, rv] : st.vals) rv.first->value = rv.second;
}
bool MapDoc::undo() {
    if (undo_.empty()) return false;
    Step s = std::move(undo_.back());
    undo_.pop_back();
    restore(s.before);
    redo_.push_back(std::move(s));
    dirty = true;
    return true;
}
bool MapDoc::redo() {
    if (redo_.empty()) return false;
    Step s = std::move(redo_.back());
    redo_.pop_back();
    restore(s.after);
    undo_.push_back(std::move(s));
    dirty = true;
    return true;
}

}  // namespace te
