#include "app/render.hpp"

#include <algorithm>
#include <cstring>

#include "app/shaders_spv.hpp"
#include "core/util.hpp"

namespace te {

VkPipeline Renderer::pipeline(const uint32_t* vs, size_t vsz, const uint32_t* fs, size_t fsz, uint32_t stride,
                              const std::vector<VkVertexInputAttributeDescription>& attrs, VkPrimitiveTopology topo, bool depthTest,
                              bool depthWrite, bool cull, bool blend, bool additive) {
    VkShaderModule v = vk.shader(vs, vsz), f = vk.shader(fs, fsz);
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, v, "main", nullptr};
    st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, f, "main", nullptr};
    VkVertexInputBindingDescription bind{0, stride, VK_VERTEX_INPUT_RATE_VERTEX};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = stride ? 1 : 0;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = uint32_t(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = topo;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    if (passFor_) { rs.depthBiasEnable = VK_TRUE; rs.depthBiasConstantFactor = 1.5f; rs.depthBiasSlopeFactor = 2.0f; }
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = passFor_ ? VK_SAMPLE_COUNT_1_BIT : vk.samples;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = depthTest;
    ds.depthWriteEnable = depthWrite;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState cb{};
    cb.colorWriteMask = 0xF;
    cb.blendEnable = blend || topo == VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cb.dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb.colorBlendOp = VK_BLEND_OP_ADD;
    cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cb.alphaBlendOp = VK_BLEND_OP_ADD;
    VkPipelineColorBlendStateCreateInfo bs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    bs.attachmentCount = passFor_ ? 0 : 1;
    bs.pAttachments = &cb;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pc{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pc.stageCount = 2;
    pc.pStages = st;
    pc.pVertexInputState = &vi;
    pc.pInputAssemblyState = &ia;
    pc.pViewportState = &vp;
    pc.pRasterizationState = &rs;
    pc.pMultisampleState = &ms;
    pc.pDepthStencilState = &ds;
    pc.pColorBlendState = &bs;
    pc.pDynamicState = &dy;
    pc.layout = layout;
    pc.renderPass = passFor_ ? passFor_ : vk.pass;
    VkPipeline p;
    check(vkCreateGraphicsPipelines(vk.dev, VK_NULL_HANDLE, 1, &pc, nullptr, &p), "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(vk.dev, v, nullptr);
    vkDestroyShaderModule(vk.dev, f, nullptr);
    return p;
}

void Renderer::init() {
    if (vk.props.limits.maxPushConstantsSize < sizeof(PC) || vk.props.limits.maxBoundDescriptorSets < 9)
        throw Error(fmt("carte graphique trop limitée : %u octets de constantes (il en faut %zu), %u jeux de textures (il en faut 9)",
                        vk.props.limits.maxPushConstantsSize, sizeof(PC), vk.props.limits.maxBoundDescriptorSets));
    VkPushConstantRange r{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC)};
    VkPipelineLayoutCreateInfo lc{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lc.setLayoutCount = 1;
    lc.pSetLayouts = &vk.texLayout;
    lc.pushConstantRangeCount = 1;
    lc.pPushConstantRanges = &r;
    check(vkCreatePipelineLayout(vk.dev, &lc, nullptr, &layout), "vkCreatePipelineLayout");
    initScene();
    {
        VkDescriptorSetLayout sets[9] = {vk.texLayout, vk.texLayout, vk.texLayout, vk.texLayout, vk.texLayout, vk.texLayout, vk.texLayout, vk.texLayout, sceneLayout};
        VkPipelineLayoutCreateInfo ml = lc;
        ml.setLayoutCount = 9;
        ml.pSetLayouts = sets;
        check(vkCreatePipelineLayout(vk.dev, &ml, nullptr, &meshLayout), "vkCreatePipelineLayout (modèles)");
        VkPipelineLayout keep = layout;
        layout = meshLayout;
        // (sommet : position, coordonnées de texture, couleur du jeu)
        meshPipe = pipeline(SPV_MESH_VERT, sizeof SPV_MESH_VERT, SPV_MESH_FRAG, sizeof SPV_MESH_FRAG, 72,
                            {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 20},
                             {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 36}, {4, 0, VK_FORMAT_R32G32_SFLOAT, 48}, {5, 0, VK_FORMAT_R32G32_SFLOAT, 56}, {6, 0, VK_FORMAT_R32G32_SFLOAT, 64}},
                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, false);
        meshBlendPipe = pipeline(SPV_MESH_VERT, sizeof SPV_MESH_VERT, SPV_MESH_FRAG, sizeof SPV_MESH_FRAG, 72,
                                 {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 20},
                             {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 36}, {4, 0, VK_FORMAT_R32G32_SFLOAT, 48}, {5, 0, VK_FORMAT_R32G32_SFLOAT, 56}, {6, 0, VK_FORMAT_R32G32_SFLOAT, 64}},
                                 VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, false, true);
        // (carte d'ombre : mêmes sommets, découpe alpha seulement)
        passFor_ = shadowPass;
        shadowMeshPipe = pipeline(SPV_MESH_VERT, sizeof SPV_MESH_VERT, SPV_SHADOW_FRAG, sizeof SPV_SHADOW_FRAG, 72,
                                  {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 20},
                                   {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 36}, {4, 0, VK_FORMAT_R32G32_SFLOAT, 48}, {5, 0, VK_FORMAT_R32G32_SFLOAT, 56}, {6, 0, VK_FORMAT_R32G32_SFLOAT, 64}},
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, false);
        passFor_ = VK_NULL_HANDLE;
        layout = keep;
    }
    {
        VkDescriptorSetLayout sets[3] = {vk.texLayout, vk.texLayout, sceneLayout};
        VkPipelineLayoutCreateInfo wl = lc;
        wl.setLayoutCount = 3;
        wl.pSetLayouts = sets;
        check(vkCreatePipelineLayout(vk.dev, &wl, nullptr, &waterLayout), "vkCreatePipelineLayout (eau)");
        VkPipelineLayout keep = layout;
        layout = waterLayout;
        waterPipe = pipeline(SPV_WATER_VERT, sizeof SPV_WATER_VERT, SPV_WATER_FRAG, sizeof SPV_WATER_FRAG, 72,
                             {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 12}}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true,
                             false, false, true);
        layout = keep;
    }
    {
        VkDescriptorSetLayout sets[6] = {vk.texLayout, vk.texLayout, vk.texLayout, vk.texLayout, vk.texLayout, sceneLayout};
        VkPipelineLayoutCreateInfo tl = lc;
        tl.setLayoutCount = 6;
        tl.pSetLayouts = sets;
        check(vkCreatePipelineLayout(vk.dev, &tl, nullptr, &terrLayout), "vkCreatePipelineLayout (terrain)");
        VkPipelineLayout keep = layout;
        layout = terrLayout;
        terrPipe = pipeline(SPV_TERRAIN_VERT, sizeof SPV_TERRAIN_VERT, SPV_TERRAIN_FRAG, sizeof SPV_TERRAIN_FRAG, uint32_t(sizeof(TerrVert)),
                            {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 28}, {3, 0, VK_FORMAT_R32_SFLOAT, 40}},
                            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, false);
        passFor_ = shadowPass;
        shadowTerrPipe = pipeline(SPV_TERRAIN_VERT, sizeof SPV_TERRAIN_VERT, SPV_SHADOW_T_FRAG, sizeof SPV_SHADOW_T_FRAG, uint32_t(sizeof(TerrVert)),
                                  {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 28}, {3, 0, VK_FORMAT_R32_SFLOAT, 40}},
                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, false);
        passFor_ = VK_NULL_HANDLE;
        layout = keep;
    }
    {
        // (ciel : un triangle plein écran, sans sommets ni profondeur)
        VkPipelineLayoutCreateInfo sl = lc;
        sl.setLayoutCount = 1;
        sl.pSetLayouts = &sceneLayout;
        check(vkCreatePipelineLayout(vk.dev, &sl, nullptr, &skyLayout), "vkCreatePipelineLayout (ciel)");
        VkPipelineLayout keep = layout;
        layout = skyLayout;
        skyPipe = pipeline(SPV_SKY_VERT, sizeof SPV_SKY_VERT, SPV_SKY_FRAG, sizeof SPV_SKY_FRAG, 0, {}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, false, false, false);
        layout = keep;
    }
    {
        // (particules : position, coordonnées de texture, couleur ; sans écriture de profondeur)
        std::vector<VkVertexInputAttributeDescription> a = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 12}, {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 20}};
        fxPipe = pipeline(SPV_FX_VERT, sizeof SPV_FX_VERT, SPV_FX_FRAG, sizeof SPV_FX_FRAG, 36, a, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false,
                          false, true);
        fxAddPipe = pipeline(SPV_FX_VERT, sizeof SPV_FX_VERT, SPV_FX_FRAG, sizeof SPV_FX_FRAG, 36, a, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true,
                             false, false, true, true);
    }
    linePipe = pipeline(SPV_LINE_VERT, sizeof SPV_LINE_VERT, SPV_LINE_FRAG, sizeof SPV_LINE_FRAG, 28,
                        {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}}, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, true, false, false);
    lineTopPipe = pipeline(SPV_LINE_VERT, sizeof SPV_LINE_VERT, SPV_LINE_FRAG, sizeof SPV_LINE_FRAG, 28,
                           {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12}}, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, false, false);
    uint32_t px = 0xFFFFFFFF;
    VkComponentSwizzle id[4] = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    white = vk.texture(VK_FORMAT_R8G8B8A8_UNORM, 1, 1, &px, 4, id, false);
    whiteArr = vk.texture(VK_FORMAT_R8G8B8A8_UNORM, 1, 1, &px, 4, id, false, 1, true);
    // boîte unité (objets sans modèle)
    Mesh m;
    const float s = 0.5f;
    const float P[8][3] = {{-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s}, {-s, -s, s}, {s, -s, s}, {s, s, s}, {-s, s, s}};
    const int F[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    for (auto& f : F) {
        uint32_t b = uint32_t(m.positions.size() / 3);
        for (int k = 0; k < 4; k++) { for (int c = 0; c < 3; c++) m.positions.push_back(P[f[k]][c]); m.uvs.push_back(0); m.uvs.push_back(0); }
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(b + i);
    }
    m.subs.push_back({0, uint32_t(m.indices.size()), ""});
    for (int c = 0; c < 3; c++) { m.bbox[c] = -s; m.bbox[3 + c] = s; }
    cube = upload(m);
}

