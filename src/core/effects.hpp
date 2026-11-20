// Effets de particules des acteurs EftLoc_* : acteur -> ELinkParam (UserName) -> Effect/<nom>.Nin_Ounce_NVN2.vsetb.byml.zs.
// Le fichier d'effet (BYML) donne ses ensembles (Esets), ses textures (Tex/<nom>.bntx.zs) et un binaire VFX3 (PtclBin)
// dont on lit les noms d'émetteurs et de modules (FadeIn, GlobalWind, Confetti…). Les valeurs des modules sont rangées
// sous des noms hachés : le comportement est rendu d'après la famille de l'effet (feuilles, brume, rides, embruns…).
#pragma once
#include <set>
#include <string>
#include <vector>

#include "core/game.hpp"

namespace te {

enum class FxKind { Generic, Leaf, Fog, Ripple, Spray, Glow, Shaft, Smoke, Fire, Drip, Fish, Hidden, Abyss, Bubbles, Debris, Aura, Aurora, Whirlpool };

struct EffectInfo {
    std::string actor, name, file;              // (name : UserName de l'ELink ; file : chemin dans le jeu)
    std::vector<std::string> esets, textures;   // (textures : noms de Tex/, dans l'ordre du fichier)
    std::vector<std::string> emitters;          // (noms des émetteurs du binaire VFX3)
    std::set<std::string> modules;              // (modules utilisés par les émetteurs)
    FxKind kind = FxKind::Generic;
    std::string color, mask;                    // (texture de couleur _Fca/_Fc… et texture d'intensité _Fi… choisies)
    bool ambient = false;                       // (effet permanent : emplacement EftLoc, ou ensemble « …_Move_00 », bougie, lueur…)
    bool guessed = false;                       // (fichier absent : textures habituelles de la famille, devinée du nom)
    std::string why;                            // (fichier d'effet absent ou illisible : famille devinée du nom seul)
};

const char* fxKindName(FxKind k);
// (sans fichier d'effet lisible : la famille est devinée du nom de l'acteur, et `why` dit pourquoi)
// (model : nom du modèle de l'acteur, pour les effets rangés sous « CmnTex_Map_<modèle> » comme les bougies)
EffectInfo effectLoad(Game& g, const std::string& actor, const std::string& model = "");
// (lecture du seul fichier d'effet, pour les tests)
EffectInfo effectParse(const Bytes& vsetb, const std::string& name);

}  // namespace te
