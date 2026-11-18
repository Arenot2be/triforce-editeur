#include "app/vk.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "core/util.hpp"

namespace te {

void check(VkResult r, const char* what) {
    if (r != VK_SUCCESS) throw Error(fmt("Vulkan : %s a échoué (code %d)", what, int(r)));
}

void Vk::init(GLFWwindow* win, bool hl) {
    win_ = win;
    headless = hl;
    check(volkInitialize(), "le chargement de Vulkan (pilote graphique à jour ?)");
    uint32_t n = 0;
    const char** ext = glfwGetRequiredInstanceExtensions(&n);
    if (!ext) throw Error("GLFW : Vulkan indisponible sur cet ordinateur");
    std::vector<const char*> exts(ext, ext + n);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Triforce editeur";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = uint32_t(exts.size());
    ici.ppEnabledExtensionNames = exts.data();
    check(vkCreateInstance(&ici, nullptr, &instance), "vkCreateInstance");
    volkLoadInstance(instance);
    check(glfwCreateWindowSurface(instance, win, nullptr, &surface), "glfwCreateWindowSurface");

    uint32_t pc = 0;
    vkEnumeratePhysicalDevices(instance, &pc, nullptr);
    std::vector<VkPhysicalDevice> pds(pc);
    vkEnumeratePhysicalDevices(instance, &pc, pds.data());
    int best = -1;
    for (auto pd : pds) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pd, &p);
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qs.data());
        for (uint32_t i = 0; i < qn; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
            if (!(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
            if (score > best) { best = score; phys = pd; family = i; }
            break;
        }
    }
    if (!phys) throw Error("aucune carte graphique Vulkan ne peut afficher la fenêtre");
    vkGetPhysicalDeviceProperties(phys, &props);
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
    gpuName = props.deviceName;
    VkPhysicalDeviceFeatures have{}, want{};
    vkGetPhysicalDeviceFeatures(phys, &have);
    bc = have.textureCompressionBC;
    anisotropy = have.samplerAnisotropy;
    want.textureCompressionBC = have.textureCompressionBC;
    want.samplerAnisotropy = have.samplerAnisotropy;
    float prio = 1;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family;
    q.queueCount = 1;
    q.pQueuePriorities = &prio;
    const char* dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &q;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dext;
    dci.pEnabledFeatures = &want;
    check(vkCreateDevice(phys, &dci, nullptr, &dev), "vkCreateDevice");
    volkLoadDevice(dev);
    vkGetDeviceQueue(dev, family, 0, &queue);

    VkSampleCountFlags ok = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    samples = ok & VK_SAMPLE_COUNT_4_BIT ? VK_SAMPLE_COUNT_4_BIT : ok & VK_SAMPLE_COUNT_2_BIT ? VK_SAMPLE_COUNT_2_BIT : VK_SAMPLE_COUNT_1_BIT;

    // format de l'écran
    uint32_t fc = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fc, nullptr);
    std::vector<VkSurfaceFormatKHR> fs(fc);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &fc, fs.data());
    colorFormat = fs.empty() ? VK_FORMAT_B8G8R8A8_UNORM : fs[0].format;
    for (auto& f : fs)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { colorFormat = f.format; break; }
    for (VkFormat d : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(phys, d, &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { depthFormat = d; break; }
    }

    // passe de rendu : couleur MSAA -> résolue dans l'image de l'écran
    bool ms = samples != VK_SAMPLE_COUNT_1_BIT;
    VkAttachmentDescription at[3]{};
    at[0].format = colorFormat;
    at[0].samples = samples;
    at[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at[0].storeOp = ms ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    at[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    at[0].finalLayout = ms ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    at[1].format = depthFormat;
    at[1].samples = samples;
    at[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    at[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    at[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    at[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    at[2] = at[0];
    at[2].samples = VK_SAMPLE_COUNT_1_BIT;
    at[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    at[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    at[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
        rr{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &cr;
    sp.pDepthStencilAttachment = &dr;
    sp.pResolveAttachments = ms ? &rr : nullptr;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = ms ? 3 : 2;
    rp.pAttachments = at;
    rp.subpassCount = 1;
    rp.pSubpasses = &sp;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    check(vkCreateRenderPass(dev, &rp, nullptr, &pass), "vkCreateRenderPass");

    VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cp.queueFamilyIndex = family;
    check(vkCreateCommandPool(dev, &cp, nullptr, &pool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = FRAMES;
    check(vkAllocateCommandBuffers(dev, &ca, cmd), "vkAllocateCommandBuffers");
    for (int i = 0; i < FRAMES; i++) {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(dev, &si, nullptr, &acquired[i]), "vkCreateSemaphore");
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(dev, &fi, nullptr, &fence[i]), "vkCreateFence");
    }

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dp.maxSets = 8192;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &ps;
    check(vkCreateDescriptorPool(dev, &dp, nullptr, &descPool), "vkCreateDescriptorPool");
    VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 1;
    dl.pBindings = &b;
    check(vkCreateDescriptorSetLayout(dev, &dl, nullptr, &texLayout), "vkCreateDescriptorSetLayout");
    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sc.magFilter = sc.minFilter = VK_FILTER_LINEAR;
    sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sc.maxLod = 16;
    sc.anisotropyEnable = anisotropy;
    sc.maxAnisotropy = anisotropy ? std::min(8.f, props.limits.maxSamplerAnisotropy) : 1.f;
    check(vkCreateSampler(dev, &sc, nullptr, &sampler), "vkCreateSampler");

    int w, h;
    glfwGetFramebufferSize(win, &w, &h);
    recreateSwapchain(w, h);
}

void Vk::image(uint32_t w, uint32_t h, uint32_t mips, VkFormat f, VkSampleCountFlagBits s, VkImageUsageFlags u, VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = f;
    ic.extent = {w, h, 1};
    ic.mipLevels = mips;
    ic.arrayLayers = 1;
    ic.samples = s;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = u;
    ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(dev, &ic, nullptr, &img), "vkCreateImage");
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(dev, &ai, nullptr, &mem), "vkAllocateMemory (image)");
    vkBindImageMemory(dev, img, mem, 0);
}

static VkImageView viewOf(VkDevice dev, VkImage img, VkFormat f, VkImageAspectFlags aspect, uint32_t mips = 1, const VkComponentSwizzle* swz = nullptr) {
    VkImageViewCreateInfo vc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vc.image = img;
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vc.format = f;
    if (swz) vc.components = {swz[0], swz[1], swz[2], swz[3]};
    vc.subresourceRange = {aspect, 0, mips, 0, 1};
    VkImageView v;
    check(vkCreateImageView(dev, &vc, nullptr, &v), "vkCreateImageView");
    return v;
}

void Vk::createTargets() {
    if (samples != VK_SAMPLE_COUNT_1_BIT) {
        image(extent.width, extent.height, 1, colorFormat, samples, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, msaaImg, msaaMem);
        msaaView = viewOf(dev, msaaImg, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    image(extent.width, extent.height, 1, depthFormat, samples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImg, depthMem);
    depthView = viewOf(dev, depthImg, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    for (auto v : views) {
        VkImageView at[3];
        uint32_t n;
        if (samples != VK_SAMPLE_COUNT_1_BIT) { at[0] = msaaView; at[1] = depthView; at[2] = v; n = 3; }
        else { at[0] = v; at[1] = depthView; n = 2; }
        VkFramebufferCreateInfo fc{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fc.renderPass = pass;
        fc.attachmentCount = n;
        fc.pAttachments = at;
        fc.width = extent.width;
        fc.height = extent.height;
        fc.layers = 1;
        VkFramebuffer fb;
        check(vkCreateFramebuffer(dev, &fc, nullptr, &fb), "vkCreateFramebuffer");
        framebuffers.push_back(fb);
    }
}

void Vk::destroyTargets() {
    for (auto fb : framebuffers) vkDestroyFramebuffer(dev, fb, nullptr);
    framebuffers.clear();
    for (auto v : views) vkDestroyImageView(dev, v, nullptr);
    views.clear();
    if (msaaView) { vkDestroyImageView(dev, msaaView, nullptr); vkDestroyImage(dev, msaaImg, nullptr); vkFreeMemory(dev, msaaMem, nullptr); }
    if (depthView) { vkDestroyImageView(dev, depthView, nullptr); vkDestroyImage(dev, depthImg, nullptr); vkFreeMemory(dev, depthMem, nullptr); }
    msaaView = depthView = VK_NULL_HANDLE;
    for (auto s : rendered) vkDestroySemaphore(dev, s, nullptr);
    rendered.clear();
}

void Vk::recreateSwapchain(int w, int h) {
    vkDeviceWaitIdle(dev);
    destroyTargets();
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    if (caps.currentExtent.width != UINT32_MAX) extent = caps.currentExtent;
    else extent = {uint32_t(std::max(1, w)), uint32_t(std::max(1, h))};
    extent.width = std::clamp(extent.width, std::max(1u, caps.minImageExtent.width), std::max(1u, caps.maxImageExtent.width));
    extent.height = std::clamp(extent.height, std::max(1u, caps.minImageExtent.height), std::max(1u, caps.maxImageExtent.height));
    uint32_t pmc = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmc, nullptr);
    std::vector<VkPresentModeKHR> pms(pmc);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &pmc, pms.data());
    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) for (auto m : pms) if (m == VK_PRESENT_MODE_MAILBOX_KHR) pm = m;
    VkSwapchainKHR old = swapchain;
    VkSwapchainCreateInfoKHR sc{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sc.surface = surface;
    sc.minImageCount = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount) sc.minImageCount = std::min(sc.minImageCount + 1, caps.maxImageCount);
    sc.imageFormat = colorFormat;
    sc.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sc.imageExtent = extent;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
        for (uint32_t b = 1; b <= 8; b <<= 1)
            if (caps.supportedCompositeAlpha & b) { sc.compositeAlpha = VkCompositeAlphaFlagBitsKHR(b); break; }
    sc.presentMode = pm;
    sc.clipped = VK_TRUE;
    sc.oldSwapchain = old;
    check(vkCreateSwapchainKHR(dev, &sc, nullptr, &swapchain), "vkCreateSwapchainKHR");
    if (old) vkDestroySwapchainKHR(dev, old, nullptr);
    uint32_t n = 0;
    vkGetSwapchainImagesKHR(dev, swapchain, &n, nullptr);
    images.resize(n);
    vkGetSwapchainImagesKHR(dev, swapchain, &n, images.data());
    for (auto img : images) views.push_back(viewOf(dev, img, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT));
    rendered.resize(n);
    for (auto& s : rendered) {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(dev, &si, nullptr, &s), "vkCreateSemaphore");
    }
    createTargets();
}

VkCommandBuffer Vk::beginFrame(int w, int h) {
    if (w <= 0 || h <= 0) return VK_NULL_HANDLE;
    if (uint32_t(w) != extent.width || uint32_t(h) != extent.height) recreateSwapchain(w, h);
    vkWaitForFences(dev, 1, &fence[frame], VK_TRUE, UINT64_MAX);
    VkResult r = vkAcquireNextImageKHR(dev, swapchain, UINT64_MAX, acquired[frame], VK_NULL_HANDLE, &imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(w, h); return VK_NULL_HANDLE; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) check(r, "vkAcquireNextImageKHR");
    vkResetFences(dev, 1, &fence[frame]);
    VkCommandBuffer c = cmd[frame];
    vkResetCommandBuffer(c, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(c, &bi);
    return c;
}

void Vk::beginPass(VkCommandBuffer c, const float clear[4]) {
    VkClearValue cv[3]{};
    std::memcpy(cv[0].color.float32, clear, 16);
    cv[1].depthStencil = {1.f, 0};
    cv[2] = cv[0];
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = pass;
    rb.framebuffer = framebuffers[imageIndex];
    rb.renderArea.extent = extent;
    rb.clearValueCount = samples != VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    rb.pClearValues = cv;
    vkCmdBeginRenderPass(c, &rb, VK_SUBPASS_CONTENTS_INLINE);
}

void Vk::endFrame(VkCommandBuffer c) {
    vkCmdEndRenderPass(c);
    vkEndCommandBuffer(c);
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquired[frame];
    si.pWaitDstStageMask = &wait;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &c;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &rendered[imageIndex];
    check(vkQueueSubmit(queue, 1, &si, fence[frame]), "vkQueueSubmit");
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &rendered[imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &imageIndex;
    VkResult r = vkQueuePresentKHR(queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        int w, h;
        glfwGetFramebufferSize(win_, &w, &h);
        if (w > 0 && h > 0) recreateSwapchain(w, h);
    } else if (r != VK_SUCCESS) {
        check(r, "vkQueuePresentKHR");
    }
    frame = (frame + 1) % FRAMES;
}

uint32_t Vk::memType(uint32_t bits, VkMemoryPropertyFlags want) const {
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & want) == want) return i;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) if (bits & (1u << i)) return i;
    throw Error("Vulkan : pas de mémoire compatible");
}

VkBuf Vk::buffer(VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
    VkBuf b;
    b.size = size;
    VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bc.size = std::max<VkDeviceSize>(size, 16);
    bc.usage = usage;
    check(vkCreateBuffer(dev, &bc, nullptr, &b.buf), "vkCreateBuffer");
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, b.buf, &mr);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = memType(mr.memoryTypeBits, host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                         : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(dev, &ai, nullptr, &b.mem), "vkAllocateMemory (tampon)");
    vkBindBufferMemory(dev, b.buf, b.mem, 0);
    if (host) vkMapMemory(dev, b.mem, 0, VK_WHOLE_SIZE, 0, &b.map);
    return b;
}

void Vk::free(VkBuf& b) {
    if (b.buf) vkDestroyBuffer(dev, b.buf, nullptr);
    if (b.mem) vkFreeMemory(dev, b.mem, nullptr);
    b = {};
}

void Vk::submitNow(const std::function<void(VkCommandBuffer)>& f) {
    VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkCommandBuffer c;
    check(vkAllocateCommandBuffers(dev, &ca, &c), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(c, &bi);
    f(c);
    vkEndCommandBuffer(c);
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fe;
    vkCreateFence(dev, &fi, nullptr, &fe);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &c;
    check(vkQueueSubmit(queue, 1, &si, fe), "vkQueueSubmit");
    vkWaitForFences(dev, 1, &fe, VK_TRUE, UINT64_MAX);
    vkDestroyFence(dev, fe, nullptr);
    vkFreeCommandBuffers(dev, pool, 1, &c);
}

VkBuf Vk::upload(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBuf st = buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(st.map, data, size_t(size));
    VkBuf b = buffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    submitNow([&](VkCommandBuffer c) {
        VkBufferCopy r{0, 0, size};
        vkCmdCopyBuffer(c, st.buf, b.buf, 1, &r);
    });
    free(st);
    return b;
}

bool Vk::formatOk(VkFormat f) const {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(phys, f, &fp);
    return fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
}

VkTex Vk::texture(VkFormat fmt, int w, int h, const void* data, size_t size, const VkComponentSwizzle swz[4], bool mips) {
    VkTex t;
    t.w = w;
    t.h = h;
    uint32_t levels = 1;
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(phys, fmt, &fp);
    if (mips && (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) && (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) &&
        (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        while ((std::max(w, h) >> levels) > 0) levels++;
    image(uint32_t(w), uint32_t(h), levels, fmt, VK_SAMPLE_COUNT_1_BIT,
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (levels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0), t.img, t.mem);
    VkBuf st = buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(st.map, data, size);
    submitNow([&](VkCommandBuffer c) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy r{};
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        r.imageExtent = {uint32_t(w), uint32_t(h), 1};
        vkCmdCopyBufferToImage(c, st.buf, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        int mw = w, mh = h;
        for (uint32_t i = 1; i < levels; i++) {
            VkImageMemoryBarrier m = b;
            m.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 1, 0, 1};
            m.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            m.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            m.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            m.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &m);
            VkImageBlit bl{};
            bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
            bl.srcOffsets[1] = {mw, mh, 1};
            mw = std::max(1, mw / 2);
            mh = std::max(1, mh / 2);
            bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
            bl.dstOffsets[1] = {mw, mh, 1};
            vkCmdBlitImage(c, t.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
            m.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            m.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            m.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            m.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &m);
        }
        VkImageMemoryBarrier e = b;
        e.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, levels - 1, 1, 0, 1};
        e.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        e.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        e.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        e.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &e);
    });
    free(st);
    t.view = viewOf(dev, t.img, fmt, VK_IMAGE_ASPECT_COLOR_BIT, levels, swz);
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = descPool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &texLayout;
    check(vkAllocateDescriptorSets(dev, &da, &t.set), "vkAllocateDescriptorSets");
    VkDescriptorImageInfo ii{sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr.dstSet = t.set;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &ii;
    vkUpdateDescriptorSets(dev, 1, &wr, 0, nullptr);
    return t;
}

void Vk::free(VkTex& t) {
    if (t.set) vkFreeDescriptorSets(dev, descPool, 1, &t.set);
    if (t.view) vkDestroyImageView(dev, t.view, nullptr);
    if (t.img) vkDestroyImage(dev, t.img, nullptr);
    if (t.mem) vkFreeMemory(dev, t.mem, nullptr);
    t = {};
}

VkShaderModule Vk::shader(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo sc{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    sc.codeSize = bytes;
    sc.pCode = code;
    VkShaderModule m;
    check(vkCreateShaderModule(dev, &sc, nullptr, &m), "vkCreateShaderModule");
    return m;
}

// ---------------------------------------------------------------- capture (PNG non compressé)
static void pngChunk(Bytes& out, const char* type, const Bytes& data) {
    Writer w;
    w.big = true;
    w.u32(uint32_t(data.size()));
    w.put(type, 4);
    w.put(data.data(), data.size());
    Bytes crcIn(w.out.begin() + 4, w.out.end());
    w.u32(crc32(crcIn.data(), crcIn.size()));
    out.insert(out.end(), w.out.begin(), w.out.end());
}

bool Vk::capture(const std::string& path) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) return false;
    vkDeviceWaitIdle(dev);
    uint32_t w = extent.width, h = extent.height;
    VkBuf b = buffer(VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    VkImage img = images[imageIndex];
    submitNow([&](VkCommandBuffer c) {
        VkImageMemoryBarrier m{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        m.srcQueueFamilyIndex = m.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        m.image = img;
        m.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        m.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        m.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        m.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &m);
        VkBufferImageCopy r{};
        r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        r.imageExtent = {w, h, 1};
        vkCmdCopyImageToBuffer(c, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.buf, 1, &r);
        m.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        m.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        m.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        m.dstAccessMask = 0;
        vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &m);
    });
    const uint8_t* px = static_cast<const uint8_t*>(b.map);
    bool bgr = colorFormat == VK_FORMAT_B8G8R8A8_UNORM || colorFormat == VK_FORMAT_B8G8R8A8_SRGB;
    Bytes raw;
    raw.reserve(size_t(w * 3 + 1) * h);
    for (uint32_t y = 0; y < h; y++) {
        raw.push_back(0);
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t* p = px + (size_t(y) * w + x) * 4;
            raw.push_back(bgr ? p[2] : p[0]);
            raw.push_back(p[1]);
            raw.push_back(bgr ? p[0] : p[2]);
        }
    }
    free(b);
    // zlib « stocké » (sans compression)
    Bytes z = {0x78, 0x01};
    uint32_t a = 1, bsum = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; bsum = (bsum + a) % 65521; }
    for (size_t o = 0; o < raw.size(); o += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - o);
        z.push_back(o + n >= raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n)); z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + long(o), raw.begin() + long(o + n));
    }
    uint32_t ad = (bsum << 16) | a;
    for (int k = 3; k >= 0; k--) z.push_back(uint8_t(ad >> (8 * k)));
    Bytes png = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    Writer ih;
    ih.big = true;
    ih.u32(w); ih.u32(h); ih.u8(8); ih.u8(2); ih.u8(0); ih.u8(0); ih.u8(0);
    pngChunk(png, "IHDR", ih.out);
    pngChunk(png, "IDAT", z);
    pngChunk(png, "IEND", {});
    writeFileAtomic(u8path(path), png);
    return true;
}

void Vk::destroy() {
    if (!dev) return;
    vkDeviceWaitIdle(dev);
    destroyTargets();
    if (swapchain) vkDestroySwapchainKHR(dev, swapchain, nullptr);
    vkDestroySampler(dev, sampler, nullptr);
    vkDestroyDescriptorSetLayout(dev, texLayout, nullptr);
    vkDestroyDescriptorPool(dev, descPool, nullptr);
    for (int i = 0; i < FRAMES; i++) { vkDestroySemaphore(dev, acquired[i], nullptr); vkDestroyFence(dev, fence[i], nullptr); }
    vkDestroyCommandPool(dev, pool, nullptr);
    vkDestroyRenderPass(dev, pass, nullptr);
    vkDestroyDevice(dev, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    dev = VK_NULL_HANDLE;
}

}  // namespace te