GpuMesh Renderer::upload(const Mesh& m) {
    GpuMesh g;
    size_t nv = m.positions.size() / 3;
    std::vector<float> v(nv * 18);
    for (size_t i = 0; i < nv; i++) {
        float* o = &v[i * 18];
        std::memcpy(o, &m.positions[i * 3], 12);
        o[3] = i * 2 < m.uvs.size() ? m.uvs[i * 2] : 0.f;
        o[4] = i * 2 + 1 < m.uvs.size() ? m.uvs[i * 2 + 1] : 0.f;
        if (i * 4 + 3 < m.colors.size()) std::memcpy(o + 5, &m.colors[i * 4], 16);
        else { o[5] = o[6] = o[7] = 1.f; o[8] = 1.f; }
        if (i * 3 + 2 < m.normals.size()) std::memcpy(o + 9, &m.normals[i * 3], 12);
        else o[9] = o[10] = o[11] = 0.f;
        if (i * 2 + 1 < m.bakeUvs.size()) { o[12] = m.bakeUvs[i * 2]; o[13] = m.bakeUvs[i * 2 + 1]; }
        else o[12] = o[13] = -1.f;
        if (i * 2 + 1 < m.uvs2.size()) { o[14] = m.uvs2[i * 2]; o[15] = m.uvs2[i * 2 + 1]; }
        else { o[14] = o[3]; o[15] = o[4]; }
        if (i * 2 + 1 < m.uvs3.size()) { o[16] = m.uvs3[i * 2]; o[17] = m.uvs3[i * 2 + 1]; }
        else { o[16] = o[3]; o[17] = o[4]; }
    }
    g.vb = vk.upload(v.data(), v.size() * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    g.ib = vk.upload(m.indices.data(), m.indices.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    for (auto& s : m.subs) {
        int kind = s.tex.empty() ? s.kind : (s.kind == 4 ? 4 : 0);
        g.subs.push_back({s.start, s.count, s.tex, kind, s.nrm, s.flow, s.tex2, s.detail, s.material, s.matIndex,
                          s.nmap, s.rgh, s.nmap2, s.rah2, s.rghCh, s.layered, s.rah, s.alphaTest, s.recipe, s.mp});
        if (kind) g.translucent = true;
    }
    std::memcpy(g.bbox, m.bbox, sizeof g.bbox);
    g.tris = m.indices.size() / 3;
    return g;
}

void Renderer::free(GpuMesh& g) {
    vk.free(g.vb);
    vk.free(g.ib);
    g.subs.clear();
}

void Renderer::setTerrainTable(const std::vector<std::array<float, 3>>& t) {
    vkDeviceWaitIdle(vk.dev);
    vk.free(terrMat);
    std::vector<uint8_t> px(128 * 4, 0);
    for (int k = 0; k < 128; k++) {
        float layer = float(k), u = 0.25f, v = 0.25f;
        if (size_t(k) < t.size()) { layer = t[size_t(k)][0]; u = t[size_t(k)][1]; v = t[size_t(k)][2]; }
        px[size_t(k) * 4] = uint8_t(std::clamp(layer, 0.f, 255.f));
        px[size_t(k) * 4 + 1] = uint8_t(std::clamp(u * 64.f + 0.5f, 1.f, 255.f));
        px[size_t(k) * 4 + 2] = uint8_t(std::clamp(v * 64.f + 0.5f, 1.f, 255.f));
        px[size_t(k) * 4 + 3] = 255;
    }
    VkComponentSwizzle id[4] = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    terrMat = vk.texture(VK_FORMAT_R8G8B8A8_UNORM, 128, 1, px.data(), px.size(), id, false, 1, true);
}

void Renderer::setTerrainMaterials(VkFormat f, int w, int h, int layers, const void* data, size_t size, bool relief) {
    vkDeviceWaitIdle(vk.dev);
    VkTex& t = relief ? terrCmb : terrAlb;
    int& n = relief ? terrCmbLayers : terrLayers;
    vk.free(t);
    n = 0;
    if (!layers || !data) return;
    VkComponentSwizzle id[4] = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    t = vk.texture(f, w, h, data, size, id, true, layers, true);
    n = layers;
}

void Renderer::setTerrain(const std::vector<TerrVert>& v, const std::vector<uint32_t>& idx, const std::vector<uint8_t>& ids,
                          const std::vector<uint8_t>& wts, int grid, int tiles) {
    vkDeviceWaitIdle(vk.dev);
    vk.free(terrVb);
    vk.free(terrIb);
    vk.free(terrIds);
    vk.free(terrWts);
    terrCount = 0;
    if (v.empty() || idx.empty()) return;
    if (grid > 1 && tiles > 0 && ids.size() == size_t(grid) * size_t(grid) * 4 * size_t(tiles) && wts.size() == ids.size()) {
        VkComponentSwizzle id[4] = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        uint32_t maxL = vk.props.limits.maxImageArrayLayers;
        if (uint32_t(tiles) <= maxL) {
            terrIds = vk.texture(VK_FORMAT_R8G8B8A8_UNORM, grid, grid, ids.data(), ids.size(), id, false, tiles, true);
            terrWts = vk.texture(VK_FORMAT_R8G8B8A8_UNORM, grid, grid, wts.data(), wts.size(), id, false, tiles, true);
        }
    }
    terrVb = vk.upload(v.data(), v.size() * sizeof(TerrVert), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    terrIb = vk.upload(idx.data(), idx.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    terrCount = uint32_t(idx.size());
}

void Renderer::bindMesh(VkCommandBuffer c) {
    if (shadowMode) { vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMeshPipe); return; }
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipe);
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, meshLayout, 8, 1, &sceneSet[vk.frame], 0, nullptr);
}

void Renderer::bindMeshBlend(VkCommandBuffer c) {
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, meshBlendPipe);
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, meshLayout, 8, 1, &sceneSet[vk.frame], 0, nullptr);
}

