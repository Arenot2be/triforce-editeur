// BYML (versions 2 à 7, petit- et gros-boutiste) : lecture et écriture, comme triforce_modkit/byml.py.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/util.hpp"

namespace te {

enum class T : uint8_t {
    Hash = 0x20, VHash = 0x21, String = 0xA0, Binary = 0xA1, File = 0xA2, Array = 0xC0, Map = 0xC1,
    Bool = 0xD0, I32 = 0xD1, F32 = 0xD2, U32 = 0xD3, I64 = 0xD4, U64 = 0xD5, F64 = 0xD6, Null = 0xFF
};

struct Value;
using Array = std::vector<Value>;
using Dict = std::map<std::string, Value>;               // (ordre octet par octet, celui de l'écriture)
using HashDict = std::map<uint32_t, Value>;
using VHashDict = std::map<uint32_t, std::pair<Value, uint32_t>>;

struct Value {
    T t = T::Null;
    bool b = false;
    int64_t i = 0;     // I32, U32, I64 ; U64 dans u
    uint64_t u = 0;
    double f = 0;      // F32 (déjà arrondi à 32 bits), F64
    std::string s;     // String ; octets de Binary et File
    uint32_t unk = 0x1000;   // File
    std::unique_ptr<Array> a;
    std::unique_ptr<Dict> d;
    std::unique_ptr<HashDict> h;
    std::unique_ptr<VHashDict> vh;

    Value() = default;
    Value(const Value& o) { *this = o; }
    Value(Value&&) noexcept = default;
    Value& operator=(const Value& o);
    Value& operator=(Value&&) noexcept = default;

    static Value null() { return {}; }
    static Value boolean(bool v) { Value x; x.t = T::Bool; x.b = v; return x; }
    static Value i32(int64_t v) { Value x; x.t = T::I32; x.i = v; return x; }
    static Value u32(uint32_t v) { Value x; x.t = T::U32; x.i = v; return x; }
    static Value i64(int64_t v) { Value x; x.t = T::I64; x.i = v; return x; }
    static Value u64(uint64_t v) { Value x; x.t = T::U64; x.u = v; return x; }
    static Value f32(double v) { Value x; x.t = T::F32; x.f = double(float(v)); return x; }
    static Value f64(double v) { Value x; x.t = T::F64; x.f = v; return x; }
    static Value str(std::string v) { Value x; x.t = T::String; x.s = std::move(v); return x; }
    static Value binary(std::string v) { Value x; x.t = T::Binary; x.s = std::move(v); return x; }
    static Value file(std::string v, uint32_t unk) { Value x; x.t = T::File; x.s = std::move(v); x.unk = unk; return x; }
    static Value array() { Value x; x.t = T::Array; x.a = std::make_unique<Array>(); return x; }
    static Value map() { Value x; x.t = T::Map; x.d = std::make_unique<Dict>(); return x; }
    static Value hash() { Value x; x.t = T::Hash; x.h = std::make_unique<HashDict>(); return x; }
    static Value vhash() { Value x; x.t = T::VHash; x.vh = std::make_unique<VHashDict>(); return x; }

    bool isMap() const { return t == T::Map; }
    bool isArray() const { return t == T::Array; }
    bool isNum() const { return t == T::F32 || t == T::F64 || t == T::I32; }
    bool isContainer() const { return t == T::Array || t == T::Map || t == T::Hash || t == T::VHash; }
    double num() const;   // (F32, F64, entiers ; NaN sinon)
    const Value* get(const std::string& k) const;
    Value* get(const std::string& k);
};

bool same(const Value& a, const Value& b);   // (égalité, types compris)

struct Document {
    Value root;
    uint16_t version = 7;
    bool big = false;
};

Document bymlLoad(const Bytes& b);
Bytes bymlDump(const Document& doc);
inline bool isByml(const Bytes& b) { return b.size() >= 2 && ((b[0] == 'B' && b[1] == 'Y') || (b[0] == 'Y' && b[1] == 'B')); }

}  // namespace te
