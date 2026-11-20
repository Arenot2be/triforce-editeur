#include "core/foliage.hpp"

#include <cmath>
#include <cstring>

#include "core/util.hpp"

namespace te {

FoliageSet foliageParse(const Bytes& d, const std::string& layer) {
    FoliageSet out;
    if (d.size() < 0x20) throw Error("fichier trop court");
    uint32_t nt = rd32(d, 0), ni = rd32(d, 4), ver = rd32(d, 8);
    size_t base = 0x20 + size_t(nt) * 0x100;
    if (ver != 2 || nt == 0 || nt > 4096 || base + size_t(ni) * 64 > d.size()) throw Error("pas une liste d'instances (couverture du sol ?)");
    for (uint32_t i = 0; i < nt; i++) {
        size_t o = 0x20 + size_t(i) * 0x100;
        size_t L = d[o];
        if (L == 0 || L > 0xff) throw Error("fiche de sorte illisible");
        FoliageType t;
        t.res = std::string(reinterpret_cast<const char*>(d.data() + o + 1), L);
        std::string stem = t.res.substr(t.res.rfind('/') == std::string::npos ? 0 : t.res.rfind('/') + 1);
        t.model = stem.substr(0, stem.find('.'));
        t.layer = layer;
        out.types.push_back(std::move(t));
    }
    for (uint32_t i = 0; i < ni; i++) {
        size_t o = base + size_t(i) * 64;
        FoliageInst f;
        std::memcpy(f.p, d.data() + o, 12);
        std::memcpy(&f.s, d.data() + o + 12, 4);
        std::memcpy(f.x, d.data() + o + 16, 12);
        std::memcpy(f.y, d.data() + o + 32, 12);
        uint16_t t = uint16_t(rd16(d, o + 48));
        if (t >= nt || !std::isfinite(f.p[0]) || !std::isfinite(f.s)) continue;
        out.types[t].inst.push_back(f);
        out.count++;
    }
    return out;
}

FoliageSet foliageLoad(Game& g, const std::string& pre, std::string* status) {
    FoliageSet all;
    int files = 0;
    std::string bad;
    for (auto& f : g.allFiles()) {
        if (f.rfind("Foliage/Placement/Scene/", 0) != 0 || !endsWith(f, ".flnt.zs")) continue;
        std::string base = f.substr(f.rfind('/') + 1);
        if (base.rfind(pre + ".", 0) != 0) continue;   // (« KokiriForest_Child.… » ; pas « KokiriForest_Adult.… »)
        std::string layer = base.substr(0, base.size() - 8);
        layer = layer.substr(layer.rfind('.') + 1);
        try {
            FoliageSet s = foliageParse(g.read(f), layer);
            for (auto& t : s.types) all.types.push_back(std::move(t));
            all.count += s.count;
            files++;
        } catch (const std::exception&) {
            all.groundCover.push_back(layer);
        }
    }
    if (status) {
        *status = fmt("feuillage : %zu plantes de %zu sortes (%d fichier(s))", all.count, all.types.size(), files);
        if (!all.groundCover.empty()) {
            std::string l;
            for (auto& c : all.groundCover) l += (l.empty() ? "" : ", ") + c;
            *status += " ; couverture du sol pas encore lue : " + l;
        }
    }
    return all;
}

}  // namespace te
