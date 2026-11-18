#include "core/byml.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace te {

Value& Value::operator=(const Value& o) {
    if (this == &o) return *this;
    t = o.t; b = o.b; i = o.i; u = o.u; f = o.f; s = o.s; unk = o.unk;
    a = o.a ? std::make_unique<Array>(*o.a) : nullptr;
    d = o.d ? std::make_unique<Dict>(*o.d) : nullptr;
    h = o.h ? std::make_unique<HashDict>(*o.h) : nullptr;
    vh = o.vh ? std::make_unique<VHashDict>(*o.vh) : nullptr;
    return *this;
}

double Value::num() const {
    switch (t) {
        case T::F32: case T::F64: return f;
        case T::I32: case T::U32: case T::I64: return double(i);
        case T::U64: return double(u);
        default: return NAN;
    }
}
const Value* Value::get(const std::string& k) const {
    if (t != T::Map) return nullptr;
    auto it = d->find(k);
    return it == d->end() ? nullptr : &it->second;
}
Value* Value::get(const std::string& k) {
    if (t != T::Map) return nullptr;
    auto it = d->find(k);
    return it == d->end() ? nullptr : &it->second;
}

bool same(const Value& a, const Value& b) {
    if (a.t != b.t) return false;
    switch (a.t) {
        case T::Map:
            if (a.d->size() != b.d->size()) return false;
            for (auto ia = a.d->begin(), ib = b.d->begin(); ia != a.d->end(); ++ia, ++ib)
                if (ia->first != ib->first || !same(ia->second, ib->second)) return false;
            return true;
        case T::Hash:
            if (a.h->size() != b.h->size()) return false;
            for (auto ia = a.h->begin(), ib = b.h->begin(); ia != a.h->end(); ++ia, ++ib)
                if (ia->first != ib->first || !same(ia->second, ib->second)) return false;
            return true;
        case T::VHash:
            if (a.vh->size() != b.vh->size()) return false;
            for (auto ia = a.vh->begin(), ib = b.vh->begin(); ia != a.vh->end(); ++ia, ++ib)
                if (ia->first != ib->first || ia->second.second != ib->second.second || !same(ia->second.first, ib->second.first))
                    return false;
            return true;
        case T::Array:
            if (a.a->size() != b.a->size()) return false;
            for (size_t k = 0; k < a.a->size(); k++) if (!same((*a.a)[k], (*b.a)[k])) return false;
            return true;
        case T::F32: case T::F64:
            if (std::isnan(a.f) && std::isnan(b.f)) return true;
            return std::memcmp(&a.f, &b.f, 8) == 0;
        case T::File: return a.s == b.s && a.unk == b.unk;
        case T::String: case T::Binary: return a.s == b.s;
        case T::Bool: return a.b == b.b;
        case T::U64: return a.u == b.u;
        case T::Null: return true;
        default: return a.i == b.i;
    }
}