void Renderer::drawMesh(VkCommandBuffer c, const GpuMesh& g, const PC& pc, const std::vector<SubTex>& tex, bool translucent, const Bake* bake) {
    if (translucent && !g.translucent) return;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(c, 0, 1, &g.vb.buf, &off);
    vkCmdBindIndexBuffer(c, g.ib.buf, 0, VK_INDEX_TYPE_UINT32);
    for (size_t i = 0; i < g.subs.size(); i++) {
        int kind = g.subs[i].kind;
        if ((kind != 0) != translucent) continue;
        if (translucent && kind == 1 && waterPipe) continue;   // (l'eau : drawWater)
        PC p = pc;
        if (translucent && kind != 4) {
            // (eau : bleu-vert ; fondu ou lumière : blanc très léger ; verre ou glace : bleu pâle)
            static const float K[4][4] = {{0, 0, 0, 1}, {0.22f, 0.42f, 0.52f, 0.5f}, {1.f, 1.f, 0.95f, 0.12f}, {0.75f, 0.85f, 0.9f, 0.3f}};
            int k = std::clamp(kind, 0, 3);
            for (int q = 0; q < 4; q++) p.color[q] = K[k][q];
        } else if (g.subs[i].tex.empty() && g.subs[i].mat.find("InnerMesh") != std::string::npos) {
            // (« InnerMesh » : sol et parois intérieures sans texture, d'une couleur de terre plutôt que grise)
            p.color[0] = 0.33f; p.color[1] = 0.26f; p.color[2] = 0.2f;
        }
        SubTex t = i < tex.size() ? tex[i] : SubTex{};
        bool lm = bake && bake->map && i < bake->sub.size() && bake->sub[i][2] > 0.f && t[0];
        VkDescriptorSet sets[8] = {t[0] ? t[0] : white.set, t[1] ? t[1] : white.set, t[2] ? t[2] : white.set, lm ? bake->map : white.set,
                                   t[3] ? t[3] : white.set, t[4] ? t[4] : white.set, t[5] ? t[5] : white.set, t[6] ? t[6] : white.set};
        // (scale.w : 1 couleur, 2 seconde couleur, 4 détail, 8 couche par-dessus, 16 éclairage précalculé : pc.color =
        //  décalage et échelle dans la carte, inutilisé sinon par les parties texturées ; 32 carte de normales,
        //  64 rugosité (canal : bits 10-11), 128 normales de la seconde couche, 256 RAH de la seconde couche,
        //  512 deux couches du jeu (graphe 3552031828), 4096 la rugosité est un RAH (hauteur dans B), 8192 découpe alpha)
        const auto& sb = g.subs[i];
        bool layered = sb.layered && t[0] && t[1];
        for (int q = 0; q < 5; q++) for (int w = 0; w < 4; w++) p.m[q][w] = sb.mp[size_t(q)][size_t(w)];
        p.m[0][3] = layered ? float(sb.recipe) : 0.f;
        p.m[2][3] = pc.m[2][3];   // (exposition de la courbe de tons du jeu)
        p.m[1][3] = pc.m[1][3];   // (teinte du soleil : v/r et b/r)
        p.m[4][3] = pc.m[4][3];
        int flags = (t[0] ? 1 : 0) | (t[0] && t[1] ? 2 : 0) | (t[0] && t[2] ? 4 : 0) | (kind == 4 ? 8 : 0) | (lm ? 16 : 0) |
                    (t[0] && t[3] ? 32 : 0) | (t[0] && t[4] ? 64 | (sb.rghCh & 3) << 10 : 0) | (layered && t[5] ? 128 : 0) |
                    (layered && t[6] ? 256 : 0) | (layered ? 512 : 0) | (sb.rah ? 4096 : 0) | (sb.alphaTest ? 8192 : 0);
        if (lm) for (int q = 0; q < 4; q++) p.color[q] = bake->sub[i][size_t(q)];
        p.scale[3] = float(flags);
        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, meshLayout, 0, 8, sets, 0, nullptr);
        vkCmdPushConstants(c, meshLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC), &p);
        vkCmdDrawIndexed(c, g.subs[i].count, 1, g.subs[i].start, 0, 0);
    }
}

