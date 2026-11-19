// Triforce — éditeur de cartes (C++ / Vulkan). Point d'entrée : fenêtre GLFW, Vulkan, Dear ImGui, boucle principale.
#include <volk.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "app/editor.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace te {
extern const uint8_t FONT_DROIDSANS[];
extern const size_t FONT_DROIDSANS_SIZE;
void drawUI(Editor& ed, bool& quit, bool closeRequested);
void uiShowTextures();
void uiShowModels();
}

using namespace te;

static void fatal(const std::string& msg) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, w.data(), n);
    MessageBoxW(nullptr, w.c_str(), L"Triforce — éditeur de cartes", MB_ICONERROR);
#else
    std::fprintf(stderr, "%s\n", msg.c_str());
#endif
}

static void style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    s.WindowRounding = 0;
    s.FrameRounding = 4;
    s.GrabRounding = 4;
    s.ChildRounding = 4;
    s.PopupRounding = 6;
    s.FramePadding = {8, 5};
    s.ItemSpacing = {8, 6};
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = {0.09f, 0.11f, 0.14f, 1};
    c[ImGuiCol_ChildBg] = {0.07f, 0.09f, 0.11f, 1};
    c[ImGuiCol_PopupBg] = {0.10f, 0.12f, 0.15f, 0.98f};
    c[ImGuiCol_FrameBg] = {0.14f, 0.17f, 0.21f, 1};
    c[ImGuiCol_FrameBgHovered] = {0.19f, 0.23f, 0.28f, 1};
    c[ImGuiCol_Button] = {0.18f, 0.22f, 0.27f, 1};
    c[ImGuiCol_ButtonHovered] = {0.30f, 0.27f, 0.18f, 1};
    c[ImGuiCol_ButtonActive] = {0.45f, 0.36f, 0.16f, 1};
    c[ImGuiCol_Header] = {0.33f, 0.28f, 0.15f, 1};
    c[ImGuiCol_HeaderHovered] = {0.40f, 0.33f, 0.17f, 1};
    c[ImGuiCol_CheckMark] = {0.95f, 0.75f, 0.35f, 1};
    c[ImGuiCol_Border] = {0.22f, 0.26f, 0.31f, 1};
}