// ---------------------------------------------------------------- lecture
namespace {
struct Reader {
    const Bytes& b;
    bool big;
    std::vector<std::string> keys, strings;
    int depth = 0;
    uint32_t u32(size_t o) const { return big ? rd32be(b, o) : rd32(b, o); }
    uint32_t u24(size_t o) const {
        need(b, o, 3);
        return big ? uint32_t(b[o]) << 16 | uint32_t(b[o + 1]) << 8 | b[o + 2] : uint32_t(b[o]) | uint32_t(b[o + 1]) << 8 | uint32_t(b[o + 2]) << 16;
    }
    uint64_t u64(size_t o) const { return big ? uint64_t(u32(o)) << 32 | u32(o + 4) : uint64_t(u32(o)) | uint64_t(u32(o + 4)) << 32; }
    std::vector<std::string> table(size_t o) {
        if (rd8(b, o) != 0xC2) throw Error(fmt("BYML : table de chaînes attendue à %#zx", o));
        uint32_t n = u24(o + 1);
        std::vector<std::string> out;
        out.reserve(n);
        for (uint32_t k = 0; k < n; k++) {
            size_t s = o + u32(o + 4 + 4 * k);
            need(b, s, 0);
            auto e = std::find(b.begin() + long(s), b.end(), 0);
            if (e == b.end()) throw Error("BYML : chaîne sans fin");
            out.emplace_back(b.begin() + long(s), e);
        }
        return out;
    }
    Value value(uint8_t t, uint32_t raw) {
        Value v;
        switch (T(t)) {
            case T::Array: case T::Map: case T::Hash: case T::VHash: return container(raw);
            case T::String:
                if (raw >= strings.size()) throw Error("BYML : chaîne inconnue");
                return Value::str(strings[raw]);
            case T::Binary: { uint32_t n = u32(raw); need(b, raw + 4, n); return Value::binary(std::string(b.begin() + raw + 4, b.begin() + raw + 4 + n)); }
            case T::File: {
                uint32_t n = u32(raw), unk = u32(raw + 4);
                need(b, raw + 8, n);
                return Value::file(std::string(b.begin() + raw + 8, b.begin() + raw + 8 + n), unk);
            }
            case T::Bool: return Value::boolean(raw != 0);
            case T::I32: return Value::i32(int32_t(raw));
            case T::U32: return Value::u32(raw);
            case T::F32: { float f; std::memcpy(&f, &raw, 4); return Value::f32(f); }
            case T::I64: return Value::i64(int64_t(u64(raw)));
            case T::U64: return Value::u64(u64(raw));
            case T::F64: { uint64_t x = u64(raw); double f; std::memcpy(&f, &x, 8); return Value::f64(f); }
            case T::Null: return Value::null();
            default: throw Error(fmt("BYML : type de nœud inconnu %#x", t));
        }
    }
    Value container(size_t o) {
        if (++depth > 512) throw Error("BYML : imbrication trop profonde");
        uint8_t t = rd8(b, o);
        uint32_t n = u24(o + 1);
        Value v;
        if (t == 0xC0) {
            v = Value::array();
            need(b, o + 4, n);
            size_t vals = o + 4 + ((n + 3) & ~3u);
            v.a->reserve(n);
            for (uint32_t k = 0; k < n; k++) v.a->push_back(value(b[o + 4 + k], u32(vals + 4 * k)));
        } else if (t == 0xC1) {
            v = Value::map();
            for (uint32_t k = 0; k < n; k++) {
                size_t at = o + 4 + 8 * k;
                uint32_t ki = u24(at);
                if (ki >= keys.size()) throw Error("BYML : clé inconnue");
                (*v.d)[keys[ki]] = value(rd8(b, at + 3), u32(at + 4));
            }
        } else if (t == 0x20) {
            v = Value::hash();
            size_t types = o + 4 + 8 * size_t(n);
            for (uint32_t k = 0; k < n; k++) {
                size_t at = o + 4 + 8 * k;
                (*v.h)[u32(at)] = value(rd8(b, types + k), u32(at + 4));
            }
        } else if (t == 0x21) {
            v = Value::vhash();
            size_t types = o + 4 + 12 * size_t(n);
            for (uint32_t k = 0; k < n; k++) {
                size_t at = o + 4 + 12 * k;
                (*v.vh)[u32(at + 4)] = {value(rd8(b, types + k), u32(at)), u32(at + 8)};
            }
        } else {
            throw Error(fmt("BYML : conteneur de type %#x inconnu à %#zx", t, o));
        }
        depth--;
        return v;
    }
};
}  // namespace

Document bymlLoad(const Bytes& b) {
    if (b.size() < 0x10 || !isByml(b)) throw Error("pas un fichier BYML");
    Reader r{b, b[0] == 'B'};
    Document doc;
    doc.big = r.big;
    doc.version = r.big ? rd16be(b, 2) : rd16(b, 2);
    if (doc.version < 2 || doc.version > 7) throw Error(fmt("version BYML %d non prise en charge", doc.version));
    uint32_t keys = r.u32(4), strings = r.u32(8), root = r.u32(12);
    if (keys) r.keys = r.table(keys);
    if (strings) r.strings = r.table(strings);
    if (root) doc.root = r.container(root);
    return doc;
}

// ---------------------------------------------------------------- écriture
namespace {
void walk(const Value& v, std::set<std::string>& keys, std::set<std::string>& strings) {
    switch (v.t) {
        case T::Map: for (auto& [k, x] : *v.d) { keys.insert(k); walk(x, keys, strings); } break;
        case T::Hash: for (auto& [k, x] : *v.h) walk(x, keys, strings); break;
        case T::VHash: for (auto& [k, x] : *v.vh) walk(x.first, keys, strings); break;
        case T::Array: for (auto& x : *v.a) walk(x, keys, strings); break;
        case T::String: strings.insert(v.s); break;
        default: break;
    }
}

struct W {
    Writer w;
    std::map<std::string, uint32_t> keyIndex, strIndex;
    struct Later { size_t place; const Value* v; };

