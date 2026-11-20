// L'état de l'éditeur : jeu, carte ouverte, sélection, caméra, chargements en arrière-plan, poignées.
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "app/math.hpp"
#include "app/render.hpp"
#include "core/bake.hpp"
#include "core/texsearch.hpp"
#include "core/bntx.hpp"
#include "core/mapdoc.hpp"
#include "core/models.hpp"
#include "core/terrain.hpp"

namespace te {

// -- travaux en arrière-plan : le résultat est appliqué sur le fil principal (envois à la carte graphique)
class Jobs {
public:
    explicit Jobs(int n);
    ~Jobs();
    void add(int prio, std::function<std::function<void()>()> job);   // (job renvoie ce qu'il faut faire ensuite sur le fil principal)
    void pump(double budgetMs);   // (fil principal)
    void clear();
    size_t pending() const;

private:
    struct J { int prio; uint64_t seq; std::function<std::function<void()>()> f; };
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<J> q_;
    std::deque<std::function<void()>> done_;
    std::vector<std::thread> th_;
    std::atomic<bool> stop_{false};
    std::atomic<int> running_{0};
    uint64_t seq_ = 0;
};

struct ModelSlot {
    enum St { Loading, Ok, None } st = Loading;
    GpuMesh gpu;
    std::string why;
    std::vector<std::string> notes;   // (formes ignorées ou lues incomplètement)
};
struct TexSlot {
    enum St { Loading, Ok, None, Mask } st = Loading;
    VkTex tex;
    std::string why, format;
    std::string standIn;   // (texture absente du jeu remplacée par une voisine de la même famille : son nom)
    bool optional = true;  // (seulement d'appoint : normales d'eau, détail, mousse, éclairage ; son absence n'est pas un problème)
    int w = 0, h = 0, invalid = 0, empty = 0;
    std::map<std::string, int> reasons;
};

struct Camera {
    V3 target{0, 0, 0};
    double dist = 80, yaw = 0.6, pitch = 0.55;
    bool top = false;   // (vue de dessus, orthographique)
    double topSpan = 200;
    V3 eye() const;
};

struct Gizmo {
    enum Kind { None, Axis, Plane, Ring } kind = None;
    int axis = 0;
};

class Editor {
public:
    Editor(Vk& vk, Renderer& r);
    ~Editor();
    Vk& vk;
    Renderer& rend;

    // -- dossiers et jeu
    Folders folders;
    std::string modName = "MonMod";
    std::unique_ptr<Game> game;
    std::unique_ptr<ModelIndex> models;
    std::string gameError;
    std::vector<MapEntry> maps;
    void loadSettings();
    void saveSettings();
    void openGame();

    // -- carte
    MapDoc doc;
    bool hasMap = false;
    std::string mapError;
    void openMap(const std::string& rel);
    void reloadMap();
    bool save();
    void revertMap();
    std::string pendingOpen;   // (carte à ouvrir après la question « enregistrer ? »)

    // -- objets
    std::map<std::string, bool> catOn;
    std::map<std::string, int> catCount;
    std::map<std::string, std::array<float, 3>> catColor;
    void rebuildCats();
    bool visible(const Rec& r) const;
    std::vector<RecP> allRecs() const;
    RecP findRec(const Rec* p) const;
    V3 posOf(const Rec& r) const;
    V3 rotOf(const Rec& r) const;
    V3 scaleOf(const Rec& r) const;
    void setPos(Rec& r, const V3& p);
    void setRot(Rec& r, const V3& e);

    // -- sélection et modifications (annulables)
    std::set<Rec*> sel;
    Rec* hover = nullptr;
    RecP primary() const;
    V3 selCenter() const;
    void select(Rec* r, bool add);
    void moveSel(const V3& d);
    void rotateSel(int axis, double a);   // (autour d'un axe du monde, par le centre de la sélection)
    void levelSel(bool keepYaw);
    void duplicateSel();
    void deleteSel();
    void dropToGround();
    bool applyProps(Rec& r, const std::string& text, std::string& err);
    void editRot(Rec& r, int axis, double degrees);
    void editPos(Rec& r, int axis, double v);
    void undo();
    void redo();

    // -- vue 3D
    Camera cam;
    M4 view, proj, vp, invVp;
    int vpX = 0, vpY = 0, vpW = 1, vpH = 1;
    bool showModels = true, showTextures = true, showTerrain = true, tilt = false, showLabels = true;
    void updateMatrices();
    bool project(const V3& p, float& sx, float& sy) const;
    void ray(float sx, float sy, V3& o, V3& d) const;
    Rec* pick(float sx, float sy) const;
    void focus(const std::vector<Rec*>& recs);
    void fitAll();
    void render(VkCommandBuffer c);

    // -- poignées (dessinées par l'interface)
    Gizmo drag;
    struct DragState {
        bool active = false, moved = false;
        Gizmo g;
        V3 center, h0;
        float sx0 = 0, sy0 = 0, last = 0, acc = 0, p0x = 0, p0y = 0;
        double sign = 1, shown = 0;
        std::vector<std::tuple<Rec*, V3, V3>> start;
        MapDoc::Tx tx;
    } ds;
    double gizmoLen() const;
    Gizmo hitGizmo(float sx, float sy) const;
    void beginDrag(const Gizmo& g, float sx, float sy);
    void updateDrag(float sx, float sy, bool snap);
    void endDrag();

    // -- modèles, textures, terrain
    std::map<std::string, ModelSlot> modelSlots;
    std::map<std::string, TexSlot> texSlots;
    Jobs jobs{int(std::max(2u, std::min(8u, std::thread::hardware_concurrency())))};
    ModelSlot* model(const std::string& actor);
    void exportModel(const std::string& actor);
    TexSlot* texture(const std::string& name, bool optional = false);
    std::vector<std::string> scenes;
    std::string terrainScene;   // ("" : aucun)
    // -- éclairage précalculé de la carte (Bake/Scene/<scène>_G_*.bkres.zs), par Hash d'objet
    std::map<uint64_t, BakeEntry> bake;
    std::string bakeStatus;
    bool showBake = true;
    std::vector<TerrainTile> tiles;
    std::string terrainStatus;
    std::string terrainMatStatus, terrainCmbStatus;   // (textures du sol : Tex/MaterialAlb, relief : Tex/MaterialCmb)
    bool terrainRelief = true;
    bool terrainMatAsked = false;
    size_t terrainMatTable = 0;   // (matières décrites par le .tscb de la scène)
    void setTerrain(const std::string& scene);
    void loadTerrainMaterials();
    std::string autoTerrain() const;
    bool groundAt(double x, double z, double& h) const;
    void freeAssets();
    // -- recherche approfondie des textures introuvables (dans tout le jeu, en arrière-plan)
    std::shared_ptr<TexSearch> texSearch;
    std::string texSearchReport;
    std::vector<std::string> missingTextures() const;   // (introuvables, ou affichées avec une voisine)
    void startTexSearch();
    bool pollTexSearch();   // (vrai à la fin d'une recherche : le rapport est prêt)

    // -- messages
    std::string toast;
    double toastUntil = 0;
    bool toastWarn = false;
    void say(const std::string& s, bool warn = false);
    std::vector<std::string> buildLog;
    std::atomic<bool> building{false};
    void buildMod();
    std::vector<std::string> logCopy();

private:
    std::mutex logM_;
    std::thread buildThread_, searchThread_;
};

std::string fmtNum(double v);
std::array<float, 3> hueColor(const std::string& s);

}  // namespace te
