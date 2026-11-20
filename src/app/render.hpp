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
};
static_assert(sizeof(PC) == 128, "constantes de 128 octets");

struct GpuMesh {
    VkBuf vb, ib;
    struct Sub { uint32_t start, count; std::string tex; int kind = 0; std::string nrm, flow;
                 std::string tex2, detail, mat; int matIndex = 0; };   // (kind 4 : couche posée par-dessus, opacité des sommets)
    std::vector<Sub> subs;
    bool translucent = false;   // (a des parties d'eau, de fondu ou de verre)
    float bbox[6] = {0, 0, 0, 0, 0, 0};
    size_t tris = 0;
};

struct LineVert { float p[3]; float c[4]; };
struct FxVert { float p[3]; float uv[2]; float c[4]; };
// (une suite de particules d'un même effet : sa texture, sa forme — voir shaders/fx.frag — et son mélange)
struct FxBatch { VkDescriptorSet tex = VK_NULL_HANDLE; int mode = 0; bool additive = false; uint32_t first = 0, count = 0; };
struct TerrVert { float p[3]; float c[4]; float t[3]; };   // (c.a : côté de la grille de la tuile ; t : place dans la tuile, n° de tuile)

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
    // translucent : seulement les parties sans texture d'eau, de fondu ou de verre (à dessiner après le reste) ;
    // sinon tout sauf elles
    // (tex : par partie, couleur, seconde couleur, détail ; VK_NULL_HANDLE = sans)
    using SubTex = std::array<VkDescriptorSet, 3>;
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

private:
    VkBuf lineBuf_[Vk::FRAMES], fxBuf_[Vk::FRAMES];
    VkPipeline pipeline(const uint32_t* vs, size_t vsz, const uint32_t* fs, size_t fsz, uint32_t stride,
                        const std::vector<VkVertexInputAttributeDescription>& attrs, VkPrimitiveTopology topo, bool depthTest, bool depthWrite, bool cull,
                        bool blend = false, bool additive = false);
};

}  // namespace te
