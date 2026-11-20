cmake_minimum_required(VERSION 3.21)
project(TriforceEditeur LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
  add_compile_options(/utf-8 /MP)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(TE_GLFW_NULL_ONLY "GLFW sans fenêtre (tests sous Linux avec une surface Vulkan headless)" OFF)

# ---------------------------------------------------------------- dépendances (versions figées)
include(FetchContent)
FetchContent_Declare(vulkan_headers GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG b26454f3ccd67e704117a39295fa13935fd3804c)
FetchContent_Declare(volk GIT_REPOSITORY https://github.com/zeux/volk.git
  GIT_TAG 051977aa2412e8a1b3f5362c975bba99c4728703)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 92dcf4ce74f2e2554a98fea09be7c705c17daa5a)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG 77fe4588e6af934b4167b91312174ca3fbb0d71a)
FetchContent_Declare(zstd GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG f148665f938bbf61f95faa64db3ed7a100ec8965 SOURCE_SUBDIR build/cmake)
# (décodage des modèles .bfres.mc ; GPL-2.0 ; pas de add_subdirectory : ses sources sont compilées ci-dessous)
FetchContent_Declare(meshcodec GIT_REPOSITORY https://github.com/dt-12345/MeshCodec.git
  GIT_TAG b3600954872dbc2893a1bcc9e851c1e364f303ab SOURCE_SUBDIR aucun GIT_SUBMODULES "")

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
if(TE_GLFW_NULL_ONLY)
  set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
endif()
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT OFF CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(vulkan_headers volk glfw imgui zstd meshcodec)

file(GLOB MC_SRC ${meshcodec_SOURCE_DIR}/src/*.cpp)
add_library(meshcodec STATIC ${MC_SRC})
target_include_directories(meshcodec PUBLIC ${meshcodec_SOURCE_DIR}/src ${meshcodec_SOURCE_DIR}/src/include ${zstd_SOURCE_DIR}/lib)
target_link_libraries(meshcodec PUBLIC libzstd_static)
if(MSVC)
  target_compile_options(meshcodec PRIVATE /W0 /Zc:__cplusplus)
  target_include_directories(meshcodec SYSTEM PUBLIC ${meshcodec_SOURCE_DIR}/lib/float16)
else()
  target_compile_options(meshcodec PRIVATE -w)
endif()

# ---------------------------------------------------------------- formats du jeu (sans fenêtre : aussi pour les tests)
add_library(te_core STATIC
  src/core/util.cpp src/core/zs.cpp src/core/sarc.cpp src/core/byml.cpp src/core/json.cpp src/core/textmap.cpp
  src/core/restbl.cpp src/core/game.cpp src/core/build.cpp src/core/bfres.cpp src/core/bntx.cpp src/core/astc.cpp
  src/core/terrain.cpp src/core/models.cpp src/core/mapdoc.cpp src/core/meshcodec.cpp src/core/texsearch.cpp src/core/bake.cpp src/core/effects.cpp src/core/foliage.cpp)
target_include_directories(te_core PUBLIC src ${zstd_SOURCE_DIR}/lib)
target_link_libraries(te_core PUBLIC libzstd_static PRIVATE meshcodec)
if(WIN32)
  target_compile_definitions(te_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(te_core PUBLIC shell32 ole32)
endif()

add_executable(te_tests src/tests/tests.cpp)
target_link_libraries(te_tests PRIVATE te_core)

# ---------------------------------------------------------------- l'éditeur
set(IMGUI_SRC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
add_executable(TriforceEditeur WIN32 src/app/main.cpp src/app/vk.cpp src/app/render.cpp src/app/editor.cpp
  src/app/ui.cpp src/app/assets.cpp ${IMGUI_SRC})
if(WIN32)
  target_sources(TriforceEditeur PRIVATE src/app/app.rc)
endif()
target_include_directories(TriforceEditeur PRIVATE src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_compile_definitions(TriforceEditeur PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK GLFW_INCLUDE_NONE)
target_link_libraries(TriforceEditeur PRIVATE te_core volk glfw Vulkan::Headers)
if(NOT WIN32)
  target_link_libraries(TriforceEditeur PRIVATE pthread dl)
endif()
