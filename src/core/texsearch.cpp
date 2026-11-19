#include "core/texsearch.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include <thread>

#include "core/bntx.hpp"
#include "core/game.hpp"
#include "core/meshcodec.hpp"

namespace te {

namespace {
std::vector<std::string> toks(const std::string& s) {
    std::vector<std::string> t;
    for (auto& p : split(lower(s), '_')) if (!p.empty()) t.push_back(p);
    return t;
}
std::string join(const std::vector<std::string>& t, size_t from, size_t to) {
    std::string o;
    for (size_t i = from; i < to; i++) o += (o.empty() ? "" : "_") + t[i];
    return o;
}
// (famille d'un nom : les trois derniers mots avant le rôle, ex. kokiri_interior_a pour Cloth_Kokiri_Interior_A_Alb)
std::string family(const std::string& name) {
    auto t = toks(name);
    if (t.size() >= 2) t.pop_back();
    size_t from = t.size() > 3 ? t.size() - 3 : 0;
    return join(t, from, t.size());
}
}  // namespace

void searchTextures(Game& g, TexSearch& s, int threads) {
    size_t n = s.names.size();
    std::vector<std::string> fam(n), full(n);
    for (size_t i = 0; i < n; i++) { fam[i] = family(s.names[i]); full[i] = lower(s.names[i]); }
    {
        std::lock_guard<std::mutex> lk(s.m);
        s.seen.assign(n, {});
        s.usable.assign(n, {});
        s.files.assign(n, 0);
    }
    g.findAnywhere("", 1);   // (liste de tous les fichiers)
    std::vector<std::string> all = g.allFiles();
    std::vector<std::string> list;
    for (auto& f : all) {
        std::string l = lower(f);
        if (endsWith(l, ".bwav") || endsWith(l, ".bwav.zs") || endsWith(l, ".bars.zs") || endsWith(l, ".bars")) continue;  // (sons)
        list.push_back(f);
    }
    s.total = list.size();
    std::atomic<size_t> next{0};

    auto work = [&] {
        Bytes low;
        std::vector<char> hitFile(n);
        // (un tampon : on y cherche les familles, et les textures BNTX qui portent les noms cherchés)
        std::function<void(const Bytes&, const std::string&, int)> scan = [&](const Bytes& d, const std::string& where, int depth) {
            if (s.cancel) return;
            if (depth < 4 && has(d, 0, "SARC")) {
                try {
                    Sarc a = Sarc::load(d);
                    for (auto& [mn, md] : a.files) {
                        if (Zs::isZstd(md)) {
                            try { scan(g.zs.unpack(md), where + "|" + mn, depth + 1); } catch (...) {}
                        } else scan(md, where + "|" + mn, depth + 1);
                    }
                } catch (...) {}
            }
            low.resize(d.size());
            for (size_t i = 0; i < d.size(); i++) { uint8_t c = d[i]; low[i] = (c >= 'A' && c <= 'Z') ? uint8_t(c + 32) : c; }
            bool any = false;
            for (size_t k = 0; k < n; k++) {
                const std::string& p = fam[k];
                if (p.empty()) continue;
                std::boyer_moore_horspool_searcher sr(p.begin(), p.end());
                auto it = low.begin();
                int shown = 0;
                while (true) {
                    it = std::search(it, low.end(), sr);
                    if (it == low.end()) break;
                    any = true;
                    hitFile[k] = 1;
                    size_t o = size_t(it - low.begin());
                    auto pr = [&](size_t i) { return d[i] >= 0x20 && d[i] < 0x7F; };
                    size_t a = o, b = o + p.size();
                    while (a > 0 && o - a < 80 && pr(a - 1)) a--;
                    while (b < d.size() && b - o < 80 && pr(b)) b++;
                    std::string txt(d.begin() + long(a), d.begin() + long(b));
                    if (shown++ < 40) {
                        std::lock_guard<std::mutex> lk(s.m);
                        auto& v = s.seen[k];
                        bool dup = false;
                        for (auto& e : v) if (e.text == txt) { dup = true; break; }
                        if (!dup && v.size() < 60) v.push_back({txt, where + fmt(" @0x%zX", o)});
                    }
                    it += long(p.size());
                }
            }
            if (!any) return;
            // (textures BNTX de ce tampon qui portent un des noms cherchés)
            static const char M[4] = {'B', 'N', 'T', 'X'};
            for (auto it = std::search(d.begin(), d.end(), M, M + 4); it != d.end(); it = std::search(it + 1, d.end(), M, M + 4)) {
                size_t o = size_t(it - d.begin());
                try {
                    Bytes sl(d.begin() + long(o), d.end());
                    for (auto& nm : bntxNames(sl)) {
                        std::string l = lower(nm);
                        for (size_t k = 0; k < n; k++)
                            if (l == full[k]) {
                                std::lock_guard<std::mutex> lk(s.m);
                                std::string loc = where + fmt("|@%zu", o);
                                if (std::find(s.usable[k].begin(), s.usable[k].end(), loc) == s.usable[k].end()) s.usable[k].push_back(loc);
                            }
                    }
                } catch (...) {}
            }
        };
        while (!s.cancel) {
            size_t i = next++;
            if (i >= list.size()) break;
            const std::string& rel = list[i];
            std::fill(hitFile.begin(), hitFile.end(), 0);
            try {
                Bytes d;
                try { d = g.read(rel); } catch (...) { d = g.raw(rel); s.errors++; }
                s.bytes += d.size();
                scan(d, rel, 0);
            } catch (...) { s.errors++; }
            {
                std::lock_guard<std::mutex> lk(s.m);
                for (size_t k = 0; k < n; k++) if (hitFile[k]) s.files[k]++;
            }
            s.done++;
        }
    };
    std::vector<std::thread> th;
    for (int t = 0; t < std::max(1, threads); t++) th.emplace_back(work);
    for (auto& t : th) t.join();
    s.finished = true;
}

std::string TexSearch::report() const {
    std::string o = fmt("Recherche des textures introuvables dans tout le jeu : %zu fichiers lus (%.1f Go décompressés)%s%s\n",
                        done.load(), double(bytes.load()) / 1e9, errors ? fmt(", %zu fichiers illisibles", errors.load()).c_str() : "",
                        cancel ? " — recherche arrêtée avant la fin" : "");
    for (size_t k = 0; k < names.size(); k++) {
        o += "\n== " + names[k] + "  (famille cherchée : « " + family(names[k]) + " »)\n";
        if (k < usable.size() && !usable[k].empty()) {
            o += "   TROUVÉE — texture lisible ici :\n";
            for (auto& u : usable[k]) o += "     " + u + "\n";
        } else
            o += "   aucune texture BNTX ne porte ce nom.\n";
        size_t nf = k < files.size() ? files[k] : 0;
        if (k < seen.size() && !seen[k].empty()) {
            o += fmt("   la famille apparaît dans %zu fichier(s) ; textes trouvés :\n", nf);
            std::vector<Seen> v = seen[k];
            std::sort(v.begin(), v.end(), [](const Seen& a, const Seen& b) { return a.text < b.text; });
            for (auto& e : v) o += "     « " + e.text + " »  dans " + e.where + "\n";
        } else
            o += "   la famille de noms n'apparaît dans aucun fichier.\n";
    }
    return o;
}

}  // namespace te
