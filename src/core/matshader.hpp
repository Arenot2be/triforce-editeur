// Réglages des matériaux pour le nuanceur « model » du jeu (Shader/Model.Product…bfsha), d'après Shader/ExternalBinaryString.
//
// Dans les bfres d'OoT (comme TotK), les noms des options et des paramètres du nuanceur ne sont pas écrits : seulement
// leur empreinte de 64 bits. Shader/ExternalBinaryString.bfres donne les noms : une table d'empreintes triées (+0xB8)
// et, dans le même ordre, un dictionnaire de noms (+0xC0).
//
// Le nuanceur « model » est un graphe : 8 textures (cTexture0..7, réglage ns_textureN_sampler = numéro J de sampler
// parmi _a0 _n0 _r0 _m0 _ao0 _s0 _a1 _n1, ns_textureN_texcoord = jeu de coordonnées), puis pour chaque grandeur de
// la surface un choix (ns_albedo_select, ns_normal_map_select, ns_roughness_select + _channel, ns_ao_select…) :
// 0..7 = texture N, 100.. = paramètres, 200.. = résultat d'une opération ou d'un graphe, 300 = constante.
#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "core/util.hpp"

namespace te {

using ShaderNames = std::unordered_map<uint64_t, std::string>;
std::shared_ptr<const ShaderNames> ebsParse(const Bytes& ebs);   // (vide si ce n'en est pas un)
void setShaderNames(std::shared_ptr<const ShaderNames> n);
std::shared_ptr<const ShaderNames> shaderNames();
std::string shaderName(uint64_t h);   // ("#<hex>" si inconnue)

// Matériau lu pour le nuanceur « model »
struct MatShader {
    std::string archive, model;                   // ("Model", "model")
    std::map<std::string, std::string> opts;      // (options écrites dans le matériau)
    std::array<std::string, 8> slot;              // (texture du bfres pour cTexture0..7 ; "" = aucune)
    std::array<int, 8> slotUv{};                  // (attribut de coordonnées _uN de chaque texture)
    std::array<std::array<float, 4>, 8> slotSrt{};  // (échelle u, v et décalage u, v de chaque texture : ns_tex_mtxK)
    // (paramètres du matériau, d'après ShaderAssign+0x20 : 24 octets par paramètre, empreinte du nom en +8,
    //  décalage u16 en +16 dans les données du matériau, FMAT+0x58 ; ns_tex_mtx : mode, échelle u, v, rotation, décalage u, v)
    std::map<std::string, std::array<float, 6>> params;
    std::array<float, 6> param(const std::string& k, std::array<float, 6> d = {1, 1, 1, 1, 0, 0}) const {
        auto it = params.find(k);
        return it == params.end() ? d : it->second;
    }
    std::string opt(const std::string& k, const std::string& d = "") const {
        auto it = opts.find(k);
        return it == opts.end() ? d : it->second;
    }
    int optInt(const std::string& k, int d) const;
    // (texture et canal d'une grandeur : select 0..7 ; -1 sinon)
    int select(const std::string& what, int d = -1) const;
};

}  // namespace te
