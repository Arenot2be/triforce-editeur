// Vulkan : instance, appareil, chaîne d'images, passe de rendu (MSAA + profondeur), tampons et textures.
#pragma once
#include <volk.h>

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace te {

struct VkBuf {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* map = nullptr;
};

struct VkTex {
    VkImage img = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    int w = 0, h = 0;
};

class Vk {
public:
    static constexpr int FRAMES = 2;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceMemoryProperties memProps{};
    bool bc = false;          // (textures BC lues directement par la carte graphique)
    bool anisotropy = false;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    std::string gpuName;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkImage msaaImg = VK_NULL_HANDLE, depthImg = VK_NULL_HANDLE;
    VkDeviceMemory msaaMem = VK_NULL_HANDLE, depthMem = VK_NULL_HANDLE;
    VkImageView msaaView = VK_NULL_HANDLE, depthView = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd[FRAMES]{};
    VkSemaphore acquired[FRAMES]{};
    std::vector<VkSemaphore> rendered;   // (une par image de la chaîne)
    VkFence fence[FRAMES]{};
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout texLayout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    int frame = 0;
    uint32_t imageIndex = 0;
    bool headless = false;
    bool vsync = true;

    void init(GLFWwindow* win, bool headless);
    void destroy();
    void recreateSwapchain(int w, int h);
    VkCommandBuffer beginFrame(int w, int h);   // (VK_NULL_HANDLE : image indisponible, on saute la trame)
    void beginPass(VkCommandBuffer c, const float clear[4]);
    void endFrame(VkCommandBuffer c);
    bool capture(const std::string& pngPath);   // (la dernière image affichée)

    uint32_t memType(uint32_t bits, VkMemoryPropertyFlags want) const;
    VkBuf buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    VkBuf upload(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);   // (mémoire de la carte graphique)
    void free(VkBuf& b);
    void submitNow(const std::function<void(VkCommandBuffer)>& f);
    // texture : données du niveau 0 ; RGBA8 : mipmaps générées ; swz : VkComponentSwizzle pour R G B A
    VkTex texture(VkFormat fmt, int w, int h, const void* data, size_t size, const VkComponentSwizzle swz[4], bool mips);
    void free(VkTex& t);
    bool formatOk(VkFormat f) const;
    VkShaderModule shader(const uint32_t* code, size_t bytes);

private:
    GLFWwindow* win_ = nullptr;
    void createTargets();
    void destroyTargets();
    void image(uint32_t w, uint32_t h, uint32_t mips, VkFormat f, VkSampleCountFlagBits s, VkImageUsageFlags u, VkImage& img, VkDeviceMemory& mem);
};

void check(VkResult r, const char* what);

}  // namespace te
