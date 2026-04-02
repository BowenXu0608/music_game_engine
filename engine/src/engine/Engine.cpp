#include "Engine.h"
#include "game/modes/BandoriRenderer.h"
#include "game/modes/CytusRenderer.h"
#include "game/modes/PhigrosRenderer.h"
#include "game/modes/ArcaeaRenderer.h"
#include "game/modes/LanotaRenderer.h"
#include "game/chart/ChartLoader.h"
#include "input/TouchTypes.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#ifdef _WIN32
#include <ole2.h>
#pragma comment(lib, "ole32.lib")
#endif

Engine::Engine() {
#ifdef _WIN32
    OleInitialize(nullptr);
#endif
}
Engine::~Engine() {
#ifdef _WIN32
    OleUninitialize();
#endif
}

void Engine::init(uint32_t width, uint32_t height, const std::string& title,
                  const std::string& shaderDir, bool vsync) {
    if (!glfwInit()) throw std::runtime_error("Failed to init GLFW");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) throw std::runtime_error("Failed to create window");

    // Engine* is the sole owner of the user pointer — InputManager no longer touches it
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, cursorPosCallback);
    glfwSetDropCallback(m_window, dropCallback);

#ifdef ENABLE_VALIDATION_LAYERS
    m_renderer.init(m_window, shaderDir, true, vsync);
#else
    m_renderer.init(m_window, shaderDir, false, vsync);
#endif

    m_clock.start();
    m_audio.init();
    m_imgui.init(m_window, m_renderer.context(), m_renderer.swapchainRenderPass());

    m_input.init();

    // Keyboard callback (backward compat — desktop lane input)
    m_input.setKeyCallback([this](int lane, bool pressed) {
        if (pressed && m_activeMode && m_sceneViewer.isPlaying()) {
            auto hit = m_hitDetector.checkHit(lane, m_clock.songTime());
            if (hit) dispatchHitResult(*hit, lane);
        }
    });

    // Touch/gesture callback — primary input path for mobile/tablet
    m_input.setGestureCallback([this](const GestureEvent& evt) {
        if (!m_activeMode || !m_sceneViewer.isPlaying()) return;
        double t = m_clock.songTime();

        if (dynamic_cast<ArcaeaRenderer*>(m_activeMode.get()))
            handleGestureArcaea(evt, t);
        else if (dynamic_cast<PhigrosRenderer*>(m_activeMode.get()))
            handleGesturePhigros(evt, t);
        else
            handleGestureLaneBased(evt, t);
    });

    // Create ImGui descriptor for scene texture
    VkDescriptorSet sceneTexSet = m_imgui.addTexture(
        m_renderer.sceneImageView(),
        m_renderer.postProcess().bloomSampler()
    );
    m_sceneViewer.setSceneTexture(sceneTexSet);

    // Give StartScreenEditor access to Vulkan so it can upload textures
    m_startScreenEditor.initVulkan(m_renderer.context(),
                                   m_renderer.buffers(),
                                   m_imgui,
                                   m_window);

    // Give MusicSelectionEditor access to Vulkan for cover textures
    m_musicSelectionEditor.initVulkan(m_renderer.context(),
                                      m_renderer.buffers(),
                                      m_imgui,
                                      m_window);

    // Give SongEditor access to Vulkan
    m_songEditor.initVulkan(m_renderer.context(),
                            m_renderer.buffers(),
                            m_imgui,
                            m_window);

    m_running = true;
}

