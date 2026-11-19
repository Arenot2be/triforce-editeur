// L'interface (Dear ImGui) : barre du haut, cartes / couches / catégories / objets, sélection, vue 3D et ses poignées.
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "app/editor.hpp"
#include "core/textmap.hpp"

namespace te {

namespace {
const float TOP = 46, BOTTOM = 26, LEFT = 310, RIGHT = 390;
const char* QUIET[] = {"Land", "EftLoc", "Tree", "Foliage", "Grass"};

double clockNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct UiState {
    std::string mapFilter, objFilter;
    std::string props;          // (texte des propriétés de l'objet sélectionné)
    const Rec* propsFor = nullptr;
    std::string propsVal;       // (valeur affichée la dernière fois : pour voir les changements faits ailleurs)
    std::string propsErr;
    bool keepYaw = true;
    bool showHelp = false, showFolders = false, showBuild = false, showTex = false, showMdl = false, askRevert = false, askSave = false, askQuit = false;
    std::string romfs, work, mods;
    bool viewHovered = false;
    float boxX0 = 0, boxY0 = 0;
    bool boxing = false, orbit = false, pan = false;
    double lastX = 0, lastY = 0;
    bool clickPending = false;
    float clickX = 0, clickY = 0;
} U;

ImU32 col(float r, float g, float b, float a = 1) { return ImGui::ColorConvertFloat4ToU32({r, g, b, a}); }

std::string texLine(Editor& ed, const std::string& name) {
    auto it = ed.texSlots.find(name);
    if (it == ed.texSlots.end()) return name + " — en attente";
    auto& t = it->second;
    switch (t.st) {
        case TexSlot::Loading: return name + " — chargement";
        case TexSlot::Ok: {
            std::string s = name + " — ok (" + t.format + fmt(", %dx%d)", t.w, t.h);
            if (t.invalid) {
                s += fmt(", %d blocs invalides (", t.invalid);
                bool first = true;
                for (auto& [k, v] : t.reasons) { s += (first ? "" : ", ") + std::to_string(v) + " " + k; first = false; }
                s += ")";
            }
            if (t.empty) s += fmt(", %d blocs vides", t.empty);
            return s;
        }
        case TexSlot::Mask: return name + " — ignorée : " + t.why;
        default: return name + " — illisible : " + t.why;
    }
}

// (pour la liste des modèles à problème : ce qui manque à un modèle chargé, une ligne par problème ; "" si rien)
std::string modelProblems(Editor& ed, const ModelSlot& s) {
    std::string out;
    for (auto& nt : s.notes) out += "\n    " + nt;
    for (auto& sb : s.gpu.subs) {
        if (sb.tex.empty()) continue;
        auto it = ed.texSlots.find(sb.tex);
        if (it != ed.texSlots.end() && it->second.st == TexSlot::None) out += "\n    texture " + sb.tex + " : " + it->second.why;
    }
    return out.empty() ? out : out + "\n";
}

void tryOpen(Editor& ed, const std::string& rel) {
    if (ed.hasMap && ed.doc.dirty) { ed.pendingOpen = rel; U.askSave = true; return; }
    ed.openMap(rel);
    U.propsFor = nullptr;
}

// ---------------------------------------------------------------- barre du haut
void topBar(Editor& ed) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({io.DisplaySize.x, TOP});
    ImGui::Begin("##haut", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored({0.95f, 0.75f, 0.35f, 1}, "Éditeur de cartes");
    ImGui::SameLine();
    if (ed.hasMap) {
        ImGui::Text("%s", ed.doc.path.c_str());
        if (ed.doc.dirty) { ImGui::SameLine(); ImGui::TextColored({0.95f, 0.75f, 0.35f, 1}, "(modifiée)"); }
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", ed.doc.origin == "travail" ? "ta version" : "version du jeu");
    } else {
        ImGui::TextDisabled("aucune carte ouverte");
    }
    float right = io.DisplaySize.x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, right - 900));
    if (ImGui::Button(ed.cam.top ? "Vue 3D" : "Vue de dessus")) ed.cam.top = !ed.cam.top;
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.doc.canUndo());
    if (ImGui::Button("Annuler")) ed.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.doc.canRedo());
    if (ImGui::Button("Rétablir")) ed.redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.hasMap || !ed.doc.dirty);
    if (ImGui::Button("Enregistrer")) ed.save();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.hasMap || ed.doc.origin != "travail");
    if (ImGui::Button("Version du jeu")) U.askRevert = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputTextWithHint("##mod", "Nom du mod", &ed.modName)) ed.saveSettings();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ed.game || ed.building);
    if (ImGui::Button(ed.building ? "Construction…" : "Construire le mod")) {
        if (ed.hasMap && ed.doc.dirty) ed.save();
        ed.buildMod();
        U.showBuild = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Dossiers")) {
        U.romfs = pathStr(ed.folders.romfs);
        U.work = pathStr(ed.folders.work);
        U.mods = pathStr(ed.folders.mods);
        U.showFolders = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("?")) U.showHelp = true;
    ImGui::End();
}

