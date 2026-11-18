#include "core/textmap.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>

namespace te {

static const char* TAGS[] = {"!u32", "!i64", "!u64", "!f64", "!f32nan", "!bin", "!file", "!h32", "!vh32", "!map"};
static const char* WEB_TAGS[] = {"!u32", "!i64", "!u64", "!f64", "!f32", "!f32bits", "!bin", "!file", "!h32", "!vh32", "!map"};
template <size_t N>
static bool isTag(const std::string& k, const char* (&tags)[N]) {
    for (auto t : tags) if (k == t) return true;
    return false;
}

std::string f32Text(double x) {
    if (std::isnan(x)) {
        float f = float(x);
        uint32_t u;
        std::memcpy(&u, &f, 4);
        return fmt("{\"!f32nan\": %u}", u);
    }
    if (std::isinf(x)) return x > 0 ? "1e999" : "-1e999";
    float want = float(x);
    std::string s;
    for (int p = 1; p < 10; p++) {
        s = fmt("%.*g", p, x);
        if (float(std::strtod(s.c_str(), nullptr)) == want) break;
    }
    if (s.find_first_of(".e") == std::string::npos && s.find("inf") == std::string::npos) s += ".0";
    return s;
}

std::string f64Text(double x) {
    if (std::isinf(x)) return x > 0 ? "1e999" : "-1e999";
    if (std::isnan(x)) return "NaN";
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, x);
    std::string s(buf, r.ptr);
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    return s;
}