void Renderer::drawWater(VkCommandBuffer c, const GpuMesh& g, const PC& pc, const std::vector<VkDescriptorSet>& nrm,
                         const std::vector<VkDescriptorSet>& flow, const std::vector<char>& signedNrm, float time) {
    if (!g.translucent || !waterPipe) return;
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(c, 0, 1, &g.vb.buf, &off);
    vkCmdBindIndexBuffer(c, g.ib.buf, 0, VK_INDEX_TYPE_UINT32);
    for (size_t i = 0; i < g.subs.size(); i++) {
        if (g.subs[i].kind != 1) continue;
        PC p = pc;
        // (couleur du fond : vert-bleu des rivières d'Hyrule ; a : temps)
        p.color[0] = 0.06f; p.color[1] = 0.24f; p.color[2] = 0.27f; p.color[3] = time;
        VkDescriptorSet sets[2] = {i < nrm.size() && nrm[i] ? nrm[i] : white.set, i < flow.size() && flow[i] ? flow[i] : white.set};
        // (scale.w : 1 = carte de normales, 2 = carte de courant, 4 = normales signées)
        int flags = (sets[0] != white.set ? 1 : 0) | (sets[1] != white.set ? 2 : 0) | (i < signedNrm.size() && signedNrm[i] ? 4 : 0);
        p.scale[3] = float(flags);
        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, waterLayout, 0, 2, sets, 0, nullptr);
        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, waterLayout, 2, 1, &sceneSet[vk.frame], 0, nullptr);
        vkCmdPushConstants(c, waterLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC), &p);
        vkCmdDrawIndexed(c, g.subs[i].count, 1, g.subs[i].start, 0, 0);
    }
}

