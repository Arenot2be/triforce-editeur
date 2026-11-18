// Un petit lecteur JSON qui garde le texte des nombres (entier ou à virgule) et l'ordre des clés.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/util.hpp"

namespace te {

struct JNode {
    enum Kind { Null, Bool, Number, String, Array, Object } k = Null;
    bool b = false;
    std::string s;   // String : le texte ; Number : son écriture
    std::vector<JNode> arr;
    std::vector<std::pair<std::string, JNode>> obj;
    bool isInt() const;   // (nombre écrit sans . ni exposant)
    double num() const;
    const JNode* get(const std::string& key) const;
};

JNode jsonParse(const std::string& text);    // (Error avec ligne et colonne)
std::string jsonQuote(const std::string& s); // (comme json.dumps(..., ensure_ascii=False) de Python)

}  // namespace te