static std::string strHex(const std::string& s) { return hex(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
static std::string unhex(const std::string& h, const std::string& where) {
    if (h.size() % 2) throw Error(where + " : hexadécimal de longueur impaire");
    std::string o;
    for (size_t k = 0; k < h.size(); k += 2) {
        auto v = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw Error(where + " : hexadécimal invalide");
        };
        o += char(v(h[k]) << 4 | v(h[k + 1]));
    }
    return o;
}

// ---------------------------------------------------------------- fichier de travail
static void dump(const Value& v, std::string& out, int indent, int level);

static void dumpMap(const Dict& d, std::string& out, int indent, int level) {
    std::string pad(size_t(indent * (level + 1)), ' '), end(size_t(indent * level), ' ');
    out += "{\n";
    size_t k = 0;
    for (auto& [key, x] : d) {
        out += pad + jsonQuote(key) + ": ";
        dump(x, out, indent, level + 1);
        out += ++k < d.size() ? ",\n" : "\n";
    }
    out += end + "}";
}

static void dump(const Value& v, std::string& out, int indent, int level) {
    std::string pad(size_t(indent * (level + 1)), ' '), end(size_t(indent * level), ' ');
    switch (v.t) {
        case T::Map:
            if (v.d->empty()) { out += "{}"; return; }
            if (v.d->size() == 1 && isTag(v.d->begin()->first, TAGS)) {
                out += "{\"!map\": ";
                dumpMap(*v.d, out, indent, level);
                out += "}";
                return;
            }
            dumpMap(*v.d, out, indent, level);
            return;
        case T::Array: {
            auto& a = *v.a;
            if (a.empty()) { out += "[]"; return; }
            bool small = a.size() <= 4;
            for (auto& x : a) if (!(x.t == T::I32 || x.t == T::F32 || x.t == T::Bool || x.t == T::Null)) small = false;
            if (small) {
                out += "[";
                for (size_t k = 0; k < a.size(); k++) { if (k) out += ", "; dump(a[k], out, indent, level + 1); }
                out += "]";
                return;
            }
            out += "[\n";
            for (size_t k = 0; k < a.size(); k++) {
                out += pad;
                dump(a[k], out, indent, level + 1);
                out += k + 1 < a.size() ? ",\n" : "\n";
            }
            out += end + "]";
            return;
        }
        case T::Hash: case T::VHash: {
            out += v.t == T::Hash ? "{\"!h32\": {\n" : "{\"!vh32\": {\n";
            size_t n = v.t == T::Hash ? v.h->size() : v.vh->size(), k = 0;
            auto one = [&](uint32_t h, const Value& x, const uint32_t* unk) {
                out += pad + fmt("\"0x%08x\": ", h);
                if (unk) { out += "["; dump(x, out, indent, level + 1); out += fmt(", %u]", *unk); }
                else dump(x, out, indent, level + 1);
                out += ++k < n ? ",\n" : "\n";
            };
            if (v.t == T::Hash) for (auto& [h, x] : *v.h) one(h, x, nullptr);
            else for (auto& [h, x] : *v.vh) one(h, x.first, &x.second);
            out += end + "}}";
            return;
        }
        case T::String: out += jsonQuote(v.s); return;
        case T::Bool: out += v.b ? "true" : "false"; return;
        case T::Null: out += "null"; return;
        case T::I32: out += std::to_string(v.i); return;
        case T::F32: out += f32Text(v.f); return;
        case T::U32: out += fmt("{\"!u32\": %lld}", (long long)v.i); return;
        case T::I64: out += fmt("{\"!i64\": %lld}", (long long)v.i); return;
        case T::U64: out += fmt("{\"!u64\": %llu}", (unsigned long long)v.u); return;
        case T::F64: out += "{\"!f64\": " + f64Text(v.f) + "}"; return;
        case T::File: out += fmt("{\"!file\": [%u, \"", v.unk) + strHex(v.s) + "\"]}"; return;
        case T::Binary: out += "{\"!bin\": \"" + strHex(v.s) + "\"}"; return;
    }
}

std::string toText(const Document& doc, int indent) {
    std::string out = fmt("{\"!byml\": {\"version\": %d, \"big_endian\": %s},\n \"root\": ", doc.version, doc.big ? "true" : "false");
    if (doc.root.t == T::Null) out += "null";
    else dump(doc.root, out, indent, 0);
    out += "}\n";
    return out;
}

static int64_t needInt(const JNode& x, const std::string& where) {
    if (x.k != JNode::Number || !x.isInt()) throw Error(where + " : un nombre entier est attendu");
    errno = 0;
    if (!x.s.empty() && x.s[0] == '-') {
        long long v = std::strtoll(x.s.c_str(), nullptr, 10);
        if (errno) throw Error(where + " : nombre trop grand");
        return v;
    }
    unsigned long long v = std::strtoull(x.s.c_str(), nullptr, 10);
    if (errno) throw Error(where + " : nombre trop grand");
    return int64_t(v);
}
static uint64_t needU64(const JNode& x, const std::string& where) {
    if (x.k != JNode::Number || !x.isInt() || (!x.s.empty() && x.s[0] == '-')) throw Error(where + " : un nombre entier positif est attendu");
    errno = 0;
    unsigned long long v = std::strtoull(x.s.c_str(), nullptr, 10);
    if (errno) throw Error(where + " : nombre trop grand");
    return v;
}

static Value fromNode(const JNode& n, const std::string& where) {
    switch (n.k) {
        case JNode::Null: return Value::null();
        case JNode::Bool: return Value::boolean(n.b);
        case JNode::String: return Value::str(n.s);
        case JNode::Number:
            if (n.isInt()) {
                long long v = std::strtoll(n.s.c_str(), nullptr, 10);
                return Value::i32(v);   // (hors limites : refusé à l'écriture, comme Python)
            }
            return Value::f32(n.num());
        case JNode::Array: {
            Value a = Value::array();
            for (size_t k = 0; k < n.arr.size(); k++) a.a->push_back(fromNode(n.arr[k], where + fmt("[%zu]", k)));
            return a;
        }
        case JNode::Object: break;
    }
    if (n.obj.size() == 1) {
        auto& [k, x] = n.obj[0];
        try {
            if (k == "!u32") return Value::u32(uint32_t(needInt(x, where)));
            if (k == "!i64") return Value::i64(needInt(x, where));
            if (k == "!u64") return Value::u64(needU64(x, where));
            if (k == "!f64") { if (x.k != JNode::Number) throw Error("nombre attendu"); return Value::f64(x.num()); }
            if (k == "!f32nan") { uint32_t u = uint32_t(needInt(x, where)); float f; std::memcpy(&f, &u, 4); return Value::f32(f); }
            if (k == "!bin") { if (x.k != JNode::String) throw Error("texte attendu"); return Value::binary(unhex(x.s, where)); }
            if (k == "!file") {
                if (x.k != JNode::Array || x.arr.size() != 2 || x.arr[1].k != JNode::String) throw Error("[nombre, \"hexadécimal\"] attendu");
                return Value::file(unhex(x.arr[1].s, where), uint32_t(needInt(x.arr[0], where)));
            }
            if (k == "!h32") {
                if (x.k != JNode::Object) throw Error("dictionnaire attendu");
                Value h = Value::hash();
                for (auto& [hk, y] : x.obj) (*h.h)[uint32_t(std::stoul(hk, nullptr, 16))] = fromNode(y, where + "." + hk);
                return h;
            }
            if (k == "!vh32") {
                if (x.k != JNode::Object) throw Error("dictionnaire attendu");
                Value h = Value::vhash();
                for (auto& [hk, y] : x.obj) {
                    if (y.k != JNode::Array || y.arr.size() != 2) throw Error("[valeur, nombre] attendu");
                    (*h.vh)[uint32_t(std::stoul(hk, nullptr, 16))] = {fromNode(y.arr[0], where + "." + hk), uint32_t(needInt(y.arr[1], where))};
                }
                return h;
            }
            if (k == "!map") {
                if (x.k != JNode::Object) throw Error("dictionnaire attendu");
                Value m = Value::map();
                for (auto& [mk, y] : x.obj) (*m.d)[mk] = fromNode(y, where + "." + mk);
                return m;
            }
        } catch (const Error& e) {
            throw Error(where + " : valeur " + k + " mal écrite (" + e.what() + ")");
        } catch (const std::exception& e) {
            throw Error(where + " : valeur " + k + " mal écrite");
        }
        if (!k.empty() && k[0] == '!') throw Error(where + " : étiquette inconnue " + k);
    }
    Value m = Value::map();
    for (auto& [mk, y] : n.obj) (*m.d)[mk] = fromNode(y, where + "." + mk);
    return m;
}

Document fromText(const std::string& text) {
    JNode n = jsonParse(text);
    const JNode* head = n.get("!byml");
    const JNode* root = n.get("root");
    if (n.k != JNode::Object || !head || !root) throw Error("le fichier doit commencer par {\"!byml\": ..., \"root\": ...}");
    Document doc;
    if (auto v = head->get("version"); v && v->k == JNode::Number) doc.version = uint16_t(v->num());
    if (auto b = head->get("big_endian"); b && b->k == JNode::Bool) doc.big = b->b;
    if (root->k != JNode::Null) doc.root = fromNode(*root, "root");
    return doc;
}

// ---------------------------------------------------------------- propriétés d'un objet (éditeur)
static void web(const Value& v, std::string& out, int level) {
    std::string pad(size_t(2 * (level + 1)), ' '), end(size_t(2 * level), ' ');
    auto obj = [&](auto& items, auto key, auto val) {
        if (items.empty()) { out += "{}"; return; }
        out += "{\n";
        size_t k = 0;
        for (auto& it : items) {
            out += pad + key(it) + ": ";
            val(it);
            out += ++k < items.size() ? ",\n" : "\n";
        }
        out += end + "}";
    };
    switch (v.t) {
        case T::Map: {
            bool wrap = v.d->size() == 1 && isTag(v.d->begin()->first, WEB_TAGS);
            if (wrap) { out += "{\n" + pad + "\"!map\": "; level++; pad += "  "; end += "  "; }
            obj(*v.d, [](auto& it) { return jsonQuote(it.first); }, [&](auto& it) { web(it.second, out, level + 1); });
            if (wrap) out += "\n" + std::string(size_t(2 * (level - 1)), ' ') + "}";
            return;
        }
        case T::Array: {
            auto& a = *v.a;
            if (a.empty()) { out += "[]"; return; }
            out += "[\n";
            for (size_t k = 0; k < a.size(); k++) {
                out += pad;
                web(a[k], out, level + 1);
                out += k + 1 < a.size() ? ",\n" : "\n";
            }
            out += end + "]";
            return;
        }
        case T::Hash: case T::VHash: {
            out += std::string("{\n") + pad + (v.t == T::Hash ? "\"!h32\": " : "\"!vh32\": ");
            std::string p2 = pad + "  ", e2 = pad;
            size_t n = v.t == T::Hash ? v.h->size() : v.vh->size(), k = 0;
            out += "{\n";
            auto one = [&](uint32_t h, const Value& x, const uint32_t* unk) {
                out += p2 + fmt("\"0x%08x\": ", h);
                if (unk) { out += "["; web(x, out, level + 2); out += fmt(", %u]", *unk); }
                else web(x, out, level + 2);
                out += ++k < n ? ",\n" : "\n";
            };
            if (v.t == T::Hash) for (auto& [h, x] : *v.h) one(h, x, nullptr);
            else for (auto& [h, x] : *v.vh) one(h, x.first, &x.second);
            out += e2 + "}\n" + end + "}";
            return;
        }
        case T::String: out += jsonQuote(v.s); return;
        case T::Bool: out += v.b ? "true" : "false"; return;
        case T::Null: out += "null"; return;
        case T::I32: out += std::to_string(v.i); return;
        case T::F32:
            if (std::isnan(v.f) || std::isinf(v.f)) {
                float f = float(v.f);
                uint32_t u;
                std::memcpy(&u, &f, 4);
                out += fmt("{\"!f32bits\": %u}", u);
            } else {
                std::string s = f32Text(v.f);
                if (endsWith(s, ".0")) s.resize(s.size() - 2);   // (comme la page : 1.0 s'écrit 1, et reste un F32)
                out += s;
            }
            return;
        case T::U32: out += fmt("{\"!u32\": %lld}", (long long)v.i); return;
        case T::I64: out += fmt("{\"!i64\": \"%lld\"}", (long long)v.i); return;
        case T::U64: out += fmt("{\"!u64\": \"%llu\"}", (unsigned long long)v.u); return;
        case T::F64: out += "{\"!f64\": " + f64Text(v.f) + "}"; return;
        case T::File: out += fmt("{\"!file\": [%u, \"", v.unk) + strHex(v.s) + "\"]}"; return;
        case T::Binary: out += "{\"!bin\": \"" + strHex(v.s) + "\"}"; return;
    }
}

std::string toWebText(const Value& v) {
    std::string out;
    web(v, out, 0);
    return out;
}

static int64_t webInt(const JNode& x, const std::string& where) {
    if (x.k == JNode::String) {
        std::string s = x.s;
        while (!s.empty() && s.back() == ' ') s.pop_back();
        while (!s.empty() && s.front() == ' ') s.erase(0, 1);
        char* e = nullptr;
        errno = 0;
        long long v;
        if (!s.empty() && s[0] != '-') v = (long long)std::strtoull(s.c_str(), &e, 0);
        else v = std::strtoll(s.c_str(), &e, 0);
        if (s.empty() || *e || errno) throw Error(where + " : « " + x.s + " » n'est pas un nombre entier");
        return v;
    }
    if (x.k == JNode::Number) {
        if (x.isInt()) return needInt(x, where);
        double d = x.num();
        if (d != std::floor(d)) throw Error(where + " : " + x.s + " n'est pas un nombre entier");
        return int64_t(d);
    }
    throw Error(where + " : un nombre entier est attendu");
}
static uint64_t webU64(const JNode& x, const std::string& where) {
    if (x.k == JNode::String && !x.s.empty() && x.s[0] != '-') {
        char* e = nullptr;
        errno = 0;
        unsigned long long v = std::strtoull(x.s.c_str(), &e, 0);
        if (*e || errno) throw Error(where + " : « " + x.s + " » n'est pas un nombre entier");
        return v;
    }
    if (x.k == JNode::Number && x.isInt()) return needU64(x, where);
    return uint64_t(webInt(x, where));
}

static const Value* tmplItem(const Value* t, size_t i) {
    if (!t || t->t != T::Array || t->a->empty()) return nullptr;
    return i < t->a->size() ? &(*t->a)[i] : &t->a->back();
}

Value fromWeb(const JNode& n, const Value* tmpl, const std::string& where) {
    switch (n.k) {
        case JNode::Null: return Value::null();
        case JNode::Bool: return Value::boolean(n.b);
        case JNode::String: return Value::str(n.s);
        case JNode::Number: {
            T tt = tmpl ? tmpl->t : T::Null;
            double d = n.num();
            bool integral = n.isInt() || (std::isfinite(d) && d == std::floor(d));
            if (tt == T::F32) return Value::f32(d);
            if (tt == T::F64) return Value::f64(d);
            if (integral && tt == T::U32) {
                int64_t v = webInt(n, where);
                if (v < 0 || v > UINT32_MAX) throw Error(where + fmt(" : %lld hors des limites d'un u32", (long long)v));
                return Value::u32(uint32_t(v));
            }
            if (integral && tt == T::I64) return Value::i64(webInt(n, where));
            if (integral && tt == T::U64) return Value::u64(webU64(n, where));
            if (integral) {
                int64_t v = webInt(n, where);
                if (v < INT32_MIN || v > INT32_MAX)
                    throw Error(where + fmt(" : %lld ne tient pas dans un entier 32 bits (écris {\"!i64\": \"%lld\"})", (long long)v, (long long)v));
                return Value::i32(v);
            }
            return Value::f32(d);
        }
        case JNode::Array: {
            Value a = Value::array();
            for (size_t k = 0; k < n.arr.size(); k++) a.a->push_back(fromWeb(n.arr[k], tmplItem(tmpl, k), where + fmt("[%zu]", k)));
            return a;
        }
        case JNode::Object: break;
    }
    if (n.obj.size() == 1 && !n.obj[0].first.empty() && n.obj[0].first[0] == '!') {
        auto& [k, x] = n.obj[0];
        try {
            if (k == "!u32") {
                int64_t v = webInt(x, where);
                if (v < 0 || v > UINT32_MAX) throw Error(fmt("%lld hors des limites d'un u32", (long long)v));
                return Value::u32(uint32_t(v));
            }
            if (k == "!i64") return Value::i64(webInt(x, where));
            if (k == "!u64") return Value::u64(webU64(x, where));
            if (k == "!f64") return Value::f64(x.k == JNode::String ? std::strtod(x.s.c_str(), nullptr) : x.num());
            if (k == "!f32") return Value::f32(x.k == JNode::String ? std::strtod(x.s.c_str(), nullptr) : x.num());
            if (k == "!f32bits") { uint32_t u = uint32_t(webInt(x, where)); float f; std::memcpy(&f, &u, 4); return Value::f32(f); }
            if (k == "!bin") return Value::binary(unhex(x.s, where));
            if (k == "!file") {
                if (x.k != JNode::Array || x.arr.size() != 2) throw Error("[nombre, \"hexadécimal\"] attendu");
                return Value::file(unhex(x.arr[1].s, where), uint32_t(webInt(x.arr[0], where)));
            }
            if (k == "!h32") {
                Value h = Value::hash();
                for (auto& [hk, y] : x.obj) {
                    uint32_t key = uint32_t(std::stoul(hk, nullptr, 16));
                    const Value* old = tmpl && tmpl->t == T::Hash && tmpl->h->count(key) ? &tmpl->h->at(key) : nullptr;
                    (*h.h)[key] = fromWeb(y, old, where + "." + hk);
                }
                return h;
            }
            if (k == "!vh32") {
                Value h = Value::vhash();
                for (auto& [hk, y] : x.obj) {
                    uint32_t key = uint32_t(std::stoul(hk, nullptr, 16));
                    if (y.k != JNode::Array || y.arr.size() != 2) throw Error("[valeur, nombre] attendu");
                    const Value* old = tmpl && tmpl->t == T::VHash && tmpl->vh->count(key) ? &tmpl->vh->at(key).first : nullptr;
                    (*h.vh)[key] = {fromWeb(y.arr[0], old, where + "." + hk), uint32_t(webInt(y.arr[1], where))};
                }
                return h;
            }
            if (k == "!map") {
                Value m = Value::map();
                for (auto& [mk, y] : x.obj) (*m.d)[mk] = fromWeb(y, tmpl ? tmpl->get(mk) : nullptr, where + "." + mk);
                return m;
            }
        } catch (const Error& e) {
            throw Error(where + " : valeur " + k + " mal écrite (" + e.what() + ")");
        } catch (const std::exception&) {
            throw Error(where + " : valeur " + k + " mal écrite");
        }
        throw Error(where + " : étiquette inconnue " + k);
    }
    Value m = Value::map();
    const Value* tm = tmpl && tmpl->t == T::Map ? tmpl : nullptr;
    for (auto& [mk, y] : n.obj) (*m.d)[mk] = fromWeb(y, tm ? tm->get(mk) : nullptr, where + "." + mk);
    return m;
}

}  // namespace te
