#include "core/effects.hpp"

#include <cstring>

#include "core/byml.hpp"
#include "core/util.hpp"

namespace te {

const char* fxKindName(FxKind k) {
    switch (k) {
        case FxKind::Leaf: return "feuilles qui tombent";
        case FxKind::Fog: return "brume";
        case FxKind::Ripple: return "rides sur l'eau";
        case FxKind::Spray: return "embruns de cascade";
        case FxKind::Glow: return "lueurs (insectes, poussières)";
        case FxKind::Shaft: return "rayons de lumière";
        case FxKind::Smoke: return "fumée";
        case FxKind::Fire: return "flammes";
        case FxKind::Drip: return "gouttes";
        case FxKind::Fish: return "bancs de poissons";
        case FxKind::Hidden: return "masque de brume (invisible en jeu : il retire la brume volumique autour de lui)";
        case FxKind::Abyss: return "abîme sombre";
        case FxKind::Bubbles: return "bulles et courant sous l'eau";
        case FxKind::Debris: return "débris rocheux en suspension";
        case FxKind::Aura: return "aura du Chant de Saria";
        case FxKind::Aurora: return "aurore";
        case FxKind::Whirlpool: return "tourbillon";
        default: return "particules";
    }
}

static uint32_t u32at(const Bytes& b, size_t o) { uint32_t v; std::memcpy(&v, b.data() + o, 4); return v; }
static uint64_t u64at(const Bytes& b, size_t o) { uint64_t v; std::memcpy(&v, b.data() + o, 8); return v; }

// Noms d'émetteurs et de modules du binaire VFX3 : les fiches de modules (dans la section « Default ») pointent sur
// deux chaînes de la réserve « StringPool » qui se suivent : le module puis son émetteur.
static void readVfx3(const Bytes& p, EffectInfo& e) {
    if (p.size() < 0x100 || std::memcmp(p.data(), "BFFH", 4) != 0) return;
    size_t defOff = 0, defSize = 0, poolOff = 0, poolSize = 0;
    for (size_t o = 0; o + 0x28 <= std::min<size_t>(p.size(), 0x1000); o += 4) {
        if (std::memcmp(p.data() + o, "BFSI", 4) != 0) continue;
        size_t off = u32at(p, o + 4), size = u32at(p, o + 8);
        std::string name(reinterpret_cast<const char*>(p.data() + o + 0x18), strnlen(reinterpret_cast<const char*>(p.data() + o + 0x18), 16));
        if (off + size > p.size()) continue;
        if (name == "Default") { defOff = off; defSize = size; }
        if (name == "StringPool") { poolOff = off; poolSize = size; }
    }
    if (!poolSize || !defSize) return;
    auto str = [&](uint64_t v, std::string& out) {
        if (v < poolOff || v >= poolOff + poolSize) return false;
        const char* s = reinterpret_cast<const char*>(p.data() + v);
        out.assign(s, strnlen(s, poolOff + poolSize - v));
        return !out.empty();
    };
    auto ident = [](const std::string& s) {
        if (s.empty() || !std::isalpha(uint8_t(s[0]))) return false;
        for (char c : s) if (!std::isalnum(uint8_t(c)) && c != '_') return false;
        return !startsWith(s, "NnVfx3");
    };
    std::set<std::string> em;
    for (size_t o = defOff + 8; o + 16 <= defOff + defSize; o += 8) {
        std::string a, b;
        if (!str(u64at(p, o), a) || !str(u64at(p, o + 8), b)) continue;
        if ((u32at(p, o - 8) & 0xFFFF) != 1 || !ident(a) || !ident(b)) continue;
        e.modules.insert(a);
        if (em.insert(b).second) e.emitters.push_back(b);
    }
}

static bool has(const std::string& s, std::initializer_list<const char*> keys) {
    for (auto k : keys) if (s.find(k) != std::string::npos) return true;
    return false;
}

EffectInfo effectParse(const Bytes& vsetb, const std::string& name) {
    EffectInfo e;
    e.name = name;
    Document doc;
    if (!vsetb.empty()) {
        doc = bymlLoad(vsetb);
        if (!doc.root.isMap()) throw Error("fichier d'effet illisible (racine)");
    }
    const Value& r = doc.root;
    if (r.isMap()) {
    if (auto es = r.get("Esets"); es && es->isArray())
        for (auto& v : *es->a) if (v.t == T::String) e.esets.push_back(v.s);
    if (auto tx = r.get("Textures"); tx && tx->isArray())
        for (auto& v : *tx->a)
            if (v.isMap())
                if (auto n = v.get("name"); n && n->t == T::String) e.textures.push_back(n->s);
    if (auto pb = r.get("PtclBin"); pb && (pb->t == T::File || pb->t == T::Binary)) {
        Bytes p(pb->s.begin(), pb->s.end());
        readVfx3(p, e);
    }
    }
    // (famille de l'effet : d'après son nom d'abord — « MidFog » reste une brume même si ses émetteurs s'appellent
    // « Smoke » —, puis d'après ses ensembles et émetteurs, puis ses modules)
    std::string all;
    for (auto& s : e.esets) all += " " + lower(s);
    for (auto& s : e.emitters) all += " " + lower(s);
    auto classify = [&](const std::string& t) {
        if (has(t, {"removedensity", "remove2dv"})) return FxKind::Hidden;
        if (has(t, {"ripple"})) return FxKind::Ripple;
        if (has(t, {"fishmigration", "fish_school", "_fish"})) return FxKind::Fish;
        if (has(t, {"leaf", "petal", "confetti", "feather"})) return FxKind::Leaf;
        if (has(t, {"lightshaft", "shaft", "sunbeam", "beam"})) return FxKind::Shaft;
        if (has(t, {"dryice"})) return FxKind::Fog;   // (cascade : nappe de brume au sol)
        if (has(t, {"waterfall", "splash", "spray", "foam", "basin"})) return FxKind::Spray;
        if (has(t, {"drip", "drop"})) return FxKind::Drip;
        if (has(t, {"fire", "flame", "torch", "candle", "burn"})) return FxKind::Fire;
        if (has(t, {"fog", "mist", "haze", "cloud", "volume"})) return FxKind::Fog;
        if (has(t, {"smoke", "steam", "chimney"})) return FxKind::Smoke;
        if (has(t, {"insect", "firefly", "glow", "light", "sparkle", "fairy", "spore", "dust", "pollen", "twinkle"})) return FxKind::Glow;
        if (has(t, {"abyss"})) return FxKind::Abyss;
        if (has(t, {"whirlpool"})) return FxKind::Whirlpool;
        if (has(t, {"aurora"})) return FxKind::Aurora;
        if (has(t, {"songofsaria", "ocarina"})) return FxKind::Aura;
        if (has(t, {"divinggame", "waterflow", "bubble"})) return FxKind::Bubbles;
        if (has(t, {"finalbattlefield_env", "debris", "erosion"})) return FxKind::Debris;
        if (has(t, {"_exit"})) return FxKind::Glow;
        return FxKind::Generic;
    };
    e.kind = classify(lower(name));
    if (e.kind == FxKind::Generic) e.kind = classify(all);
    if (e.kind == FxKind::Generic && e.modules.count("Confetti")) e.kind = FxKind::Leaf;
    // (textures : une de couleur avec transparence, sinon une d'intensité servant de masque)
    for (auto& t : e.textures) {
        if (e.color.empty() && (has(t, {"_Fca", "_Fc", "_Alb"}) && !has(t, {"_Nrm"}))) e.color = t;
        if (e.mask.empty() && has(t, {"_Fi"}) && !has(t, {"Noise_3d"})) e.mask = t;
    }
    // (un masque de bruit ne dessine pas une forme : on lui préfère un autre masque du fichier, s'il y en a un)
    if (startsWith(e.mask, "Noise_"))
        for (auto& t : e.textures)
            if (has(t, {"_Fi"}) && !startsWith(t, "Noise_")) { e.mask = t; break; }
    return e;
}

// Effet permanent ? Toujours pour un emplacement EftLoc ; pour un autre acteur, seulement si un de ses ensembles
// tourne en continu (« …_Move_00 » des insectes, « …_Candle_00 », « …PointLight… », « …_Loop… ») : les autres
// (éclaboussures, poussière de course…) ne viennent que pendant une action. La famille est alors tirée de ces seuls ensembles.
static void markAmbient(EffectInfo& e) {
    if (startsWith(e.actor, "EftLoc")) { e.ambient = true; return; }
    std::string keep;
    for (auto& s : e.esets) {
        std::string l = lower(s);
        bool move = l.size() > 8 && l.compare(l.size() - 8, 5, "_move") == 0;   // (« _Move_00 »)
        if (move || has(l, {"candle", "pointlight", "_loop", "torch", "flame_00"})) keep += " " + l;
    }
    if (keep.empty()) return;
    e.ambient = true;
    if (has(keep, {"candle", "torch", "flame"})) e.kind = FxKind::Fire;
    else if (has(keep, {"insect", "pointlight", "firefly", "fairy", "glow"})) e.kind = FxKind::Glow;
}

// (UserName de l'ELinkParam de l'acteur, en suivant ses parents ; à défaut le nom de l'acteur)
static void allStrings(const Value& v, std::vector<std::string>& out) {
    if (v.t == T::String && !v.s.empty()) out.push_back(v.s);
    else if (v.isMap()) { for (auto& [k, c] : *v.d) if (k != "$parent") allStrings(c, out); }
    else if (v.isArray()) for (auto& c : *v.a) allStrings(c, out);
}

static void findUserName(const Value& v, std::string& out) {
    if (!out.empty()) return;
    if (v.isMap()) {
        if (auto u = v.get("UserName"); u && u->t == T::String && !u->s.empty()) { out = u->s; return; }
        for (auto& [k, c] : *v.d) findUserName(c, out);
    } else if (v.isArray()) {
        for (auto& c : *v.a) findUserName(c, out);
    }
}

EffectInfo effectLoad(Game& g, const std::string& actor, const std::string& model) {
    std::string user;
    std::vector<std::string> elink;
    std::string rel = "Pack/Actor/" + actor + ".pack.zs";
    if (g.exists(rel)) {
        if (auto p = g.pack(rel)) {
            for (auto& [n, d] : p->files) {
                if (n.find("ELinkParam") == std::string::npos) continue;
                try {
                    Value root = bymlLoad(g.zs.unpack(d)).root;
                    findUserName(root, user);
                    allStrings(root, elink);
                } catch (...) {}
                if (!user.empty()) break;
            }
        }
    }
    std::vector<std::string> tries;
    if (!user.empty()) tries.push_back(user);
    if (user != actor) tries.push_back(actor);
    // (effets des objets du décor rangés au nom de leur modèle : chandeliers, bougies…)
    if (!model.empty()) tries.push_back("CmnTex_Map_" + model);
    tries.push_back("CmnTex_Map_" + actor);
    std::string why;
    for (auto& n : tries) {
        std::string f = "Effect/" + n + ".Nin_Ounce_NVN2.vsetb.byml.zs";
        if (!g.exists(f)) continue;
        try {
            EffectInfo e = effectParse(g.read(f), n);
            e.actor = actor;
            e.file = f;
            markAmbient(e);
            return e;
        } catch (const std::exception& x) {
            why = f + " illisible : " + x.what();
        }
    }
    if (why.empty())
        why = "Effect/" + (user.empty() ? actor : user) + ".Nin_Ounce_NVN2.vsetb.byml.zs absent" +
              (user.empty() ? std::string(" (pas de UserName dans l'ELinkParam)") : "");
    if (!elink.empty()) {
        std::string l;
        for (auto& x : elink) if (l.size() < 300) l += (l.empty() ? "" : ", ") + x;
        why += " ; ELink de l'acteur : " + l;
    }
    // (famille devinée du nom seul, avec les textures que le jeu emploie d'habitude pour elle)
    EffectInfo e = effectParse(Bytes{}, user.empty() ? actor : user);
    e.actor = actor;
    e.why = why;
    e.guessed = true;
    switch (e.kind) {
        case FxKind::Fog: e.mask = "Smoke_Fog_00_Fia"; break;
        case FxKind::Leaf: e.color = "LeafFX_Texture_00_Green_Fca"; break;
        case FxKind::Ripple: e.mask = "Liquid_Drop_Ptcls_01_FiL"; break;
        case FxKind::Spray: e.mask = "Liquid_Splash_00_FiL"; break;
        case FxKind::Glow: e.mask = "Glow_Sphere_00_Fi"; break;
        case FxKind::Smoke: e.color = "Smoke_Thin_00_Fca"; break;
        case FxKind::Fire: e.color = "Fire_Candle_01_Fca"; e.mask = "Glow_Sphere_00_Fi"; break;
        case FxKind::Fish: e.mask = "Shape_Animal_Fish_00_Fia"; break;
        case FxKind::Bubbles: e.mask = "Liquid_Bubble_00_Fi"; break;
        default: break;
    }
    markAmbient(e);
    return e;
}

}  // namespace te
