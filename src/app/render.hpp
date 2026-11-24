// Rendu Vulkan de la vue 3D : modèles (texturés ou non), boîtes, terrain, lignes.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "app/vk.hpp"
#include "core/bfres.hpp"

namespace te {

struct PC {
    float vp[16];
    float pos[4];
    float scale[4];
    float rot[4];
    float color[4];
    // (recettes de mélange des matériaux du jeu : couleurs A, B, masque (a de m[0] : recette) ; scalaires du matériau)
    float m[5][4];
    // (éclairement ambiant des sondes de la scène autour de l'objet : a + M·n, voir ProbeSet::ambient ; nul = sans)
    float amb[3][4];
};
static_assert(sizeof(PC) == 256, "constantes de 256 octets");

// (lumière de la scène, voir shaders/scene.glsl : même disposition std140)
struct SceneUbo {
    float lightVP[16], invVP[16], eye[4], viewport[4], shadowP[4], sunDirS[4], sunCol[4], skyCol[4], ambUp[4];
    float fogA[2][4], fogB[2][4], fogRay[2][4], fogMie[2][4], fogAbs[2][4], fogEmit[2][4], fogLight[4], lightN[4];
    float lightPos[16][4], lightCol[16][4], lightDir[16][4];
    float skyR[4], skyM[4], skyX[4], skyK[4], skyG[4], shadowF[4], giScale[4];
};

struct GpuMesh {
    VkBuf vb, ib;
    struct Sub { uint32_t start, count; std::string tex; int kind = 0; std::string nrm, flow;
                 std::string tex2, detail, mat; int matIndex = 0;   // (kind 4 : couche posée par-dessus, opacité des sommets)
                 std::string nmap, rgh, nmap2, rah2; int rghCh = 0; bool layered = false, rah = false, alphaTest = false;
                 int recipe = 0; std::array<std::array<float, 4>, 5> mp{}; };
    std::vector<Sub> subs;
    bool translucent = false;   // (a des parties d'eau, de fondu ou de verre)
    float bbox[6] = {0, 0, 0, 0, 0, 0};
    size_t tris = 0;
};

struct LineVert { float p[3]; float c[4]; };
struct FxVert { float p[3]; float uv[2]; float c[4]; };
// (une suite de particules d'un même effet : sa texture, sa forme — voir shaders/fx.frag — et son mélange)
struct FxBatch { VkDescriptorSet tex = VK_NULL_HANDLE; int mode = 0; bool additive = false; uint32_t first = 0, count = 0; };
struct TerrVert {
    float p[3]; float c[4]; float t[3];
    float ao = 1;                         // (ombre ambiante précalculée du jeu)
    float gi[4] = {-1, 0, 0, 0};          // (lumière précalculée du jeu : facteur du ciel (−1 : aucune), soleil renvoyé rgb)
    float ga[3] = {0, 0, 0};              // (… lumières locales rgb)
};   // (c.a : côté de la grille de la tuile ; t : place dans la tuile, n° de tuile)

class Renderer {
public:
    explicit Renderer(Vk& v) : vk(v) {}
    Vk& vk;
    VkPipelineLayout layout = VK_NULL_HANDLE, terrLayout = VK_NULL_HANDLE, meshLayout = VK_NULL_HANDLE;   // (meshLayout : 3 textures)
    VkPipeline fxPipe = VK_NULL_HANDLE, fxAddPipe = VK_NULL_HANDLE;
    VkPipeline waterPipe = VK_NULL_HANDLE, meshBlendPipe = VK_NULL_HANDLE, meshPipe = VK_NULL_HANDLE, terrPipe = VK_NULL_HANDLE, linePipe = VK_NULL_HANDLE, lineTopPipe = VK_NULL_HANDLE;
    VkTex white, whiteArr;
    GpuMesh cube;

    void init();
    void destroy();
    GpuMesh upload(const Mesh& m);
    void free(GpuMesh& g);

    // terrain : un seul maillage
    VkBuf terrVb, terrIb;
    uint32_t terrCount = 0;
    // ids, wts : grid × grid × 4 octets par tuile (couches) ; vides : couleurs seules
    void setTerrain(const std::vector<TerrVert>& v, const std::vector<uint32_t>& idx, const std::vector<uint8_t>& ids = {},
                    const std::vector<uint8_t>& wts = {}, int grid = 0, int tiles = 0);
    // textures des matières du sol (RGBA8 ou format compressé lu par la carte graphique), une couche par matière
    VkTex terrAlb, terrIds, terrWts, terrMat, terrCmb;
    int terrLayers = 0, terrCmbLayers = 0;
    bool terrRelief = true;
    float terrUvScale = 1.f;   // (multiplie les répétitions du jeu)
    // table des matières : par numéro, couche de MaterialAlb et répétitions u, v par mètre
    void setTerrainTable(const std::vector<std::array<float, 3>>& t);
    void setTerrainMaterials(VkFormat f, int w, int h, int layers, const void* data, size_t size, bool relief = false);