int run(int argc, char** argv) {
    bool headless = false;
    std::string script, romfs;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--headless") headless = true;
        else if (a == "--script" && i + 1 < argc) script = argv[++i];
        else if (a == "--romfs" && i + 1 < argc) romfs = argv[++i];
    }
    if (headless) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (volkInitialize() != VK_SUCCESS) { fatal("Vulkan est introuvable : mets à jour le pilote de la carte graphique."); return 1; }
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (!glfwInit()) { fatal("Impossible d'initialiser la fenêtre (GLFW)."); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* win = glfwCreateWindow(1600, 900, "Triforce — éditeur de cartes", nullptr, nullptr);
    if (!win) { fatal("Impossible de créer la fenêtre."); return 1; }
    if (!headless) glfwMaximizeWindow(win);

    Vk vk;
    try {
        vk.init(win, headless);
    } catch (const std::exception& e) {
        fatal(std::string("Vulkan : ") + e.what());
        return 1;
    }
    Renderer rend(vk);
    rend.init();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    float scale = 1;
    if (!headless) { float sx, sy; glfwGetWindowContentScale(win, &sx, &sy); scale = std::max(1.f, sx); }
    ImFontConfig fc;
    fc.FontDataOwnedByAtlas = false;
    ImFont* font = nullptr;
#ifdef _WIN32
    for (const char* p : {"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arial.ttf"})
        if (!font && fs::exists(p)) font = io.Fonts->AddFontFromFileTTF(p, 16.f * scale);
#endif
    if (!font) font = io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(FONT_DROIDSANS), int(FONT_DROIDSANS_SIZE), 16.f * scale, &fc);
    style();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui_ImplGlfw_InitForVulkan(win, true);
    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_1;
    ii.Instance = vk.instance;
    ii.PhysicalDevice = vk.phys;
    ii.Device = vk.dev;
    ii.QueueFamily = vk.family;
    ii.Queue = vk.queue;
    ii.DescriptorPoolSize = 64;
    ii.MinImageCount = 2;
    ii.ImageCount = uint32_t(vk.images.size());
    ii.PipelineInfoMain.RenderPass = vk.pass;
    ii.PipelineInfoMain.MSAASamples = vk.samples;
    ImGui_ImplVulkan_Init(&ii);

    int code = 0;
    {
        Editor ed(vk, rend);
        if (!romfs.empty()) ed.folders.romfs = u8path(romfs);
        ed.openGame();
        // script de test : une commande par ligne (frames N, open carte, select N, rotate, capture fichier, quit…)
        std::vector<std::string> cmds;
        if (!script.empty()) {
            std::ifstream f(u8path(script));
            for (std::string l; std::getline(f, l);) if (!l.empty() && l[0] != '#') cmds.push_back(l);
        }
        size_t ci = 0;
        int waitFrames = 0;
        bool quit = false;
        while (!quit) {
            glfwPollEvents();
            bool close = glfwWindowShouldClose(win);
            if (close) glfwSetWindowShouldClose(win, GLFW_FALSE);
            ed.jobs.pump(8);
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            drawUI(ed, quit, close);
            ImGui::Render();
            int w, h;
            glfwGetFramebufferSize(win, &w, &h);
            VkCommandBuffer c = vk.beginFrame(w, h);
            if (c) {
                float clear[4] = {0.10f, 0.13f, 0.17f, 1};
                vk.beginPass(c, clear);
                ed.render(c);
                VkViewport v{0, 0, float(vk.extent.width), float(vk.extent.height), 0, 1};
                vkCmdSetViewport(c, 0, 1, &v);
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), c);
                vk.endFrame(c);
            } else if (!headless) {
                glfwWaitEventsTimeout(0.05);
            }
            // -- script
            if (waitFrames > 0) { waitFrames--; continue; }
            while (ci < cmds.size() && waitFrames == 0) {
                auto parts = split(cmds[ci++], ' ');
                std::string cmd = parts[0], arg = parts.size() > 1 ? cmds[ci - 1].substr(cmd.size() + 1) : "";
                std::printf("> %s\n", cmds[ci - 1].c_str());
                if (cmd == "frames") waitFrames = std::atoi(arg.c_str());
                else if (cmd == "open") ed.openMap(arg);
                else if (cmd == "wait") { while (ed.jobs.pending()) { ed.jobs.pump(50); } waitFrames = 2; }
                else if (cmd == "select") { auto all = ed.allRecs(); size_t k = size_t(std::atoi(arg.c_str())); if (k < all.size()) { ed.select(all[k].get(), false); ed.focus({all[k].get()}); } }
                else if (cmd == "rotate") ed.rotateSel(1, std::atof(arg.c_str()) * 3.14159265358979323846 / 180);
                else if (cmd == "move") ed.moveSel({std::atof(arg.c_str()), 0, 0});
                else if (cmd == "top") ed.cam.top = !ed.cam.top;
                else if (cmd == "textures") uiShowTextures();
                else if (cmd == "modeles") uiShowModels();
                else if (cmd == "tilt") ed.tilt = !ed.tilt;
                else if (cmd == "terrain") ed.setTerrain(arg);
                else if (cmd == "orbit") { auto p = split(arg, ' '); ed.cam.yaw = std::atof(p[0].c_str()); ed.cam.pitch = std::atof(p[1].c_str()); ed.cam.dist = std::atof(p[2].c_str()); }
                else if (cmd == "save") ed.save();
                else if (cmd == "undo") ed.undo();
                else if (cmd == "duplicate") ed.duplicateSel();
                else if (cmd == "delete") ed.deleteSel();
                else if (cmd == "build") { ed.buildMod(); while (ed.building) glfwWaitEventsTimeout(0.05); for (auto& l : ed.logCopy()) std::printf("  %s\n", l.c_str()); }
                else if (cmd == "capture") { if (!vk.capture(arg)) std::printf("  (capture impossible)\n"); }
                else if (cmd == "print") {
                    std::printf("  carte=%s modifiée=%d sélection=%zu modèles=%zu textures=%zu tuiles=%zu\n", ed.doc.path.c_str(), ed.doc.dirty ? 1 : 0, ed.sel.size(),
                                ed.modelSlots.size(), ed.texSlots.size(), ed.tiles.size());
                    if (auto r = ed.primary()) { V3 p = ed.posOf(*r), e = ed.rotOf(*r); std::printf("  %s pos %.3f %.3f %.3f rot %.4f %.4f %.4f\n", nameOf(r->value).c_str(), p.x, p.y, p.z, e.x, e.y, e.z); }
                    for (auto& [k, s] : ed.modelSlots) std::printf("  modèle %s : %d %s\n", k.c_str(), int(s.st), s.why.c_str());
                    for (auto& [k, s] : ed.texSlots) std::printf("  texture %s : %d %s %s\n", k.c_str(), int(s.st), s.format.c_str(), s.why.c_str());
                    std::printf("  toast : %s\n", ed.toast.c_str());
                } else if (cmd == "debug") {
                    V3 e = ed.cam.eye();
                    std::printf("  œil %.1f %.1f %.1f cible %.1f %.1f %.1f vue %d %d %d %d\n", e.x, e.y, e.z, ed.cam.target.x, ed.cam.target.y, ed.cam.target.z, ed.vpX, ed.vpY, ed.vpW, ed.vpH);
                    for (auto& r : ed.allRecs()) {
                        float sx = -1, sy = -1;
                        bool ok = posKey(r->value) && ed.project(ed.posOf(*r), sx, sy);
                        V3 p = ed.posOf(*r);
                        std::printf("  %s pos %.1f %.1f %.1f écran %d %.0f %.0f visible %d\n", nameOf(r->value).c_str(), p.x, p.y, p.z, ok, sx, sy, ed.visible(*r));
                    }
                } else if (cmd == "quit") quit = true;
            }
        }
        vkDeviceWaitIdle(vk.dev);
        ed.freeAssets();
        ed.saveSettings();
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    rend.destroy();
    vk.destroy();
    glfwDestroyWindow(win);
    glfwTerminate();
    return code;
}

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s.data(), n, nullptr, nullptr);
        args.push_back(s);
    }
    LocalFree(wargv);
    std::vector<char*> av;
    for (auto& s : args) av.push_back(s.data());
    try {
        return run(int(av.size()), av.data());
    } catch (const std::exception& e) {
        fatal(std::string("Erreur : ") + e.what());
        return 1;
    }
}
#else
int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        fatal(std::string("Erreur : ") + e.what());
        return 1;
    }
}
#endif
