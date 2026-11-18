// Rendu Vulkan de la vue 3D : modèles (texturés ou non), boîtes, terrain, lignes.
#pragma once
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
    struct Sub { uint32_t start, count; std::string tex; };
    std::vector<Sub> subs;
    float bbox[6] = {0, 0, 0, 0, 0, 0};
    size_t tris = 0;
};

struct LineVert { float p[3]; float c[4]; };
struct TerrVert { float p[3]; float c[4]; };

class Renderer {
public:
    explicit Renderer(Vk& v) : vk(v) {}
    Vk& vk;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline meshPipe = VK_NULL_HANDLE, terrPipe = VK_NULL_HANDLE, linePipe = VK_NULL_HANDLE, lineTopPipe = VK_NULL_HANDLE;
    VkTex white;
    GpuMesh cube;

    void init();
    void destroy();
    GpuMesh upload(const Mesh& m);
    void free(GpuMesh& g);

    // terrain : un seul maillage
    VkBuf terrVb, terrIb;
    uint32_t terrCount = 0;
    void setTerrain(const std::vector<TerrVert>& v, const std::vector<uint32_t>& idx);

    // lignes de la trame (grille, sélection) ; top : par-dessus tout
    std::vector<LineVert> lines, linesTop;

    void bindMesh(VkCommandBuffer c);
    void drawMesh(VkCommandBuffer c, const GpuMesh& g, const PC& pc, const std::vector<VkDescriptorSet>& texPerSub);
    void drawTerrain(VkCommandBuffer c, const PC& pc);
    void drawLines(VkCommandBuffer c, const PC& pc);

private:
    VkBuf lineBuf_[Vk::FRAMES];
    VkPipeline pipeline(const uint32_t* vs, size_t vsz, const uint32_t* fs, size_t fsz, uint32_t stride,
                        const std::vector<VkVertexInputAttributeDescription>& attrs, VkPrimitiveTopology topo, bool depthTest, bool depthWrite, bool cull);
};

}  // namespace te
