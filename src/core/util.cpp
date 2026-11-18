#include "core/util.hpp"

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <random>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

namespace te {

fs::path u8path(std::string_view s) {
    std::u8string u(reinterpret_cast<const char8_t*>(s.data()), s.size());
    return fs::path(u);
}

std::string pathStr(const fs::path& p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

Bytes readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("impossible d'ouvrir " + pathStr(p));
    f.seekg(0, std::ios::end);
    auto n = static_cast<size_t>(f.tellg());
    f.seekg(0);
    Bytes b(n);
    if (n && !f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(n)))
        throw Error("lecture impossible : " + pathStr(p));
    return b;
}

std::string readText(const fs::path& p) {
    Bytes b = readFile(p);
    std::string s(b.begin(), b.end());
    if (s.size() >= 3 && (uint8_t)s[0] == 0xEF && (uint8_t)s[1] == 0xBB && (uint8_t)s[2] == 0xBF) s.erase(0, 3);
    return s;
}

void writeFileAtomic(const fs::path& p, const void* data, size_t n) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += u8path(".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw Error("impossible d'écrire " + pathStr(tmp));
        f.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
        if (!f) throw Error("écriture impossible : " + pathStr(tmp));
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(p, ec);
        fs::rename(tmp, p, ec);
        if (ec) throw Error("impossible de remplacer " + pathStr(p) + " : " + ec.message());
    }
}

fs::path homeDir() {
#ifdef _WIN32
    PWSTR w = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &w))) {
        fs::path p(w);
        CoTaskMemFree(w);
        return p;
    }
    if (const wchar_t* u = _wgetenv(L"USERPROFILE")) return fs::path(u);
    return fs::path(L"C:\\");
#else
    if (const char* h = getenv("HOME")) return u8path(h);
    return u8path("/");
#endif
}

fs::path exeDir() {
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    return fs::path(std::wstring(buf, n)).parent_path();
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return fs::current_path();
    return u8path(std::string(buf, size_t(n))).parent_path();
#endif
}

std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
bool endsWith(std::string_view s, std::string_view x) { return s.size() >= x.size() && s.substr(s.size() - x.size()) == x; }
bool startsWith(std::string_view s, std::string_view x) { return s.size() >= x.size() && s.substr(0, x.size()) == x; }
std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
    return s;
}
std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t a = 0;
    for (size_t i = 0; i <= s.size(); i++)
        if (i == s.size() || s[i] == sep) { out.emplace_back(s.substr(a, i - a)); a = i + 1; }
    return out;
}
std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; i++) { s[2 * i] = d[p[i] >> 4]; s[2 * i + 1] = d[p[i] & 15]; }
    return s;
}
std::string fmt(const char* f, ...) {
    va_list a, b;
    va_start(a, f);
    va_copy(b, a);
    int n = vsnprintf(nullptr, 0, f, a);
    va_end(a);
    std::string s(size_t(n > 0 ? n : 0), '\0');
    if (n > 0) vsnprintf(s.data(), size_t(n) + 1, f, b);
    va_end(b);
    return s;
}

uint32_t crc32(const void* data, size_t n) {
    static uint32_t table[256];
    static bool ready = [] {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return true;
    }();
    (void)ready;
    uint32_t c = 0xFFFFFFFFu;
    auto p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

}  // namespace te
