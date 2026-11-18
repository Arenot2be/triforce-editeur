// BYML <-> texte JSON : le format des fichiers du dossier de travail (identique à triforce_modkit/textmap.py),
// et le texte des propriétés d'un objet dans l'éditeur (comme les étiquettes de la page web : !u32, !u64…).
#pragma once
#include "core/byml.hpp"
#include "core/json.hpp"

namespace te {

std::string f32Text(double x);
std::string f64Text(double x);

std::string toText(const Document& doc, int indent = 2);   // (fichier de travail)
Document fromText(const std::string& text);

// propriétés d'un objet, modifiables à la main ; tmpl : la valeur d'avant (garde les types : un 1.0 F32 reste F32)
std::string toWebText(const Value& v);
Value fromWeb(const JNode& n, const Value* tmpl, const std::string& where = "valeur");

}  // namespace te