void Engine::shutdown() {
    if (m_activeMode) m_activeMode->onShutdown(m_renderer);
    m_songEditor.shutdownVulkan(m_renderer.context(), m_renderer.buffers());
    m_musicSelectionEditor.shutdownVulkan(m_renderer.context(), m_renderer.buffers());
    m_startScreenEditor.shutdownVulkan(m_renderer.context(), m_renderer.buffers());
    m_audio.shutdown();
    m_imgui.shutdown();
    m_renderer.shutdown();
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void Engine::run() {
    mainLoop();
}

void Engine::runHub() {
    m_hubMode = true;
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
    double dspPos = m_audio.positionSeconds();
    if (dspPos >= 0.0)
        m_clock.setSongTime(dspPos);
    else
        m_clock.setSongTime(m_clock.songTime() + dt);

    m_input.update(m_clock.songTime()); // process hold timeouts

    m_renderer.particles().update(dt);

    if (m_activeMode && m_sceneViewer.isPlaying()) {
        m_hitDetector.update(m_clock.songTime());
        m_activeMode->onUpdate(dt, m_clock.songTime());
    }
}

void Engine::render() {
    if (!m_renderer.beginFrame()) {
        m_renderer.onResize(m_window);
        return;
    }

    if (m_activeMode && m_sceneViewer.isPlaying()) {
        m_activeMode->onRender(m_renderer);
    }

    m_renderer.endFrame();

    m_imgui.beginFrame();

    switch (m_currentLayer) {
        case EditorLayer::ProjectHub:
            m_hub.render(this);
            break;
        case EditorLayer::StartScreen:
            m_startScreenEditor.render(this);
            break;
        case EditorLayer::MusicSelection:
            m_musicSelectionEditor.render(this);
            break;
        case EditorLayer::SongEditor:
            m_songEditor.render(this);
            break;
        case EditorLayer::GamePlay:
            m_sceneViewer.render(*this);
            break;
        default:
            break;
    }

    m_imgui.endFrame();
    m_imgui.render(m_renderer.currentCmd());

    m_renderer.finishFrame();
}

void Engine::loadAudio(const std::string& path) {
    m_audio.load(path);
    m_audio.play();
    m_clock.setSongTime(0.0);
}

void Engine::setMode(GameModeRenderer* renderer, const ChartData& chart) {
    if (m_activeMode) m_activeMode->onShutdown(m_renderer);

    m_activeMode.reset(renderer);
    m_activeMode->onInit(m_renderer, chart);
    m_hitDetector.init(chart);
    m_judgment.reset();
    m_score.reset();
    m_activeTouches.clear();
}

// ── Gesture handlers ─────────────────────────────────────────────────────────

void Engine::dispatchHitResult(const HitResult& hit, int lane) {
    auto judgment = m_judgment.judge(hit.timingDelta);
    m_judgment.recordJudgment(judgment);
    m_score.onJudgment(judgment);

    if (auto* bandori = dynamic_cast<BandoriRenderer*>(m_activeMode.get()))
        bandori->showJudgment(lane >= 0 ? lane : 0, judgment);

    std::cout << "Hit - ";
    switch (judgment) {
        case Judgment::Perfect: std::cout << "Perfect"; break;
        case Judgment::Good:    std::cout << "Good";    break;
        case Judgment::Bad:     std::cout << "Bad";     break;
        case Judgment::Miss:    std::cout << "Miss";    break;
    }
    std::cout << " | Score: " << m_score.getScore()
              << " | Combo: " << m_score.getCombo() << "\n";
}

void Engine::handleGestureLaneBased(const GestureEvent& evt, double songTime) {
    int screenW = static_cast<int>(m_renderer.width());
    int lane = static_cast<int>(evt.pos.x / static_cast<float>(screenW) * 7.0f);
    lane = std::clamp(lane, 0, 6);

    switch (evt.type) {
        case GestureType::Tap: {
            auto hit = m_hitDetector.checkHit(lane, songTime);
            if (hit) dispatchHitResult(*hit, lane);
            break;
        }
        case GestureType::Flick: {
            auto hit = m_hitDetector.checkHit(lane, songTime);
            if (hit && hit->noteType == NoteType::Flick) {
                float speed = glm::length(evt.velocity);
                float dirAcc = speed > 0.f ? std::abs(evt.velocity.x) / speed : 0.f;
                auto judgment = m_judgment.judgeFlick(hit->timingDelta, dirAcc);
                m_judgment.recordJudgment(judgment);
                m_score.onJudgment(judgment);
            }
            break;
        }
        case GestureType::HoldBegin: {
            auto noteId = m_hitDetector.beginHold(lane, songTime);
            if (noteId) m_activeTouches[evt.touchId] = *noteId;
            break;
        }
        case GestureType::HoldEnd: {
            auto it = m_activeTouches.find(evt.touchId);
            if (it != m_activeTouches.end()) {
                auto hit = m_hitDetector.endHold(it->second, songTime);
                if (hit) dispatchHitResult(*hit, lane);
                m_activeTouches.erase(it);
            }
            break;
        }
        default: break;
    }
}

void Engine::handleGestureArcaea(const GestureEvent& evt, double songTime) {
    glm::vec2 screenSize{static_cast<float>(m_renderer.width()),
                         static_cast<float>(m_renderer.height())};

    switch (evt.type) {
        case GestureType::Tap: {
            auto hit = m_hitDetector.checkHitPosition(evt.pos, screenSize, songTime);
            if (hit) dispatchHitResult(*hit);
            break;
        }
        case GestureType::HoldBegin: {
            auto noteId = m_hitDetector.beginHoldPosition(evt.pos, screenSize, songTime);
            if (noteId) m_activeTouches[evt.touchId] = *noteId;
            break;
        }
        case GestureType::SlideMove: {
            auto it = m_activeTouches.find(evt.touchId);
            if (it != m_activeTouches.end())
                m_hitDetector.updateSlide(it->second, evt.pos, songTime);
            break;
        }
        case GestureType::SlideEnd:
        case GestureType::HoldEnd: {
            auto it = m_activeTouches.find(evt.touchId);
            if (it != m_activeTouches.end()) {
                float accuracy = m_hitDetector.getSlideAccuracy(it->second);
                auto hit = m_hitDetector.endHold(it->second, songTime);
                if (hit) {
                    auto judgment = m_judgment.judgeArc(accuracy, 1.0f);
                    m_judgment.recordJudgment(judgment);
                    m_score.onJudgment(judgment);
                }
                m_activeTouches.erase(it);
            }
            break;
        }
        default: break;
    }
}

void Engine::handleGesturePhigros(const GestureEvent& evt, double songTime) {
    if (evt.type != GestureType::Tap && evt.type != GestureType::HoldBegin) return;

    auto* phigros = dynamic_cast<PhigrosRenderer*>(m_activeMode.get());
    if (!phigros) return;

    auto lines = phigros->getActiveLines();
    for (const auto& line : lines) {
        auto hit = m_hitDetector.checkHitPhigros(evt.pos, line.origin, line.rotation, songTime);
        if (hit) {
            dispatchHitResult(*hit);
            break;
        }
    }
}

// ── Static GLFW callbacks ─────────────────────────────────────────────────────

void Engine::framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    engine->m_framebufferResized = true;
}

void Engine::keyCallback(GLFWwindow* window, int key, int, int action, int) {
    auto* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    engine->m_input.onKey(key, action);
}

void Engine::mouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    auto* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    TouchPhase phase = (action == GLFW_PRESS) ? TouchPhase::Began : TouchPhase::Ended;
    engine->m_input.injectTouch(-1, phase, {static_cast<float>(x), static_cast<float>(y)},
                                glfwGetTime());
}

void Engine::cursorPosCallback(GLFWwindow* window, double x, double y) {
    auto* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    engine->m_input.onMouseMove(x, y, glfwGetTime());
}

void Engine::dropCallback(GLFWwindow* window, int count, const char** paths) {
    auto* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
    std::cout << "[drop] layer=" << (int)engine->m_currentLayer
              << " project='" << engine->m_startScreenEditor.projectPath() << "'\n";
    if (engine->m_currentLayer != EditorLayer::StartScreen) return;
    if (engine->m_startScreenEditor.projectPath().empty()) return;

    std::vector<std::string> srcPaths;
    srcPaths.reserve(count);
    for (int i = 0; i < count; ++i)
        srcPaths.emplace_back(paths[i]);

    engine->m_startScreenEditor.importFiles(srcPaths);
}
