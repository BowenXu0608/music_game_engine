cmake_minimum_required(VERSION 3.22.1)
project(musicgame CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Root paths ───────────────────────────────────────────────────────────────
# CMAKE_CURRENT_SOURCE_DIR = engine/src/android, so ../../.. = Music_game/
set(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(ENGINE_SRC  ${ENGINE_ROOT}/engine/src)
set(TP          ${ENGINE_ROOT}/third_party)

# ── Third-party (header-only or single-file) ─────────────────────────────────
set(GLM_INCLUDE_DIR   "${TP}/glm_extracted")
set(VMA_INCLUDE_DIR   "${TP}/vma")
set(STB_INCLUDE_DIR   "${TP}/stb")
set(IMGUI_DIR         "${TP}/imgui")

# ── Android native app glue ──────────────────────────────────────────────────
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

# ── Shared engine sources (platform-independent) ────────────────────────────
# These are the SAME source files used by the desktop build.
# We do NOT compile Engine.cpp, VulkanContext.cpp, Swapchain.cpp, or any ui/ editors.

set(SHARED_RENDERER_SOURCES
    ${ENGINE_SRC}/renderer/Renderer.cpp
    ${ENGINE_SRC}/renderer/QuadBatch.cpp
    ${ENGINE_SRC}/renderer/LineBatch.cpp
    ${ENGINE_SRC}/renderer/MeshRenderer.cpp
    ${ENGINE_SRC}/renderer/ParticleSystem.cpp
    ${ENGINE_SRC}/renderer/PostProcess.cpp
)

set(SHARED_VULKAN_SOURCES
    ${ENGINE_SRC}/renderer/vulkan/Pipeline.cpp
    ${ENGINE_SRC}/renderer/vulkan/BufferManager.cpp
    ${ENGINE_SRC}/renderer/vulkan/TextureManager.cpp
    ${ENGINE_SRC}/renderer/vulkan/DescriptorManager.cpp
    ${ENGINE_SRC}/renderer/vulkan/CommandManager.cpp
    ${ENGINE_SRC}/renderer/vulkan/SyncObjects.cpp
    ${ENGINE_SRC}/renderer/vulkan/RenderPass.cpp
)

set(SHARED_GAME_SOURCES
    ${ENGINE_SRC}/game/modes/BandoriRenderer.cpp
    ${ENGINE_SRC}/game/modes/CytusRenderer.cpp
    ${ENGINE_SRC}/game/modes/PhigrosRenderer.cpp
    ${ENGINE_SRC}/game/modes/ArcaeaRenderer.cpp
    ${ENGINE_SRC}/game/modes/LanotaRenderer.cpp
    ${ENGINE_SRC}/game/chart/ChartLoader.cpp
)

set(SHARED_GAMEPLAY_SOURCES
    ${ENGINE_SRC}/gameplay/HitDetector.cpp
    # JudgmentSystem and ScoreTracker are header-only
)

set(SHARED_CORE_SOURCES
    ${ENGINE_SRC}/core/SceneNode.cpp
)

set(SHARED_ENGINE_SOURCES
    ${ENGINE_SRC}/engine/AudioEngine.cpp
)

set(SHARED_INPUT_SOURCES
    ${ENGINE_SRC}/input/GestureRecognizer.cpp
)

# ── Android-specific sources (replace GLFW-dependent desktop files) ──────────
set(ANDROID_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/AndroidVulkanContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AndroidSwapchain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AndroidEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AndroidFileIO.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android_main.cpp
)

# ── ImGui sources (Vulkan backend only — no GLFW backend on Android) ─────────
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# ── Build shared library ─────────────────────────────────────────────────────
add_library(musicgame SHARED
    ${ANDROID_SOURCES}
    ${SHARED_RENDERER_SOURCES}
    ${SHARED_VULKAN_SOURCES}
    ${SHARED_GAME_SOURCES}
    ${SHARED_GAMEPLAY_SOURCES}
    ${SHARED_CORE_SOURCES}
    ${SHARED_ENGINE_SOURCES}
    ${SHARED_INPUT_SOURCES}
    ${IMGUI_SOURCES}
)

# ── Include directories ──────────────────────────────────────────────────────
# CRITICAL: The GLFW stub directory must come FIRST so that #include <GLFW/glfw3.h>
# picks up the stub instead of the real (non-existent on Android) GLFW header.
target_include_directories(musicgame PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs    # GLFW stub (must be first!)
    ${ENGINE_ROOT}/engine/include
    ${ENGINE_SRC}
    ${GLM_INCLUDE_DIR}
    ${VMA_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}          # for AndroidFileIO.h, AndroidEngine.h
)

# ── Compile definitions ──────────────────────────────────────────────────────
target_compile_definitions(musicgame PRIVATE
    VK_USE_PLATFORM_ANDROID_KHR
    NOMINMAX
    # VMA: restrict to Vulkan 1.0 functions available on Android API 24
    VMA_VULKAN_VERSION=1000000
)

# ── Link libraries ───────────────────────────────────────────────────────────
target_link_libraries(musicgame
    -Wl,--whole-archive
    native_app_glue
    -Wl,--no-whole-archive
    android
    log
    vulkan
)