void Renderer::drawTerrain(VkCommandBuffer c, const PC& pc) {
    if (!terrCount) return;
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMode ? shadowTerrPipe : terrPipe);
    if (!shadowMode) vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, terrLayout, 5, 1, &sceneSet[vk.frame], 0, nullptr);
    if (!terrMat.set) setTerrainTable({});
    bool tex = terrLayers > 0 && terrIds.set && terrWts.set;
    bool relief = tex && terrRelief && terrCmbLayers == terrLayers && terrCmb.set;
    VkDescriptorSet sets[5] = {tex ? terrAlb.set : whiteArr.set, tex ? terrIds.set : whiteArr.set, tex ? terrWts.set : whiteArr.set, terrMat.set,
                               relief ? terrCmb.set : whiteArr.set};
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, terrLayout, 0, 5, sets, 0, nullptr);
    PC p = pc;
    p.scale[0] = terrUvScale;
    p.scale[1] = float(terrLayers);
    p.scale[3] = tex ? 1.f : 0.f;
    p.scale[2] = relief ? 1.f : 0.f;
    vkCmdPushConstants(c, terrLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC), &p);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(c, 0, 1, &terrVb.buf, &off);
    vkCmdBindIndexBuffer(c, terrIb.buf, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(c, terrCount, 1, 0, 0, 0);
}