    size_t table(const std::set<std::string>& items) {
        w.align(4);
        size_t at = w.size();
        w.u8(0xC2);
        w.u24(uint32_t(items.size()));
        uint32_t o = uint32_t(4 + 4 * (items.size() + 1));
        for (auto& s : items) { w.u32(o); o += uint32_t(s.size() + 1); }
        w.u32(o);
        for (auto& s : items) { w.put(s); w.u8(0); }
        w.align(4);
        return at;
    }
    static bool inlineType(T t) {
        return t == T::String || t == T::Bool || t == T::I32 || t == T::U32 || t == T::F32 || t == T::Null;
    }
    uint32_t raw(const Value& v) {
        switch (v.t) {
            case T::String: return strIndex.at(v.s);
            case T::Bool: return v.b ? 1 : 0;
            case T::I32:
                if (v.i < INT32_MIN || v.i > INT32_MAX) throw Error(fmt("entier %lld hors des limites d'un I32 (utilise !u32, !i64 ou !u64)", (long long)v.i));
                return uint32_t(int32_t(v.i));
            case T::U32:
                if (v.i < 0 || v.i > UINT32_MAX) throw Error(fmt("entier %lld hors des limites d'un U32", (long long)v.i));
                return uint32_t(v.i);
            case T::F32: { float f = float(v.f); uint32_t u; std::memcpy(&u, &f, 4); return u; }
            default: return 0;
        }
    }
    void slot(const Value& x, std::vector<Later>& later) {
        if (inlineType(x.t)) w.u32(raw(x));
        else { later.push_back({w.size(), &x}); w.u32(0); }
    }
    size_t container(const Value& v) {
        w.align(4);
        size_t at = w.size();
        std::vector<Later> later;
        if (v.t == T::Array) {
            w.u8(0xC0); w.u24(uint32_t(v.a->size()));
            for (auto& x : *v.a) w.u8(uint8_t(x.t));
            w.align(4);
            for (auto& x : *v.a) slot(x, later);
        } else if (v.t == T::Map) {
            w.u8(0xC1); w.u24(uint32_t(v.d->size()));
            for (auto& [k, x] : *v.d) { w.u24(keyIndex.at(k)); w.u8(uint8_t(x.t)); slot(x, later); }
        } else if (v.t == T::Hash) {
            w.u8(0x20); w.u24(uint32_t(v.h->size()));
            for (auto& [k, x] : *v.h) { w.u32(k); slot(x, later); }
            for (auto& [k, x] : *v.h) w.u8(uint8_t(x.t));
            w.align(4);
        } else if (v.t == T::VHash) {
            w.u8(0x21); w.u24(uint32_t(v.vh->size()));
            for (auto& [k, x] : *v.vh) { slot(x.first, later); w.u32(k); w.u32(x.second); }
            for (auto& [k, x] : *v.vh) w.u8(uint8_t(x.first.t));
            w.align(4);
        } else {
            throw Error("BYML : conteneur attendu");
        }
        for (auto& l : later) w.set32(l.place, uint32_t(data(*l.v)));
        return at;
    }
    size_t data(const Value& x) {
        if (x.isContainer()) return container(x);
        w.align(4);
        size_t at = w.size();
        switch (x.t) {
            case T::Binary: w.u32(uint32_t(x.s.size())); w.put(x.s); break;
            case T::File: w.u32(uint32_t(x.s.size())); w.u32(x.unk); w.put(x.s); break;
            case T::I64: w.u64(uint64_t(x.i)); break;
            case T::U64: w.u64(x.u); break;
            case T::F64: { uint64_t u; std::memcpy(&u, &x.f, 8); w.u64(u); break; }
            default: break;
        }
        w.align(4);
        return at;
    }
};
}  // namespace

Bytes bymlDump(const Document& doc) {
    W x;
    x.w.big = doc.big;
    std::set<std::string> keys, strings;
    if (doc.root.t != T::Null) {
        if (!doc.root.isContainer()) throw Error("BYML : la racine doit être un dictionnaire ou une liste");
        walk(doc.root, keys, strings);
    }
    uint32_t k = 0;
    for (auto& s : keys) x.keyIndex[s] = k++;
    k = 0;
    for (auto& s : strings) x.strIndex[s] = k++;
    x.w.put(doc.big ? "BY" : "YB");
    x.w.u16(doc.version);
    for (int i = 0; i < 12; i++) x.w.u8(0);
    size_t keyAt = keys.empty() ? 0 : x.table(keys);
    size_t strAt = strings.empty() ? 0 : x.table(strings);
    size_t rootAt = doc.root.t == T::Null ? 0 : x.container(doc.root);
    x.w.set32(4, uint32_t(keyAt));
    x.w.set32(8, uint32_t(strAt));
    x.w.set32(12, uint32_t(rootAt));
    return x.w.out;
}

}  // namespace te
