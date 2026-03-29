#include "Engine.h"
#include "game/modes/BandoriRenderer.h"
#include "game/modes/CytusRenderer.h"
#include "game/modes/PhigrosRenderer.h"
#include "game/modes/ArcaeaRenderer.h"
#include "game/modes/LanotaRenderer.h"
#include "game/chart/ChartLoader.h"
#include <stdexcept>
#include <iostream>

void Engine::init(uint32_t width, uint32_t height, const std::string& title,
                  const std::string& shaderDir, bool vsync) {
    if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) throw std::runtime_error("Failed to create window");

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    glfwSetKeyCallback(m_window, keyCallback);

#ifdef ENABLE_VALIDATION_LAYERS
    m_renderer.init(m_window, shaderDir, true, vsync);
#else
    m_renderer.init(m_window, shaderDir, false, vsync);
#endif

    m_clock.start();
    m_audio.init();
    m_imgui.init(m_window, m_renderer.context(), m_renderer.swapchainRenderPass());

    // Create ImGui descriptor for scene texture
    VkDescriptorSet sceneTexSet = m_imgui.addTexture(
        m_renderer.sceneImageView(),
        m_renderer.postProcess().bloomSampler()
    );
    m_sceneViewer.setSceneTexture(sceneTexSet);

    m_running = true;
}

void Engine::shutdown() {
    if (m_activeMode) m_activeMode->onShutdown(m_renderer);
    m_audio.shutdown();
    m_imgui.shutdown();
    m_renderer.shutdown();
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void Engine::run() {
    mainLoop();
}

void Engine::mainLoop() {
    while (m_running && !glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        if (m_framebufferResized) {
            m_framebufferResized = false;
            m_renderer.onResize(m_window);
            if (m_activeMode)
                m_activeMode->onResize(m_renderer.width(), m_renderer.height());
        }

        float dt = m_clock.tick();
        update(dt);
        render();
    }
    vkDeviceWaitIdle(m_renderer.context().device());
}

void Engine::update(float dt) {
    // Use DSP clock if audio is playing, otherwise fall back to wall clock
    double dspPos = m_audio.positionSeconds();
    if (dspPos >= 0.0)
        m_clock.setSongTime(dspPos);
    else
        m_clock.setSongTime(m_clock.songTime() + dt);

    m_renderer.particles().update(dt);

    // Only update game if playing
    if (m_activeMode && m_sceneViewer.isPlaying())
        m_activeMode->onUpdate(dt, m_clock.songTime());
}

void Engine::render() {
    if (!m_renderer.beginFrame()) {
        m_renderer.onResize(m_window);
        return;
    }

    // Only render game if playing
    if (m_activeMode && m_sceneViewer.isPlaying()) {
        m_activeMode->onRender(m_renderer);
    }
    // If stopped, scene buffer is already cleared by beginFrame

    m_renderer.endFrame();

    // Render ImGui UI after composite (no bloom on UI)
    m_imgui.beginFrame();
    m_sceneViewer.render(*this);
    m_imgui.endFrame();
    m_imgui.render(m_renderer.currentCmd());

    m_renderer.finishFrame();
}

void Engine::loadAudio(const std::string& path) {
    m_audio.load(path);
    m_audio.play();
    m_clock.setSongTime(0.0);
}

void Engine::loadChart(GameMode mode, const std::string& chartPath) {
    ChartData chart = ChartLoader::load(chartPath);
    setMode(mode, chart);
}

void Engine::setMode(GameMode mode, const ChartData& chart) {
    if (m_activeMode) m_activeMode->onShutdown(m_renderer);

    switch (mode) {
        case GameMode::Bandori: m_activeMode = std::make_unique<BandoriRenderer>(); break;
        case GameMode::Cytus:   m_activeMode = std::make_unique<CytusRenderer>();   break;
        case GameMode::Phigros: m_activeMode = std::make_unique<PhigrosRenderer>(); break;
        case GameMode::Arcaea:  m_activeMode = std::make_unique<ArcaeaRenderer>();  break;
        case GameMode::Lanota:  m_activeMode = std::make_unique<LanotaRenderer>();  break;
    }

    m_activeMode->onInit(m_renderer, chart);
}

void Engine::framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    engine->m_framebufferResized = true;
}

void Engine::keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}