void Renderer::drawLines(VkCommandBuffer c, const PC& pc) {
    size_t n = lines.size() + linesTop.size();
    if (!n) return;
    VkBuf& b = lineBuf_[vk.frame];
    VkDeviceSize need = n * sizeof(LineVert);
    if (b.size < need) {
        if (b.buf) vk.free(b);
        b = vk.buffer(need * 2, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    }
    std::memcpy(b.map, lines.data(), lines.size() * sizeof(LineVert));
    std::memcpy(static_cast<char*>(b.map) + lines.size() * sizeof(LineVert), linesTop.data(), linesTop.size() * sizeof(LineVert));
    VkDeviceSize off = 0;
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &white.set, 0, nullptr);
    vkCmdPushConstants(c, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC), &pc);
    vkCmdBindVertexBuffers(c, 0, 1, &b.buf, &off);
    if (!lines.empty()) {
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipe);
        vkCmdDraw(c, uint32_t(lines.size()), 1, 0, 0);
    }
    if (!linesTop.empty()) {
        vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, lineTopPipe);
        vkCmdDraw(c, uint32_t(linesTop.size()), 1, uint32_t(lines.size()), 0);
    }
}

void Renderer::drawFx(VkCommandBuffer c, const PC& pc) {
    if (fx.empty() || fxBatches.empty()) return;
    VkBuf& b = fxBuf_[vk.frame];
    VkDeviceSize need = fx.size() * sizeof(FxVert);
    if (b.size < need) {
        if (b.buf) vk.free(b);
        b = vk.buffer(need * 2, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    }
    std::memcpy(b.map, fx.data(), need);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(c, 0, 1, &b.buf, &off);
    VkPipeline cur = VK_NULL_HANDLE;
    for (auto& fb : fxBatches) {
        if (!fb.count) continue;
        VkPipeline p = fb.additive ? fxAddPipe : fxPipe;
        if (p != cur) { vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, p); cur = p; }
        VkDescriptorSet set = fb.tex ? fb.tex : white.set;
        vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
        PC p2 = pc;
        p2.scale[0] = float(fb.tex ? fb.mode : (fb.mode == 1 || fb.mode == 2 ? 0 : fb.mode == 5 || fb.mode == 6 ? 3 : fb.mode));
        vkCmdPushConstants(c, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PC), &p2);
        vkCmdDraw(c, fb.count, 1, fb.first, 0);
    }
}