    // lignes de la trame (grille, sélection) ; top : par-dessus tout
    std::vector<LineVert> lines, linesTop;

    void bindMesh(VkCommandBuffer c);
    void bindMeshBlend(VkCommandBuffer c);   // (parties transparentes, après l'eau : la lumière de la scène est remise)
    // translucent : seulement les parties sans texture d'eau, de fondu ou de verre (à dessiner après le reste) ;
    // sinon tout sauf elles
    // (tex : par partie, couleur, seconde couleur, détail ; VK_NULL_HANDLE = sans)
    // (… puis carte de normales, rugosité (ou RAH), normales et RAH de la seconde couche)
    using SubTex = std::array<VkDescriptorSet, 7>;
    // (éclairage précalculé d'un objet : sa carte, et pour chaque partie la place de ses coordonnées dans la carte —
    //  décalage u, v puis échelle u, v ; échelle nulle : partie non éclairée)
    struct Bake { VkDescriptorSet map = VK_NULL_HANDLE; std::vector<std::array<float, 4>> sub; float strength = 1.f; };
    void drawMesh(VkCommandBuffer c, const GpuMesh& g, const PC& pc, const std::vector<SubTex>& tex, bool translucent = false, const Bake* bake = nullptr);
    // (parties d'eau : leur carte de normales par partie, `signedNrm` si la carte est signée ; time en secondes)
    void drawWater(VkCommandBuffer c, const GpuMesh& g, const PC& pc, const std::vector<VkDescriptorSet>& nrm,
                   const std::vector<VkDescriptorSet>& flow, const std::vector<char>& signedNrm, float time);
    VkPipelineLayout waterLayout = VK_NULL_HANDLE;   // (deux textures : normales, carte de courant)
    void drawTerrain(VkCommandBuffer c, const PC& pc);
    void drawLines(VkCommandBuffer c, const PC& pc);
    // particules des effets (refaites à chaque image ; triangles, 6 sommets par particule)
    std::vector<FxVert> fx;
    std::vector<FxBatch> fxBatches;
    void drawFx(VkCommandBuffer c, const PC& pc);

    // (lumière de la scène : tampon par image, carte d'ombre du soleil, ciel)
    VkDescriptorSetLayout sceneLayout = VK_NULL_HANDLE;
    VkDescriptorPool scenePool = VK_NULL_HANDLE;
    VkDescriptorSet sceneSet[Vk::FRAMES]{};
    VkBuf sceneBuf[Vk::FRAMES];
    VkImage shadowImg = VK_NULL_HANDLE;
    VkDeviceMemory shadowMem = VK_NULL_HANDLE;
    VkImageView shadowView = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkRenderPass shadowPass = VK_NULL_HANDLE;
    VkFramebuffer shadowFb = VK_NULL_HANDLE;
    uint32_t shadowSize = 4096;
    VkPipeline shadowMeshPipe = VK_NULL_HANDLE, shadowTerrPipe = VK_NULL_HANDLE, skyPipe = VK_NULL_HANDLE;
    VkPipelineLayout skyLayout = VK_NULL_HANDLE;
    bool shadowMode = false;   // (entre beginShadow et endShadow : modèles et terrain dans la carte d'ombre)
    void setScene(const SceneUbo& u);
    void beginShadow(VkCommandBuffer c);
    void endShadow(VkCommandBuffer c);
    void drawSky(VkCommandBuffer c);

private:
    VkRenderPass passFor_ = VK_NULL_HANDLE;   // (pipeline() : passe de la carte d'ombre au lieu de la passe principale)
    void initScene();
    VkBuf lineBuf_[Vk::FRAMES], fxBuf_[Vk::FRAMES];
    VkPipeline pipeline(const uint32_t* vs, size_t vsz, const uint32_t* fs, size_t fsz, uint32_t stride,
                        const std::vector<VkVertexInputAttributeDescription>& attrs, VkPrimitiveTopology topo, bool depthTest, bool depthWrite, bool cull,
                        bool blend = false, bool additive = false);
};

}  // namespace te