// ---------------------------------------------------------------- panneau de gauche
void leftPanel(Editor& ed) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, TOP});
    ImGui::SetNextWindowSize({LEFT, io.DisplaySize.y - TOP - BOTTOM});
    ImGui::Begin("##gauche", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    float h = ImGui::GetContentRegionAvail().y;
    if (!ed.gameError.empty()) {
        ImGui::PushTextWrapPos();
        ImGui::TextColored({1, 0.5f, 0.4f, 1}, "%s", ed.gameError.c_str());
        ImGui::TextWrapped("Indique le dossier romfs du jeu avec le bouton « Dossiers ».");
        ImGui::PopTextWrapPos();
    }
    // cartes
    ImGui::TextDisabled("CARTES");
    ImGui::SameLine(LEFT - 90);
    if (ImGui::SmallButton("Actualiser") && ed.game) ed.maps = MapDoc::listMaps(*ed.game, ed.folders.work);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##fcartes", "Filtrer (ex. Kokiri)", &U.mapFilter);
    ImGui::BeginChild("##cartes", {0, h * 0.27f}, ImGuiChildFlags_Borders);
    std::string f = lower(U.mapFilter);
    for (auto& m : ed.maps) {
        std::string shown = m.path.substr(5);   // (sans « Banc/ »)
        if (endsWith(shown, ".bcett.byml.zs")) shown.resize(shown.size() - 14);
        if (!f.empty() && lower(shown).find(f) == std::string::npos) continue;
        bool cur = ed.hasMap && ed.doc.path == m.path;
        std::string label = shown + (m.edited ? "   *" : "");
        if (ImGui::Selectable((label + "##" + m.path).c_str(), cur)) tryOpen(ed, m.path);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s", m.path.c_str(), m.edited ? "\n* modifiée (dans le dossier de travail)" : "");
    }
    ImGui::EndChild();
    if (ed.hasMap) {
        // couches
        ImGui::TextDisabled("COUCHES");
        ImGui::BeginChild("##couches", {0, std::min(h * 0.16f, ImGui::GetFrameHeightWithSpacing() * float(ed.doc.layers.size()) + 2 * ImGui::GetStyle().WindowPadding.y)}, ImGuiChildFlags_Borders);
        for (auto& L : ed.doc.layers) {
            ImGui::Checkbox((L.name + "##c").c_str(), &L.visible);
            ImGui::SameLine(LEFT - 70);
            ImGui::TextDisabled("%zu", L.recs.size());
        }
        ImGui::EndChild();
        // catégories
        ImGui::TextDisabled("CATÉGORIES");
        ImGui::SameLine(LEFT - 120);
        if (ImGui::SmallButton("Toutes")) for (auto& [k, v] : ed.catOn) v = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Aucune")) for (auto& [k, v] : ed.catOn) v = false;
        ImGui::BeginChild("##cats", {0, h * 0.2f}, ImGuiChildFlags_Borders);
        std::vector<std::pair<int, std::string>> cats;
        for (auto& [k, n] : ed.catCount) cats.push_back({-n, k});
        std::sort(cats.begin(), cats.end());
        for (auto& [n, k] : cats) {
            auto& c = ed.catColor[k];
            ImGui::ColorButton(("##col" + k).c_str(), {c[0], c[1], c[2], 1}, ImGuiColorEditFlags_NoTooltip, {12, 12});
            ImGui::SameLine();
            bool on = ed.catOn[k];
            if (ImGui::Checkbox(k.c_str(), &on)) ed.catOn[k] = on;
            ImGui::SameLine(LEFT - 70);
            ImGui::TextDisabled("%d", -n);
        }
        ImGui::EndChild();
        // objets
        auto recs = ed.allRecs();
        std::string of = lower(U.objFilter);
        std::vector<RecP> shown;
        for (auto& r : recs) {
            if (!ed.visible(*r)) continue;
            if (!of.empty() && lower(nameOf(r->value)).find(of) == std::string::npos) continue;
            shown.push_back(r);
        }
        ImGui::TextDisabled("OBJETS");
        ImGui::SameLine(LEFT - 100);
        ImGui::TextDisabled("%zu / %zu", shown.size(), recs.size());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##fobj", "Chercher un objet", &U.objFilter);
        ImGui::BeginChild("##objets", {0, 0}, ImGuiChildFlags_Borders);
        ImGuiListClipper clip;
        clip.Begin(int(shown.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                auto& r = shown[size_t(i)];
                std::string n = nameOf(r->value);
                if (n.empty()) n = "(sans nom)";
                if (ImGui::Selectable((n + "##o" + std::to_string(uintptr_t(r.get()))).c_str(), ed.sel.count(r.get()) > 0)) {
                    ed.select(r.get(), io.KeyCtrl || io.KeyShift);
                    if (ImGui::IsMouseDoubleClicked(0) || !(io.KeyCtrl || io.KeyShift)) ed.focus({r.get()});
                }
            }
        ImGui::EndChild();
    }
    ImGui::End();
}

// ---------------------------------------------------------------- panneau de droite
bool numField(const char* label, double v, double& out) {
    double x = v;
    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::InputDouble(label, &x, 0, 0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
    if (changed && x != v && std::isfinite(x)) { out = x; return true; }
    return false;
}

void rightPanel(Editor& ed) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x - RIGHT, TOP});
    ImGui::SetNextWindowSize({RIGHT, io.DisplaySize.y - TOP - BOTTOM});
    ImGui::Begin("##droite", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::TextDisabled("SÉLECTION");
    ImGui::PushTextWrapPos();
    if (ed.sel.empty()) {
        ImGui::TextDisabled("Clique sur un objet pour le sélectionner.\nCtrl+clic : en ajouter ; Maj+glisser : sélection rectangle.");
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    RecP r = ed.primary();
    if (!r) {
        ImGui::Text("%zu objets sélectionnés", ed.sel.size());
        V3 c = ed.selCenter();
        ImGui::TextDisabled("centre : %s, %s, %s", fmtNum(c.x).c_str(), fmtNum(c.y).c_str(), fmtNum(c.z).c_str());
    } else {
        std::string n = nameOf(r->value);
        ImGui::TextColored({0.95f, 0.8f, 0.45f, 1}, "%s", n.empty() ? "(sans nom)" : n.c_str());
        size_t idx = 0;
        auto& L = ed.doc.layers[size_t(r->layer)];
        for (size_t i = 0; i < L.recs.size(); i++) if (L.recs[i] == r) idx = i;
        ImGui::TextDisabled("Couche : %s — n° %zu%s", L.name.c_str(), idx, r->dup ? " (copie)" : "");
        auto ms = n.empty() ? nullptr : ed.model(n);
        if (ms) {
            if (ms->st == ModelSlot::Ok) {
                std::string s = fmt("Modèle : %zu triangles. Textures : ", ms->gpu.tris);
                bool first = true;
                for (auto& sb : ms->gpu.subs) {
                    if (sb.tex.empty()) continue;
                    s += (first ? "" : " ; ") + texLine(ed, sb.tex);
                    first = false;
                }
                if (first) s += "aucune";
                ImGui::TextDisabled("%s", s.c_str());
                for (auto& nt : ms->notes) ImGui::TextColored({0.95f, 0.75f, 0.35f, 1}, "%s", nt.c_str());
                if (ImGui::SmallButton("Exporter ce modèle pour analyse")) ed.exportModel(n);
                if (!ms->notes.empty()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copier les remarques")) {
                        std::string all = n;
                        for (auto& nt : ms->notes) all += "\n  " + nt;
                        ImGui::SetClipboardText(all.c_str());
                    }
                }
            } else if (ms->st == ModelSlot::None) {
                ImGui::TextDisabled("Pas de modèle : %s", ms->why.c_str());
                // (pour tout modèle illisible, pas seulement les meshlets : la raison affichée suffit à trier ensuite)
                if (ImGui::SmallButton("Exporter ce modèle pour analyse")) ed.exportModel(n);
                ImGui::SameLine();
                if (ImGui::SmallButton("Copier la raison")) ImGui::SetClipboardText((n + " : " + ms->why).c_str());
            } else {
                ImGui::TextDisabled("Modèle : chargement…");
            }
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    if (r) {
        // position
        const char* pk = posKey(r->value);
        if (pk) {
            V3 p = ed.posOf(*r);
            const char* names[3] = {"X", "Y", "Z"};
            for (int k = 0; k < 3; k++) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", names[k]);
                ImGui::SameLine(30);
                double v;
                if (numField((std::string("##p") + names[k]).c_str(), p[k], v)) ed.editPos(*r, k, v);
            }
        } else {
            ImGui::TextDisabled("Pas de position.");
        }
        // rotation
        ImGui::TextDisabled("Rotation (degrés, telle qu'écrite : X, puis Y, puis Z)");
        V3 e = ed.rotOf(*r);
        float w3 = (ImGui::GetContentRegionAvail().x - 3 * 18) / 3;
        const char* rn[3] = {"X", "Y", "Z"};
        const ImVec4 rc[3] = {{0.94f, 0.35f, 0.31f, 1}, {0.42f, 0.78f, 0.48f, 1}, {0.35f, 0.69f, 0.91f, 1}};
        for (int k = 0; k < 3; k++) {
            if (k) ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(rc[k], "%s", rn[k]);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w3);
            double deg = e[k] * 180 / 3.14159265358979323846, nv = deg;
            if (ImGui::InputDouble((std::string("##r") + rn[k]).c_str(), &nv, 0, 0, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue) ||
                ImGui::IsItemDeactivatedAfterEdit())
                if (nv != deg && std::isfinite(nv)) ed.editRot(*r, k, nv);
        }
        V3 f = matVec(eulerMat(e), {0, 0, 1});
        double hd = (std::hypot(f.x, f.z) > 1e-4 ? std::atan2(f.x, f.z) : 0) * 180 / 3.14159265358979323846;
        double tiltDeg = std::acos(std::clamp(eulerMat(e)[1][1], -1.0, 1.0)) * 180 / 3.14159265358979323846;
        ImGui::TextDisabled("%sCap : %.1f° · inclinaison : %.1f°", rotKey(r->value) ? "" : "(pas de rotation dans le fichier) ", hd, tiltDeg);
    }
    if (ImGui::Button("Gauche 90°")) ed.rotateSel(1, 3.14159265358979323846 / 2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tourner de 90° vers la gauche autour de la verticale (Maj+R)");
    ImGui::SameLine();
    if (ImGui::Button("Droite 90°")) ed.rotateSel(1, -3.14159265358979323846 / 2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tourner de 90° vers la droite autour de la verticale (R)");
    ImGui::SameLine();
    if (ImGui::Button("Redresser")) ed.levelSel(U.keepYaw);
    ImGui::SameLine();
    ImGui::Checkbox("garder le cap", &U.keepYaw);
    ImGui::Separator();
    if (ImGui::Button("Centrer")) { std::vector<Rec*> v(ed.sel.begin(), ed.sel.end()); ed.focus(v); }
    ImGui::SameLine();
    if (ImGui::Button("Dupliquer")) ed.duplicateSel();
    ImGui::SameLine();
    if (ImGui::Button("Supprimer")) ed.deleteSel();
    ImGui::SameLine();
    ImGui::BeginDisabled(ed.tiles.empty());
    if (ImGui::Button("Poser au sol")) ed.dropToGround();
    ImGui::EndDisabled();
    if (r) {
        ImGui::TextDisabled("Toutes les propriétés (texte modifiable) :");
        std::string cur = toWebText(r->value);
        if (U.propsFor != r.get() || (cur != U.propsVal && !ImGui::IsItemActive())) {
            if (U.propsFor != r.get() || U.props == U.propsVal) U.props = cur;
            U.propsFor = r.get();
            U.propsVal = cur;
            U.propsErr.clear();
        }
        float hh = ImGui::GetContentRegionAvail().y - 40 - (U.propsErr.empty() ? 0 : 40);
        ImGui::InputTextMultiline("##props", &U.props, {-1, std::max(120.f, hh)}, ImGuiInputTextFlags_AllowTabInput);
        ImGui::BeginDisabled(U.props == U.propsVal);
        if (ImGui::Button("Appliquer le texte")) {
            std::string err;
            if (ed.applyProps(*r, U.props, err)) { U.propsVal = U.props = toWebText(r->value); U.propsErr.clear(); }
            else U.propsErr = err;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(U.props == U.propsVal);
        if (ImGui::Button("Annuler le texte")) { U.props = U.propsVal; U.propsErr.clear(); }
        ImGui::EndDisabled();
        if (!U.propsErr.empty()) {
            ImGui::PushTextWrapPos();
            ImGui::TextColored({1, 0.5f, 0.4f, 1}, "%s", U.propsErr.c_str());
            ImGui::PopTextWrapPos();
        }
    }
    ImGui::End();
}

// ---------------------------------------------------------------- vue 3D (entrées, étiquettes, poignées)
void viewport(Editor& ed) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 p0{LEFT, TOP}, sz{io.DisplaySize.x - LEFT - RIGHT, io.DisplaySize.y - TOP - BOTTOM};
    ed.vpX = int(p0.x);
    ed.vpY = int(p0.y);
    ed.vpW = std::max(1, int(sz.x));
    ed.vpH = std::max(1, int(sz.y));
    ed.updateMatrices();
    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(sz);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("##vue", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##zone", sz, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    float mx = io.MousePos.x, my = io.MousePos.y;

    // -- étiquettes
    if (ed.hasMap && ed.showLabels) {
        V3 eye = ed.cam.eye();
        int drawn = 0;
        double maxD = ed.cam.top ? 1e9 : std::max(150.0, ed.cam.dist * 3);
        for (auto& L : ed.doc.layers) {
            if (!L.visible) continue;
            for (auto& r : L.recs) {
                if (!posKey(r->value) || !ed.visible(*r)) continue;
                bool hot = ed.sel.count(r.get()) || ed.hover == r.get();
                std::string n = nameOf(r->value);
                std::string cat = catOf(n);
                bool quiet = false;
                for (auto q : QUIET) if (cat == q) quiet = true;
                if (quiet && !hot) continue;
                V3 p = ed.posOf(*r);
                if (!hot && len(p - eye) > maxD) continue;
                float sx, sy;
                if (!ed.project(p, sx, sy) || sx < p0.x || sy < p0.y || sx > p0.x + sz.x || sy > p0.y + sz.y) continue;
                if (!hot && ++drawn > 220) continue;
                ImU32 c = hot ? col(1, 0.88f, 0.5f) : col(0.9f, 0.92f, 0.95f, 0.8f);
                dl->AddText({sx + 6 + 1, sy - 8 + 1}, col(0, 0, 0, 0.7f), n.c_str());
                dl->AddText({sx + 6, sy - 8}, c, n.c_str());
            }
        }
    }

    // -- poignées
    if (!ed.sel.empty()) {
        V3 c = ed.ds.active ? ed.ds.center : ed.selCenter();
        float cx, cy;
        if (ed.project(c, cx, cy)) {
            double L = ed.gizmoLen();
            ImU32 axc[3] = {col(0.94f, 0.35f, 0.31f), col(0.42f, 0.78f, 0.48f), col(0.35f, 0.69f, 0.91f)};
            Gizmo hot = ed.ds.active ? ed.ds.g : (hovered ? ed.hitGizmo(mx, my) : Gizmo());
            for (int a = 0; a < 3; a++) {
                if (ed.cam.top && a == 1) continue;
                V3 e = c;
                e[a] += L;
                float ex, ey;
                if (!ed.project(e, ex, ey)) continue;
                bool h = hot.kind == Gizmo::Axis && hot.axis == a;
                dl->AddLine({cx, cy}, {ex, ey}, h ? col(1, 1, 1) : axc[a], h ? 4.f : 2.5f);
                dl->AddCircleFilled({ex, ey}, h ? 6.f : 5.f, axc[a]);
            }
            bool ph = hot.kind == Gizmo::Plane;
            dl->AddRectFilled({cx - 7, cy - 7}, {cx + 7, cy + 7}, ph ? col(1, 1, 1, 0.9f) : col(0.95f, 0.85f, 0.4f, 0.75f));
            for (int i : {1, 0, 2}) {
                if (i != 1 && !ed.tilt) continue;
                double rad = L * (i == 1 ? 0.75 : 0.62);
                std::vector<ImVec2> pts;
                for (int k = 0; k <= 64; k++) {
                    double a = k / 64.0 * 6.283185307179586, u = std::sin(a) * rad, w = std::cos(a) * rad;
                    V3 off = i == 1 ? V3(u, 0, w) : i == 0 ? V3(0, u, w) : V3(u, w, 0);
                    float qx, qy;
                    if (ed.project(c + off, qx, qy)) pts.push_back({qx, qy});
                }
                bool h = hot.kind == Gizmo::Ring && hot.axis == i;
                ImU32 rcol = i == 1 ? col(0.88f, 0.72f, 0.29f, h ? 1.f : 0.8f) : i == 0 ? col(0.94f, 0.35f, 0.31f, h ? 1.f : 0.6f) : col(0.35f, 0.69f, 0.91f, h ? 1.f : 0.6f);
                if (pts.size() > 1) dl->AddPolyline(pts.data(), int(pts.size()), rcol, 0, h ? 3.f : 2.f);
            }
            if (ed.ds.active && ed.ds.g.kind == Gizmo::Ring)
                dl->AddText({cx + 14, cy - 18}, col(1, 1, 1), fmt("%+.1f°", ed.ds.shown).c_str());
        }
    }

    // -- souris
    if (hovered && !ed.ds.active && !U.boxing && !U.orbit && !U.pan) ed.hover = ed.pick(mx, my);
    if (!hovered && !ImGui::IsItemActive()) ed.hover = nullptr;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Gizmo g = ed.hitGizmo(mx, my);
        if (g.kind != Gizmo::None) ed.beginDrag(g, mx, my);
        else if (io.KeyShift) { U.boxing = true; U.boxX0 = mx; U.boxY0 = my; }
        else { U.clickPending = true; U.clickX = mx; U.clickY = my; }
    }
    if (U.clickPending && ImGui::IsMouseDown(ImGuiMouseButton_Left) && std::hypot(mx - U.clickX, my - U.clickY) > 4) {
        U.clickPending = false;
        if (ed.cam.top) U.pan = true;
        else U.orbit = true;
    }
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) U.pan = true;
    ImVec2 d = io.MouseDelta;
    if (ed.ds.active) ed.updateDrag(mx, my, io.KeyCtrl);
    if (U.orbit) {
        ed.cam.yaw -= d.x * 0.006;
        ed.cam.pitch = std::clamp(ed.cam.pitch + d.y * 0.006, -1.5, 1.55);
    }
    if (U.pan) {
        if (ed.cam.top) {
            double k = ed.cam.topSpan / ed.vpW;
            ed.cam.target = ed.cam.target + V3(-d.x * k, 0, -d.y * k);
        } else {
            V3 e = ed.cam.eye(), f = norm(ed.cam.target - e), r = norm(cross(f, {0, 1, 0})), u = cross(r, f);
            double k = ed.cam.dist * 0.0018;
            ed.cam.target = ed.cam.target - r * (d.x * k) + u * (d.y * k);
        }
    }
    if (U.boxing) {
        dl->AddRectFilled({std::min(U.boxX0, mx), std::min(U.boxY0, my)}, {std::max(U.boxX0, mx), std::max(U.boxY0, my)}, col(0.95f, 0.8f, 0.4f, 0.12f));
        dl->AddRect({std::min(U.boxX0, mx), std::min(U.boxY0, my)}, {std::max(U.boxX0, mx), std::max(U.boxY0, my)}, col(0.95f, 0.8f, 0.4f, 0.8f));
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (ed.ds.active) ed.endDrag();
        if (U.boxing) {
            float x0 = std::min(U.boxX0, mx), x1 = std::max(U.boxX0, mx), y0 = std::min(U.boxY0, my), y1 = std::max(U.boxY0, my);
            if (!io.KeyCtrl) ed.sel.clear();
            for (auto& r : ed.allRecs()) {
                if (!posKey(r->value) || !ed.visible(*r)) continue;
                float sx, sy;
                if (ed.project(ed.posOf(*r), sx, sy) && sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) ed.sel.insert(r.get());
            }
            U.boxing = false;
        }
        if (U.clickPending) {
            Rec* p = ed.pick(U.clickX, U.clickY);
            ed.select(p, io.KeyCtrl);
            U.clickPending = false;
        }
        U.orbit = false;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) U.pan = false;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) U.pan = false;
    if (hovered && io.MouseWheel != 0) {
        double k = std::pow(0.88, io.MouseWheel);
        if (ed.cam.top) ed.cam.topSpan = std::clamp(ed.cam.topSpan * k, 5.0, 60000.0);
        else ed.cam.dist = std::clamp(ed.cam.dist * k, 1.0, 40000.0);
    }
    // -- clavier (déplacement : ZQSD, A/E ; flèches)
    if (!io.WantTextInput) {
        double dt = io.DeltaTime, sp = (ed.cam.top ? ed.cam.topSpan : ed.cam.dist) * (io.KeyShift ? 2.4 : 0.8) * dt;
        V3 e = ed.cam.eye(), fw = ed.cam.target - e;
        fw.y = 0;
        fw = ed.cam.top ? V3(0, 0, -1) : norm(fw);
        V3 rt = norm(cross(fw, {0, 1, 0}));
        V3 mv;
        if (ImGui::IsKeyDown(ImGuiKey_Z) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) mv = mv + fw;
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) mv = mv - fw;
        if (ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) mv = mv - rt;
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) mv = mv + rt;
        if (!io.KeyCtrl) {
            if (ImGui::IsKeyDown(ImGuiKey_E)) mv.y += 1;
            if (ImGui::IsKeyDown(ImGuiKey_A)) mv.y -= 1;
        }
        if (!io.KeyCtrl && len(mv) > 0) ed.cam.target = ed.cam.target + norm(mv) * sp;
    }

    // -- cases à cocher de la vue
    ImGui::SetCursorScreenPos({p0.x + 10, p0.y + 8});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(18, 22, 28, 215));
    ImGui::BeginChild("##outils", {0, 0}, ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
    ImGui::Checkbox("Modèles 3D", &ed.showModels);
    ImGui::SameLine();
    {
        int ok = 0, none = 0, loading = 0;
        for (auto& [k, s] : ed.modelSlots) (s.st == ModelSlot::Ok ? ok : s.st == ModelSlot::None ? none : loading)++;
        ImGui::TextDisabled("%d chargé(s), %d sans modèle%s", ok, none, loading ? fmt(", %d en cours", loading).c_str() : "");
        int partial = 0;
        for (auto& [k, s] : ed.modelSlots)
            if (s.st == ModelSlot::Ok && !modelProblems(ed, s).empty()) partial++;
        ImGui::SameLine(0, 4);
        if ((partial || none) && ImGui::SmallButton(partial ? fmt("%d à problème", partial).c_str() : "détails")) U.showMdl = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Textures", &ed.showTextures);
    ImGui::SameLine();
    {
        int ok = 0, bad = 0, mask = 0;
        for (auto& [k, s] : ed.texSlots) (s.st == TexSlot::Ok ? ok : s.st == TexSlot::Mask ? mask : s.st == TexSlot::None ? bad : ok)++;
        ImGui::TextDisabled("%d", ok);
        ImGui::SameLine(0, 4);
        if (bad && ImGui::SmallButton(fmt("%d illisibles", bad).c_str())) U.showTex = true;
        if (mask) { ImGui::SameLine(); ImGui::TextDisabled("· %d masques ignorés", mask); }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Terrain", &ed.showTerrain);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    if (ImGui::BeginCombo("##scene", ed.terrainScene.empty() ? "(aucun)" : ed.terrainScene.c_str())) {
        if (ImGui::Selectable("(aucun)", ed.terrainScene.empty())) ed.setTerrain("");
        for (auto& s : ed.scenes) if (ImGui::Selectable(s.c_str(), s == ed.terrainScene)) ed.setTerrain(s);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ed.terrainStatus.c_str());
    ImGui::SameLine();
    ImGui::Checkbox("Incliner", &ed.tilt);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Montrer aussi les anneaux rouge (axe X) et bleu (axe Z) pour pencher les objets");
    ImGui::SameLine();
    ImGui::Checkbox("Noms", &ed.showLabels);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // -- aide en bas de la vue
    ImGui::SetCursorScreenPos({p0.x + 10, p0.y + sz.y - 26});
    ImGui::TextDisabled(ed.cam.top ? "glisser : se déplacer · molette : zoomer · Maj+glisser : sélection rectangle"
                                   : "clic gauche glissé : tourner · clic droit glissé : se déplacer · molette : zoomer · ZQSD, A/E : voler");
    ImGui::End();
}

// ---------------------------------------------------------------- fenêtres
void dialogs(Editor& ed, bool& quit) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center{io.DisplaySize.x / 2, io.DisplaySize.y / 2};
    auto modal = [&](const char* id, bool& flag) {
        if (flag) { ImGui::OpenPopup(id); flag = false; }
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    };
    modal("Aide", U.showHelp);
    if (ImGui::BeginPopupModal("Aide", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Vue 3D");
        ImGui::BulletText("clic gauche : choisir un objet ; Ctrl+clic : en ajouter ; Maj+glisser : sélection rectangle");
        ImGui::BulletText("clic gauche glissé dans le vide : tourner ; clic droit glissé : se déplacer ; molette : zoomer");
        ImGui::BulletText("ZQSD ou flèches : avancer, reculer, glisser ; A / E : descendre, monter ; Maj : plus vite");
        ImGui::Text("Poignées");
        ImGui::BulletText("flèches rouge, verte, bleue : déplacer le long de X, Y, Z ; carré : déplacer à l'horizontale");
        ImGui::BulletText("anneau jaune : tourner autour de la verticale ; « Incliner » : anneaux rouge (X) et bleu (Z)");
        ImGui::BulletText("Ctrl pendant le geste : par pas de 1 m ou de 15°");
        ImGui::Text("Raccourcis");
        ImGui::BulletText("Ctrl+Z / Ctrl+Y : annuler / rétablir ; Ctrl+S : enregistrer ; Ctrl+D : dupliquer ; Suppr : supprimer");
        ImGui::BulletText("F : centrer la vue sur la sélection ; G : poser sur le terrain ; R / Maj+R : tourner de 90°");
        ImGui::BulletText("Échap : tout désélectionner");
        ImGui::Separator();
        ImGui::TextDisabled("Les cartes modifiées sont enregistrées dans le dossier de travail (même format que le modkit).");
        ImGui::TextDisabled("« Construire le mod » écrit le mod dans le dossier des mods de Triforce.");
        if (ImGui::Button("Fermer")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    modal("Dossiers", U.showFolders);
    if (ImGui::BeginPopupModal("Dossiers", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("romfs du jeu (fichiers d'origine, jamais modifiés)");
        ImGui::SetNextItemWidth(620);
        ImGui::InputText("##romfs", &U.romfs);
        ImGui::Text("Dossier de travail (tes cartes modifiées)");
        ImGui::SetNextItemWidth(620);
        ImGui::InputText("##travail", &U.work);
        ImGui::Text("Dossier des mods de Triforce");
        ImGui::SetNextItemWidth(620);
        ImGui::InputText("##mods", &U.mods);
        if (ImGui::Button("Mods : à côté du jeu")) U.mods = pathStr(Folders::defaultMods(u8path(U.romfs)));
        if (ImGui::Button("Appliquer")) {
            ed.folders.romfs = u8path(U.romfs);
            ed.folders.work = u8path(U.work);
            ed.folders.mods = u8path(U.mods);
            ed.saveSettings();
            ed.openGame();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Fermer")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    modal("Construire le mod", U.showBuild);
    if (ImGui::BeginPopupModal("Construire le mod", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto lines = ed.logCopy();
        ImGui::BeginChild("##journal", {760, 360}, ImGuiChildFlags_Borders);
        for (auto& l : lines) ImGui::TextUnformatted(l.c_str());
        if (ed.building) ImGui::TextDisabled("…");
        ImGui::EndChild();
        ImGui::BeginDisabled(ed.building);
        if (ImGui::Button("Fermer")) ImGui::CloseCurrentPopup();
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    modal("Textures à problème", U.showTex);
    if (ImGui::BeginPopupModal("Textures à problème", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // (texte sélectionnable : souris ou Ctrl+A puis Ctrl+C ; « Tout copier » pour la liste entière)
        std::string all;
        int count = 0;
        for (auto& [n, t] : ed.texSlots) {
            if (t.st != TexSlot::None) continue;
            count++;
            all += texLine(ed, n) + "\n";
            std::string users;
            int k = 0;
            for (auto& [a, m] : ed.modelSlots)
                if (m.st == ModelSlot::Ok)
                    for (auto& sb : m.gpu.subs)
                        if (sb.tex == n && k++ < 4) { users += (users.empty() ? "" : ", ") + a; break; }
            if (!users.empty()) all += "    utilisée par " + users + (k > 4 ? "…" : "") + "\n";
        }
        if (!count) all = "Aucune texture à problème pour l'instant.\n";
        ImGui::InputTextMultiline("##tex", &all, {820, 420}, ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Tout copier")) {
            ImGui::SetClipboardText(all.c_str());
            ed.say(fmt("%d textures copiées dans le presse-papiers", count));
        }
        ImGui::SameLine();
        if (ImGui::Button("Fermer")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    modal("Modèles à problème", U.showMdl);
    if (ImGui::BeginPopupModal("Modèles à problème", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string a, b, c, d;
        int na = 0, nb = 0, nc = 0, nd = 0;
        for (auto& [n, s] : ed.modelSlots) {
            if (s.st == ModelSlot::Ok) {
                std::string p = modelProblems(ed, s);
                if (!p.empty()) { a += n + p; na++; }
                bool plain = false;
                for (auto& sb : s.gpu.subs) if (sb.tex.empty()) plain = true;
                if (plain && s.notes.empty()) { c += (nc ? ", " : "") + n; nc++; }
            } else if (s.st == ModelSlot::None) {
                // (acteurs sans modèle par nature : zones, repères, effets…)
                if (s.why.rfind("pas de ModelInfo", 0) == 0) { d += (nd ? ", " : "") + n; nd++; }
                else { b += n + " : " + s.why + "\n"; nb++; }
            }
        }
        std::string all = fmt("Modèles chargés en partie sans texture : %d\n", na) + a + fmt("\nModèles illisibles : %d\n", nb) + b +
                          fmt("\nNormal — parties sans texture de couleur dans leur matériau (eau, verre, fondus…) : %d\n", nc) + c +
                          fmt("\n\nNormal — acteurs sans modèle (zones, repères, effets…) : %d\n", nd) + d + "\n";
        ImGui::InputTextMultiline("##mdl", &all, {900, 440}, ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Tout copier")) {
            ImGui::SetClipboardText(all.c_str());
            ed.say(fmt("%d modèles copiés dans le presse-papiers", na + nb));
        }
        ImGui::SameLine();
        if (ImGui::Button("Fermer")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    modal("Revenir à la version du jeu ?", U.askRevert);
    if (ImGui::BeginPopupModal("Revenir à la version du jeu ?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Ta version modifiée de cette carte sera supprimée du dossier de travail.");
        if (ImGui::Button("Oui, revenir à la version du jeu")) { ed.revertMap(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Non")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    modal("Modifications non enregistrées", U.askSave);
    if (ImGui::BeginPopupModal("Modifications non enregistrées", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("La carte %s a des modifications non enregistrées.", ed.doc.path.c_str());
        bool go = false;
        if (ImGui::Button("Enregistrer")) { if (ed.save()) go = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Ne pas enregistrer")) { go = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) { ed.pendingOpen.clear(); U.askQuit = false; ImGui::CloseCurrentPopup(); }
        if (go) {
            if (!ed.pendingOpen.empty()) { std::string p = ed.pendingOpen; ed.pendingOpen.clear(); ed.doc.dirty = false; ed.openMap(p); }
            else if (U.askQuit) quit = true;
        }
        ImGui::EndPopup();
    }
}

void statusBar(Editor& ed) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, io.DisplaySize.y - BOTTOM});
    ImGui::SetNextWindowSize({io.DisplaySize.x, BOTTOM});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 4});
    ImGui::Begin("##bas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    ImGui::TextDisabled("X %.1f Z %.1f   distance : %.0f m   %s   %zu travaux en cours   %s", ed.cam.target.x, ed.cam.target.z,
                        ed.cam.top ? ed.cam.topSpan : ed.cam.dist, ed.hasMap ? (ed.doc.dirty ? "modifiée, pas encore enregistrée" : "pas de modification") : "",
                        ed.jobs.pending(), ed.vk.gpuName.c_str());
    ImGui::End();
    if (!ed.toast.empty() && clockNow() < ed.toastUntil) {
        ImGui::SetNextWindowPos({LEFT + 20, io.DisplaySize.y - BOTTOM - 40}, ImGuiCond_Always, {0, 1});
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::Begin("##toast", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
        ImGui::PushTextWrapPos(700);
        ImGui::TextColored(ed.toastWarn ? ImVec4{1, 0.7f, 0.4f, 1} : ImVec4{0.85f, 0.95f, 0.85f, 1}, "%s", ed.toast.c_str());
        ImGui::PopTextWrapPos();
        ImGui::End();
    }
}

void shortcuts(Editor& ed) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    bool ctrl = io.KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) ed.redo(); else ed.undo(); }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) ed.redo();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) ed.save();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) ed.duplicateSel();
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) ed.deleteSel();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ed.sel.clear();
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) { std::vector<Rec*> v(ed.sel.begin(), ed.sel.end()); if (v.empty()) ed.fitAll(); else ed.focus(v); }
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_G, false)) ed.dropToGround();
    if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_R, false) && !ed.sel.empty()) ed.rotateSel(1, io.KeyShift ? 3.14159265358979323846 / 2 : -3.14159265358979323846 / 2);
}
}  // namespace

void uiShowModels() { U.showMdl = true; }   // (commande de script « modeles »)
void uiShowTextures() { U.showTex = true; }   // (commande de script « textures »)

void drawUI(Editor& ed, bool& quit, bool closeRequested) {
    if (closeRequested) {
        if (ed.hasMap && ed.doc.dirty) { U.askQuit = true; U.askSave = true; }
        else quit = true;
    }
    topBar(ed);
    leftPanel(ed);
    rightPanel(ed);
    viewport(ed);
    statusBar(ed);
    dialogs(ed, quit);
    shortcuts(ed);
}

}  // namespace te