void Renderer::initScene() {
    // (jeu : tampon uniforme + carte d'ombre comparée)
    VkDescriptorSetLayoutBinding b[2]{};
    b[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    b[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2;
    dl.pBindings = b;
    check(vkCreateDescriptorSetLayout(vk.dev, &dl, nullptr, &sceneLayout), "vkCreateDescriptorSetLayout (scène)");
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Vk::FRAMES}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, Vk::FRAMES}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = Vk::FRAMES;
    dp.poolSizeCount = 2;
    dp.pPoolSizes = ps;
    check(vkCreateDescriptorPool(vk.dev, &dp, nullptr, &scenePool), "vkCreateDescriptorPool (scène)");
    // (carte d'ombre)
    VkFormat df = VK_FORMAT_D32_SFLOAT;
    vk.image(shadowSize, shadowSize, 1, df, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, shadowImg, shadowMem);
    VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vc.image = shadowImg;
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vc.format = df;
    vc.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    check(vkCreateImageView(vk.dev, &vc, nullptr, &shadowView), "vkCreateImageView (ombre)");
    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sc.magFilter = sc.minFilter = VK_FILTER_LINEAR;
    sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sc.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    sc.compareEnable = VK_TRUE;
    sc.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    sc.maxLod = 0;
    check(vkCreateSampler(vk.dev, &sc, nullptr, &shadowSampler), "vkCreateSampler (ombre)");
    VkAttachmentDescription at{};
    at.format = df;
    at.samples = VK_SAMPLE_COUNT_1_BIT;
    at.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    at.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.pDepthStencilAttachment = &ref;
    VkSubpassDependency dep[2]{};
    dep[0] = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_SHADER_READ_BIT,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0};
    dep[1] = {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0};
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &at;
    rp.subpassCount = 1;
    rp.pSubpasses = &sp;
    rp.dependencyCount = 2;
    rp.pDependencies = dep;
    check(vkCreateRenderPass(vk.dev, &rp, nullptr, &shadowPass), "vkCreateRenderPass (ombre)");
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = shadowPass;
    fb.attachmentCount = 1;
    fb.pAttachments = &shadowView;
    fb.width = fb.height = shadowSize;
    fb.layers = 1;
    check(vkCreateFramebuffer(vk.dev, &fb, nullptr, &shadowFb), "vkCreateFramebuffer (ombre)");
    // (première mise en place de la carte : effacée, prête à être lue)
    vk.submitNow([&](VkCommandBuffer c) { beginShadow(c); endShadow(c); });
    for (int f = 0; f < Vk::FRAMES; f++) {
        sceneBuf[f] = vk.buffer(sizeof(SceneUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        SceneUbo z{};
        std::memcpy(sceneBuf[f].map, &z, sizeof z);
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        da.descriptorPool = scenePool;
        da.descriptorSetCount = 1;
        da.pSetLayouts = &sceneLayout;
        check(vkAllocateDescriptorSets(vk.dev, &da, &sceneSet[f]), "vkAllocateDescriptorSets (scène)");
        VkDescriptorBufferInfo bi{sceneBuf[f].buf, 0, sizeof(SceneUbo)};
        VkDescriptorImageInfo ii{shadowSampler, shadowView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[2]{};
        w[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sceneSet[f], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bi, nullptr};
        w[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sceneSet[f], 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, nullptr, nullptr};
        vkUpdateDescriptorSets(vk.dev, 2, w, 0, nullptr);
    }
}

void Renderer::setScene(const SceneUbo& u) { std::memcpy(sceneBuf[vk.frame].map, &u, sizeof u); }

void Renderer::beginShadow(VkCommandBuffer c) {
    VkClearValue cv{};
    cv.depthStencil = {1.f, 0};
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = shadowPass;
    rb.framebuffer = shadowFb;
    rb.renderArea = {{0, 0}, {shadowSize, shadowSize}};
    rb.clearValueCount = 1;
    rb.pClearValues = &cv;
    vkCmdBeginRenderPass(c, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport v{0, 0, float(shadowSize), float(shadowSize), 0, 1};
    VkRect2D sc{{0, 0}, {shadowSize, shadowSize}};
    vkCmdSetViewport(c, 0, 1, &v);
    vkCmdSetScissor(c, 0, 1, &sc);
    shadowMode = true;
}

void Renderer::endShadow(VkCommandBuffer c) {
    vkCmdEndRenderPass(c);
    shadowMode = false;
}

void Renderer::drawSky(VkCommandBuffer c) {
    vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipe);
    vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, skyLayout, 0, 1, &sceneSet[vk.frame], 0, nullptr);
    vkCmdDraw(c, 3, 1, 0, 0);
}

void Renderer::destroy() {
    vkDeviceWaitIdle(vk.dev);
    for (auto& b : lineBuf_) vk.free(b);
    for (auto& b : fxBuf_) vk.free(b);
    vk.free(terrVb);
    vk.free(terrIb);
    vk.free(terrIds);
    vk.free(terrWts);
    vk.free(terrAlb);
    vk.free(terrMat);
    vk.free(terrCmb);
    free(cube);
    vk.free(white);
    vk.free(whiteArr);
    for (auto p : {waterPipe, meshPipe, meshBlendPipe, terrPipe, linePipe, lineTopPipe, fxPipe, fxAddPipe, shadowMeshPipe, shadowTerrPipe, skyPipe})
        vkDestroyPipeline(vk.dev, p, nullptr);
    for (auto& b : sceneBuf) vk.free(b);
    vkDestroyFramebuffer(vk.dev, shadowFb, nullptr);
    vkDestroyRenderPass(vk.dev, shadowPass, nullptr);
    vkDestroySampler(vk.dev, shadowSampler, nullptr);
    vkDestroyImageView(vk.dev, shadowView, nullptr);
    vkDestroyImage(vk.dev, shadowImg, nullptr);
    vkFreeMemory(vk.dev, shadowMem, nullptr);
    vkDestroyDescriptorPool(vk.dev, scenePool, nullptr);
    vkDestroyDescriptorSetLayout(vk.dev, sceneLayout, nullptr);
    vkDestroyPipelineLayout(vk.dev, skyLayout, nullptr);
    vkDestroyPipelineLayout(vk.dev, layout, nullptr);
    vkDestroyPipelineLayout(vk.dev, terrLayout, nullptr);
    vkDestroyPipelineLayout(vk.dev, waterLayout, nullptr);
    vkDestroyPipelineLayout(vk.dev, meshLayout, nullptr);
}

}  // namespace te
