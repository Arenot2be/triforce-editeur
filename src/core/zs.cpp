#include "core/zs.hpp"

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include "core/sarc.hpp"

namespace te {

struct Zs::Dict {
    uint32_t id = 0;
    Bytes raw;
    ZSTD_DDict* d = nullptr;
    std::mutex m;
    std::map<int, ZSTD_CDict*> c;   // (par niveau)
    ~Dict() {
        if (d) ZSTD_freeDDict(d);
        for (auto& [k, v] : c) ZSTD_freeCDict(v);
    }
};

Zs::~Zs() = default;

Zs::Zs(const fs::path& romfs) {
    fs::path p = romfs / u8path("Pack") / u8path("ZsDic.pack.zs");
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return;
    Bytes raw = readFile(p);
    Bytes b = unpackPlain(raw.data(), raw.size());
    Sarc s = Sarc::load(b);
    for (auto& [name, data] : s.files) {
        auto d = std::make_shared<Dict>();
        d->raw = data;
        d->d = ZSTD_createDDict(data.data(), data.size());
        d->id = ZSTD_getDictID_fromDict(data.data(), data.size());
        if (!d->d) continue;
        byId_[d->id] = d;
        auto slash = name.rfind('/');
        byName_[slash == std::string::npos ? name : name.substr(slash + 1)] = d;
    }
}

uint32_t Zs::dictId(const Bytes& raw) {
    if (!isZstd(raw)) return 0;
    return ZSTD_getDictID_fromFrame(raw.data(), raw.size());
}

static Bytes stream(ZSTD_DCtx* dc, const uint8_t* p, size_t n, size_t limit = SIZE_MAX) {
    Bytes out;
    unsigned long long known = ZSTD_getFrameContentSize(p, n);
    if (known != ZSTD_CONTENTSIZE_UNKNOWN && known != ZSTD_CONTENTSIZE_ERROR && known < (1ull << 31)) out.reserve(size_t(known));
    ZSTD_inBuffer in{p, n, 0};
    std::vector<uint8_t> buf(1 << 17);
    while (true) {
        ZSTD_outBuffer o{buf.data(), buf.size(), 0};
        size_t r = ZSTD_decompressStream(dc, &o, &in);
        if (ZSTD_isError(r)) throw Error(std::string("zstd : ") + ZSTD_getErrorName(r));
        out.insert(out.end(), buf.data(), buf.data() + o.pos);
        if (out.size() >= limit) break;
        if (r == 0) {   // (fin d'une trame : d'autres peuvent suivre)
            if (in.pos >= in.size) break;
            continue;
        }
        if (in.pos >= in.size && o.pos < o.size) break;
    }
    return out;
}

Bytes Zs::unpackPlain(const uint8_t* p, size_t n) {
    ZSTD_DCtx* dc = ZSTD_createDCtx();
    ZSTD_DCtx_setParameter(dc, ZSTD_d_windowLogMax, 31);
    try {
        Bytes out = stream(dc, p, n);
        ZSTD_freeDCtx(dc);
        return out;
    } catch (...) {
        ZSTD_freeDCtx(dc);
        throw;
    }
}

Bytes Zs::unpack(const Bytes& raw, size_t limit) const {
    if (!isZstd(raw)) return raw;
    uint32_t id = dictId(raw);
    ZSTD_DCtx* dc = ZSTD_createDCtx();
    ZSTD_DCtx_setParameter(dc, ZSTD_d_windowLogMax, 31);
    if (id) {
        auto it = byId_.find(id);
        if (it == byId_.end()) { ZSTD_freeDCtx(dc); throw Error(fmt("dictionnaire zstd %u inconnu", id)); }
        ZSTD_DCtx_refDDict(dc, it->second->d);
    }
    try {
        Bytes out = stream(dc, raw.data(), raw.size(), limit);
        ZSTD_freeDCtx(dc);
        return out;
    } catch (...) {
        ZSTD_freeDCtx(dc);
        throw;
    }
}

Bytes Zs::pack(const Bytes& data, uint32_t id, int level) const {
    ZSTD_CCtx* cc = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(cc, ZSTD_c_compressionLevel, level);
    ZSTD_CCtx_setParameter(cc, ZSTD_c_contentSizeFlag, 1);
    ZSTD_CCtx_setParameter(cc, ZSTD_c_checksumFlag, 0);
    auto it = id ? byId_.find(id) : byId_.end();
    if (it != byId_.end()) {
        auto& d = *it->second;
        std::lock_guard<std::mutex> lk(d.m);
        auto& cd = d.c[level];
        if (!cd) cd = ZSTD_createCDict(d.raw.data(), d.raw.size(), level);
        ZSTD_CCtx_refCDict(cc, cd);
    }
    Bytes out(ZSTD_compressBound(data.size()));
    size_t n = ZSTD_compress2(cc, out.data(), out.size(), data.data(), data.size());
    ZSTD_freeCCtx(cc);
    if (ZSTD_isError(n)) throw Error(std::string("zstd : ") + ZSTD_getErrorName(n));
    out.resize(n);
    return out;
}

uint32_t Zs::dictForName(const std::string& name) const {
    static const std::pair<const char*, const char*> forName[] = {
        {".bcett.byml.zs", "bcett.byml.zsdic"}, {".pack.zs", "pack.zsdic"}, {".bkprb.zs", "bkprb.zsdic"}};
    for (auto& [suffix, dn] : forName) {
        auto it = byName_.find(dn);
        if (endsWith(name, suffix) && it != byName_.end()) return it->second->id;
    }
    auto it = byName_.find("zs.zsdic");
    return it != byName_.end() ? it->second->id : 0;
}

}  // namespace te
