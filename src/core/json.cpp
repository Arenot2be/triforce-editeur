#include "core/json.hpp"

#include <cmath>
#include <cstdlib>

namespace te {

bool JNode::isInt() const { return k == Number && s.find_first_of(".eE") == std::string::npos; }
double JNode::num() const { return k == Number ? std::strtod(s.c_str(), nullptr) : NAN; }
const JNode* JNode::get(const std::string& key) const {
    for (auto& [k2, v] : obj) if (k2 == key) return &v;
    return nullptr;
}

namespace {
struct P {
    const std::string& t;
    size_t i = 0;
    int depth = 0;
    [[noreturn]] void fail(const char* what) {
        size_t line = 1, col = 1;
        for (size_t k = 0; k < i && k < t.size(); k++) { if (t[k] == '\n') { line++; col = 1; } else col++; }
        throw Error(fmt("texte JSON invalide, ligne %zu colonne %zu : %s", line, col, what));
    }
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) i++; }
    bool lit(const char* w) {
        size_t n = std::strlen(w);
        if (t.compare(i, n, w) == 0) { i += n; return true; }
        return false;
    }
    static void utf8(std::string& o, uint32_t c) {
        if (c < 0x80) o += char(c);
        else if (c < 0x800) { o += char(0xC0 | c >> 6); o += char(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += char(0xE0 | c >> 12); o += char(0x80 | (c >> 6 & 63)); o += char(0x80 | (c & 63)); }
        else { o += char(0xF0 | c >> 18); o += char(0x80 | (c >> 12 & 63)); o += char(0x80 | (c >> 6 & 63)); o += char(0x80 | (c & 63)); }
    }
    uint32_t hex4() {
        if (i + 4 > t.size()) fail("\\u incomplet");
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) {
            char c = t[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else fail("\\u mal écrit");
        }
        return v;
    }
    std::string str() {
        if (t[i] != '"') fail("guillemet attendu");
        i++;
        std::string o;
        while (true) {
            if (i >= t.size()) fail("texte sans guillemet de fin");
            char c = t[i++];
            if (c == '"') break;
            if (c == '\\') {
                if (i >= t.size()) fail("échappement incomplet");
                char e = t[i++];
                switch (e) {
                    case '"': o += '"'; break;
                    case '\\': o += '\\'; break;
                    case '/': o += '/'; break;
                    case 'b': o += '\b'; break;
                    case 'f': o += '\f'; break;
                    case 'n': o += '\n'; break;
                    case 'r': o += '\r'; break;
                    case 't': o += '\t'; break;
                    case 'u': {
                        uint32_t cp = hex4();
                        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                            size_t save = i;
                            i += 2;
                            uint32_t lo = hex4();
                            if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else i = save;
                        }
                        utf8(o, cp);
                        break;
                    }
                    default: fail("échappement inconnu");
                }
            } else {
                if (uint8_t(c) < 0x20) fail("caractère de contrôle dans un texte");
                o += c;
            }
        }
        return o;
    }
    JNode value() {
        if (++depth > 1000) fail("imbrication trop profonde");
        ws();
        if (i >= t.size()) fail("valeur attendue");
        JNode n;
        char c = t[i];
        if (c == '{') {
            n.k = JNode::Object;
            i++;
            ws();
            if (i < t.size() && t[i] == '}') { i++; depth--; return n; }
            while (true) {
                ws();
                if (i >= t.size() || t[i] != '"') fail("nom de propriété attendu (entre guillemets)");
                std::string key = str();
                ws();
                if (i >= t.size() || t[i] != ':') fail("« : » attendu");
                i++;
                JNode v = value();
                bool dup = false;
                for (auto& kv : n.obj) if (kv.first == key) { kv.second = std::move(v); dup = true; break; }   // (la dernière gagne, comme Python)
                if (!dup) n.obj.emplace_back(std::move(key), std::move(v));
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == '}') { i++; break; }
                fail("« , » ou « } » attendu");
            }
        } else if (c == '[') {
            n.k = JNode::Array;
            i++;
            ws();
            if (i < t.size() && t[i] == ']') { i++; depth--; return n; }
            while (true) {
                n.arr.push_back(value());
                ws();
                if (i < t.size() && t[i] == ',') { i++; continue; }
                if (i < t.size() && t[i] == ']') { i++; break; }
                fail("« , » ou « ] » attendu");
            }
        } else if (c == '"') {
            n.k = JNode::String;
            n.s = str();
        } else if (lit("true")) { n.k = JNode::Bool; n.b = true; }
        else if (lit("false")) { n.k = JNode::Bool; n.b = false; }
        else if (lit("null")) { n.k = JNode::Null; }
        else if (lit("NaN")) { n.k = JNode::Number; n.s = "nan"; }
        else if (lit("Infinity")) { n.k = JNode::Number; n.s = "inf"; }
        else if (lit("-Infinity")) { n.k = JNode::Number; n.s = "-inf"; }
        else if (c == '-' || (c >= '0' && c <= '9')) {
            size_t a = i;
            if (t[i] == '-') i++;
            if (i >= t.size() || !(t[i] >= '0' && t[i] <= '9')) fail("nombre mal écrit");
            while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++;
            if (i < t.size() && t[i] == '.') {
                i++;
                if (i >= t.size() || !(t[i] >= '0' && t[i] <= '9')) fail("nombre mal écrit");
                while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++;
            }
            if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
                i++;
                if (i < t.size() && (t[i] == '+' || t[i] == '-')) i++;
                if (i >= t.size() || !(t[i] >= '0' && t[i] <= '9')) fail("nombre mal écrit");
                while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++;
            }
            n.k = JNode::Number;
            n.s = t.substr(a, i - a);
        } else {
            fail("valeur inattendue");
        }
        depth--;
        return n;
    }
};
}  // namespace

JNode jsonParse(const std::string& text) {
    P p{text};
    JNode n = p.value();
    p.ws();
    if (p.i != text.size()) p.fail("texte en trop après la fin");
    return n;
}

std::string jsonQuote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            default:
                if (c < 0x20) o += fmt("\\u%04x", c);
                else o += char(c);
        }
    }
    return o + "\"";
}

}  // namespace te
