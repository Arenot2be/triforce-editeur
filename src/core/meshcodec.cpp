#include "core/meshcodec.hpp"

#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "mc_MeshCodec.h"

namespace te {

namespace {

constexpr uint32_t MCPK = 0x4b50434d, FMSH = 0x48534d46;
constexpr size_t BASE_ALIGN = 0x1000;   // (le tampon de sortie est aligné comme l'aurait fait le jeu)

size_t alignUp(size_t v, size_t a) { return a ? (v + a - 1) / a * a : v; }

struct Aligned {
    uint8_t* p = nullptr;
    explicit Aligned(size_t n) {
        n = alignUp(std::max<size_t>(n, 1), BASE_ALIGN);
#ifdef _WIN32
        p = static_cast<uint8_t*>(_aligned_malloc(n, BASE_ALIGN));
#else
        p = static_cast<uint8_t*>(std::aligned_alloc(BASE_ALIGN, n));
#endif
        if (!p) throw Error("mémoire insuffisante (MeshCodec)");
        std::memset(p, 0, n);
    }
    ~Aligned() {
#ifdef _WIN32
        _aligned_free(p);
#else
        std::free(p);
#endif
    }
    Aligned(const Aligned&) = delete;
    Aligned& operator=(const Aligned&) = delete;
};

struct DCtx {
    ZSTD_DCtx* c = ZSTD_createDCtx();
    ~DCtx() { ZSTD_freeDCtx(c); }
};

}  // namespace

bool isMeshCodec(const Bytes& raw) { return raw.size() >= 12 && rd32(raw, 0) == MCPK; }

Bytes meshCodecUnpack(const Bytes& raw) {
    if (!isMeshCodec(raw)) throw Error("pas un fichier MeshCodec (MCPK)");
    uint16_t verMajor = rd16(raw, 6);
    uint8_t verMinor = raw[5];
    uint32_t flags = rd32(raw, 8);
    if (verMajor == 1)
        throw Error("MeshCodec 1.0 : géométrie dans une compression inconnue ; le jeu n'ouvre pas ce fichier "
                    "(il charge le .bfres.zs du même modèle, absent ici)");
    if (verMajor != 0 || verMinor > 1) throw Error(fmt("MeshCodec version %u.%u non prise en charge", verMajor, verMinor));
    size_t outSize = size_t(flags >> 5) << (flags & 0xf);
    if (outSize < 0x100 || outSize > (size_t(1) << 31)) throw Error(fmt("MeshCodec : taille décompressée incohérente (%zu)", outSize));

    // 1. le BFRES, compressé par zstd (bloc par bloc, comme le jeu)
    Aligned dst(outSize);
    DCtx dctx;
    if (!dctx.c) throw Error("zstd : contexte non créé");
    ZSTD_DCtx_setParameter(dctx.c, ZSTD_d_experimentalParam1, 1);   // (ZSTD_d_format : trames sans nombre magique)
    ZSTD_decompressBegin(dctx.c);
    const uint8_t* ptr = raw.data() + 12;
    size_t remaining = raw.size() - 12, outLeft = outSize, next = 1;
    uint8_t* out = dst.p;
    do {
        if (next > remaining) throw Error("MeshCodec : données zstd tronquées");
        size_t got = ZSTD_decompressContinue(dctx.c, out, outLeft, ptr, next);
        if (ZSTD_isError(got)) throw Error(std::string("MeshCodec : zstd : ") + ZSTD_getErrorName(got));
        ptr += next;
        remaining -= next;
        out += got;
        outLeft -= got;
        next = ZSTD_nextSrcSizeToDecompress(dctx.c);
    } while (next != 0);

    if (std::memcmp(dst.p, "FRES", 4) != 0) throw Error("MeshCodec : le contenu n'est pas un BFRES");
    uint32_t fileSize;
    std::memcpy(&fileSize, dst.p + 0x1C, 4);
    if (fileSize < 0x100 || fileSize > outSize) throw Error("MeshCodec : taille du BFRES incohérente");
    bool hasFmsh = (dst.p[0xEE] >> 3) & 1;
    if (!hasFmsh) return Bytes(dst.p, dst.p + fileSize);

    // 2. la section FMSH (alignée sur 4 après les données zstd) : les tampons d'index puis de sommets
    size_t at = alignUp(size_t(ptr - raw.data()), 4);
    if (at + sizeof(mc::ResMeshCodecHeader) > raw.size()) throw Error("MeshCodec : section FMSH absente");
    auto* fmsh = reinterpret_cast<const mc::ResMeshCodecHeader*>(raw.data() + at);
    if (fmsh->magic != FMSH) throw Error("MeshCodec : section FMSH introuvable");
    std::memset(dst.p + fileSize, 0, outSize - fileSize);   // (zstd écrit parfois au-delà ; voir MeshCodec)
    size_t align = std::max<size_t>({fmsh->vertexAlign, fmsh->indexAlign, 1});
    size_t sizeHeader = alignUp(fileSize, 8);
    size_t outOff = alignUp(sizeHeader + 0x120, align);
    size_t need = alignUp(outOff + fmsh->indexOutputSize, std::max<size_t>(fmsh->vertexAlign, 1)) + fmsh->vertexOutputSize;
    if (need > outSize) throw Error("MeshCodec : les tampons décodés dépassent la taille annoncée");
    uint32_t hdr[2] = {uint32_t(outOff), uint32_t(outSize)};
    std::memcpy(dst.p + sizeHeader, hdr, 8);

    size_t work = std::max<size_t>(fmsh->workMemSize, 1);
    if (work > (size_t(1) << 30)) throw Error("MeshCodec : mémoire de travail démesurée");
    Aligned workMem(work);
    uint32_t res = mc::DecompressFMSH(dst.p + outOff, fmsh->vertexOutputSize + fmsh->indexOutputSize, fmsh,
                                      raw.size() - at, workMem.p);
    if (res != 0) throw Error(fmt("MeshCodec : décodage des tampons en échec (code %u)", res));
    return Bytes(dst.p, dst.p + need);
}

}  // namespace te
