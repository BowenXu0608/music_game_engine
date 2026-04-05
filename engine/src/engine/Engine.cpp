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

namespace {
std::string chartPathForDifficulty(const SongInfo& song, Difficulty diff) {
    switch (diff) {
        case Difficulty::Easy:   return song.chartEasy;
        case Difficulty::Medium: return song.chartMedium;
        case Difficulty::Hard:   return song.chartHard;
    }
    return song.chartHard;
}
} // anonymous namespace

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
    clearPreviewMode();
    clearBackgroundTexture();
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
    // During gameplay lead-in, advance clock manually until audio starts
    if (m_currentLayer == EditorLayer::GamePlay && !m_audioStarted && !m_gameplayPaused) {
        double songT = m_clock.songTime() + dt;
        m_clock.setSongTime(songT);
        if (songT >= 0.0) {
            // Lead-in finished — start audio now
            if (!m_pendingAudioPath.empty()) {
                loadAudio(m_pendingAudioPath);
                m_pendingAudioPath.clear();
            }
            m_audioStarted = true;
        }
    } else {
        double dspPos = m_audio.positionSeconds();
        if (dspPos >= 0.0)
            m_clock.setSongTime(dspPos);
        else
            m_clock.setSongTime(m_clock.songTime() + dt);
    }

    m_input.update(m_clock.songTime()); // process hold timeouts

    m_renderer.particles().update(dt);

    // Test mode transitions
    if (m_testTransitioning) {
        float dur = m_startScreenEditor.transitionDuration();
        m_testTransProgress += dt / std::max(0.1f, dur);
        if (m_testTransProgress >= 1.f) {
            m_testTransitioning = false;
            m_testTransProgress = 0.f;
            switchLayer(m_testTransTo);
        }
    }

    if (m_activeMode && m_sceneViewer.isPlaying()) {
        auto missed = m_hitDetector.update(m_clock.songTime());
        for (auto& m : missed) {
            m_judgment.recordJudgment(Judgment::Miss);
            m_score.onJudgment(Judgment::Miss);
            if (m_activeMode && m.lane >= 0)
                m_activeMode->showJudgment(m.lane, Judgment::Miss);
        }
        m_activeMode->onUpdate(dt, m_clock.songTime());
    }

    // Update preview mode at editor scene time
    if (m_previewMode) {
        m_previewMode->onUpdate(dt, m_clock.songTime());
    }

    // Song-end detection — only trigger if audio was playing and has stopped,
    // or if no audio but all notes have passed
    if (m_currentLayer == EditorLayer::GamePlay && !m_gameplayPaused && !m_showResults) {
        double songT = m_clock.songTime();
        if (songT > 2.0) {
            bool audioEnded = !m_audio.isPlaying();
            if (audioEnded) {
                m_showResults = true;
                m_sceneViewer.setPlaying(false);
            }
        }
    }
}

void Engine::render() {
    if (!m_renderer.beginFrame()) {
        m_renderer.onResize(m_window);
        return;
    }

    // Draw background image into scene framebuffer (behind game elements)
    if (m_bgLoaded && (m_activeMode || m_previewMode)) {
        float sw = static_cast<float>(m_renderer.width());
        float sh = static_cast<float>(m_renderer.height());
        Camera orthoCam = Camera::makeOrtho(0.f, sw, sh, 0.f);
        m_renderer.setCamera(orthoCam);
        m_renderer.quads().drawQuad(
            {sw * 0.5f, sh * 0.5f}, {sw, sh}, 0.f,
            {1.f, 1.f, 1.f, 1.f}, {0.f, 0.f, 1.f, 1.f},
            m_bgTexture.view, m_bgTexture.sampler,
            m_renderer.context(), m_renderer.descriptors());
    }

    if (m_activeMode && m_sceneViewer.isPlaying()) {
        m_activeMode->onRender(m_renderer);
    } else if (m_previewMode) {
        m_previewMode->onRender(m_renderer);
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
            renderGameplayHUD();
            break;
        default:
            break;
    }

    m_imgui.endFrame();
    m_imgui.render(m_renderer.currentCmd());

    m_renderer.finishFrame();
}

void Engine::setupPreviewMode(const GameModeConfig& config, const ChartData& chart,
                              const std::string& projectPath) {
    if (m_previewMode) {
        m_previewMode->onShutdown(m_renderer);
        m_previewMode.reset();
    }
    m_previewMode = createRenderer(config);
    m_previewMode->onInit(m_renderer, chart, &config);
    m_previewMode->onResize(m_renderer.width(), m_renderer.height());

    if (!projectPath.empty() && !config.backgroundImage.empty()) {
        loadBackgroundTexture(projectPath, config.backgroundImage);
    }
}

void Engine::renderPreviewFrame(double songTime) {
    if (!m_previewMode) return;
    m_previewMode->onUpdate(0.f, songTime);
    m_previewMode->onRender(m_renderer);
}

void Engine::clearPreviewMode() {
    if (m_previewMode) {
        m_previewMode->onShutdown(m_renderer);
        m_previewMode.reset();
    }
}

void Engine::loadBackgroundTexture(const std::string& projectPath,
                                   const std::string& bgImage) {
    clearBackgroundTexture();
    if (bgImage.empty()) return;
    std::string fullPath = projectPath + "/" + bgImage;
    try {
        m_bgTexture = m_renderer.textures().loadFromFile(
            m_renderer.context(), m_renderer.buffers(), fullPath);
        m_bgLoaded = true;
        std::cout << "[Engine] Loaded background: " << bgImage << "\n";
    } catch (...) {
        std::cout << "[Engine] Failed to load background: " << bgImage << "\n";
    }
}

void Engine::clearBackgroundTexture() {
    if (m_bgLoaded) {
        vkDeviceWaitIdle(m_renderer.context().device());
        vkDestroySampler(m_renderer.context().device(), m_bgTexture.sampler, nullptr);
        vkDestroyImageView(m_renderer.context().device(), m_bgTexture.view, nullptr);
        vmaDestroyImage(m_renderer.buffers().allocator(), m_bgTexture.image, m_bgTexture.allocation);
        m_bgTexture = {};
        m_bgLoaded = false;
    }
}

void Engine::loadAudio(const std::string& path) {
    std::cout << "[Engine] Loading audio: " << path << "\n";
    if (!m_audio.load(path)) {
        std::cout << "[Engine] FAILED to load audio: " << path << "\n";
        return;
    }
    m_audio.play();
    std::cout << "[Engine] Audio playing\n";
}

void Engine::setMode(GameModeRenderer* renderer, const ChartData& chart,
                     const GameModeConfig* config) {
    if (m_activeMode) m_activeMode->onShutdown(m_renderer);

    m_activeMode.reset(renderer);
    m_activeMode->onInit(m_renderer, chart, config);
    m_hitDetector.init(chart);
    m_judgment.reset();
    m_score.reset();
    m_activeTouches.clear();
}

// ── Game mode factory ────────────────────────────────────────────────────────

std::unique_ptr<GameModeRenderer> Engine::createRenderer(const GameModeConfig& config) {
    switch (config.type) {
        case GameModeType::DropNotes:
            if (config.dimension == DropDimension::ThreeD)
                return std::make_unique<ArcaeaRenderer>();
            return std::make_unique<BandoriRenderer>();
        case GameModeType::Circle:
            if (config.dimension == DropDimension::ThreeD)
                return std::make_unique<LanotaRenderer>();
            return std::make_unique<CytusRenderer>();
        case GameModeType::ScanLine:
            return std::make_unique<PhigrosRenderer>();
    }
    return std::make_unique<BandoriRenderer>();
}

// ── Gameplay lifecycle ───────────────────────────────────────────────────────

void Engine::launchGameplay(const SongInfo& song, Difficulty difficulty,
                            const std::string& projectPath) {
    std::string chartRel = chartPathForDifficulty(song, difficulty);
    if (chartRel.empty()) {
        std::cout << "[Engine] No chart for selected difficulty\n";
        return;
    }

    std::string chartPath = projectPath + "/" + chartRel;
    std::string audioPath = projectPath + "/" + song.audioFile;

    ChartData chart = ChartLoader::load(chartPath);
    auto renderer = createRenderer(song.gameMode);
    setMode(renderer.release(), chart, &song.gameMode);

    m_audio.stop();

    m_gameplayConfig = song.gameMode;
    m_preGameplayLayer = m_currentLayer;
    m_gameplayPaused = false;
    m_showResults = false;
    m_sceneViewer.setPlaying(true);
    loadBackgroundTexture(projectPath, song.gameMode.backgroundImage);

    // Start the game clock with a lead-in period so notes scroll in visually
    // before the first one reaches the hit zone. Audio offset shifts the sync.
    float leadIn = 2.0f;  // seconds of visual lead-in before audio starts
    m_clock.setSongTime(-leadIn - song.gameMode.audioOffset);
    m_gameplayLeadIn = leadIn + song.gameMode.audioOffset;
    m_audioStarted = false;
    m_pendingAudioPath = audioPath;

    switchLayer(EditorLayer::GamePlay);

    std::cout << "[Engine] Gameplay started: " << song.name
              << " (offset=" << song.gameMode.audioOffset << "s, leadIn=" << leadIn << "s)\n";
}

void Engine::launchGameplayDirect(const SongInfo& song, const ChartData& chart,
                                  const std::string& projectPath) {
    std::string audioPath = projectPath + "/" + song.audioFile;

    auto renderer = createRenderer(song.gameMode);
    setMode(renderer.release(), chart, &song.gameMode);

    m_audio.stop();

    m_gameplayConfig = song.gameMode;
    m_preGameplayLayer = m_currentLayer;
    m_gameplayPaused = false;
    m_showResults = false;
    m_sceneViewer.setPlaying(true);
    loadBackgroundTexture(projectPath, song.gameMode.backgroundImage);

    float leadIn = 2.0f;
    m_clock.setSongTime(-leadIn - song.gameMode.audioOffset);
    m_gameplayLeadIn = leadIn + song.gameMode.audioOffset;
    m_audioStarted = false;
    m_pendingAudioPath = song.audioFile.empty() ? "" : audioPath;

    switchLayer(EditorLayer::GamePlay);

    std::cout << "[Engine] Gameplay started (direct): " << song.name << "\n";
}

void Engine::exitGameplay() {
    std::cout << "[Engine] exitGameplay called, testMode=" << m_testMode << "\n";
    m_audio.stop();
    m_gameplayPaused = false;
    m_showResults = false;
    m_sceneViewer.setPlaying(false);
    if (m_activeMode) {
        m_activeMode->onShutdown(m_renderer);
        m_activeMode.reset();
    }
    m_activeTouches.clear();
    clearBackgroundTexture();
    if (m_testMode) {
        exitTestMode();
    } else {
        switchLayer(m_preGameplayLayer);
    }
}

void Engine::enterTestMode(EditorLayer returnLayer) {
    m_testMode = true;
    m_testReturnLayer = returnLayer;
    m_testTransitioning = false;
    switchLayer(EditorLayer::StartScreen);
}

void Engine::exitTestMode() {
    std::cout << "[Engine] exitTestMode\n";
    m_testMode = false;
    m_testTransitioning = false;
    m_audio.stop();
    m_gameplayPaused = false;
    m_showResults = false;
    m_sceneViewer.setPlaying(false);
    if (m_activeMode) {
        m_activeMode->onShutdown(m_renderer);
        m_activeMode.reset();
    }
    m_activeTouches.clear();
    clearBackgroundTexture();

    // If we're a standalone test game process (no editor to return to),
    // close the window. Otherwise return to the editor layer.
    if (m_testReturnLayer == EditorLayer::StartScreen) {
        // Standalone test process — close window
        if (m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    } else {
        m_currentLayer = m_testReturnLayer;
    }
}

void Engine::testTransitionTo(EditorLayer target) {
    m_testTransFrom = m_currentLayer;
    m_testTransTo = target;
    m_testTransitioning = true;
    m_testTransProgress = 0.f;
}

void Engine::togglePause() {
    m_gameplayPaused = !m_gameplayPaused;
    if (m_gameplayPaused) {
        m_audio.pause();
        m_clock.pause();
        m_sceneViewer.setPlaying(false);
    } else {
        m_audio.resume();
        m_clock.resume();
        m_sceneViewer.setPlaying(true);
    }
}

// ── Gameplay HUD ─────────────────────────────────────────────────────────────

void Engine::renderGameplayHUD() {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;

    // Full-screen scene texture (rendered offscreen by Vulkan, composited here)
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySz);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##gameplay_scene", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs);
    if (m_sceneViewer.sceneTexture() != VK_NULL_HANDLE) {
        ImGui::Image((ImTextureID)m_sceneViewer.sceneTexture(), displaySz);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    // HUD overlay — use foreground draw list so it's always on top
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float sw = displaySz.x, sh = displaySz.y;

    // HUD text rendering helper (uses HudTextConfig)
    auto drawHud = [&](const HudTextConfig& h, const char* text) {
        if (!text || text[0] == '\0') return;
        float fx = sw * h.pos[0];
        float fy = sh * h.pos[1];
        float fs = h.fontSize * h.scale;
        ImU32 col = IM_COL32((int)(h.color[0]*255), (int)(h.color[1]*255),
                             (int)(h.color[2]*255), (int)(h.color[3]*255));
        ImFont* font = ImGui::GetFont();
        ImVec2 textSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
        ImVec2 textPos(fx - textSz.x * 0.5f, fy - textSz.y * 0.5f);

        if (h.glow) {
            ImU32 gc = IM_COL32((int)(h.glowColor[0]*255), (int)(h.glowColor[1]*255),
                                (int)(h.glowColor[2]*255), (int)(h.glowColor[3]*255));
            float r = h.glowRadius;
            float offsets[][2] = {{-r,0},{r,0},{0,-r},{0,r},{-r*0.7f,-r*0.7f},
                                  {r*0.7f,-r*0.7f},{-r*0.7f,r*0.7f},{r*0.7f,r*0.7f}};
            for (auto& o : offsets)
                dl->AddText(font, fs, ImVec2(textPos.x+o[0], textPos.y+o[1]), gc, text);
        }
        if (h.bold)
            dl->AddText(font, fs, ImVec2(textPos.x + 1.f, textPos.y), col, text);
        dl->AddText(font, fs, textPos, col, text);
    };

    // Score — always visible with background panel
    {
        char scoreBuf[32];
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", m_score.getScore());

        // Draw background panel behind score
        const HudTextConfig& sh_ = m_gameplayConfig.scoreHud;
        float fs = sh_.fontSize * sh_.scale;
        ImFont* font = ImGui::GetFont();
        ImVec2 textSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, scoreBuf);
        float fx = sw * sh_.pos[0], fy = sh * sh_.pos[1];
        float pad = 8.f;
        dl->AddRectFilled(
            ImVec2(fx - textSz.x / 2 - pad, fy - textSz.y / 2 - pad / 2),
            ImVec2(fx + textSz.x / 2 + pad, fy + textSz.y / 2 + pad / 2),
            IM_COL32(0, 0, 0, 140), 6.f);

        drawHud(m_gameplayConfig.scoreHud, scoreBuf);

        // "SCORE" label above
        HudTextConfig scoreLabel = m_gameplayConfig.scoreHud;
        scoreLabel.pos[1] -= scoreLabel.fontSize * scoreLabel.scale / sh * 1.1f;
        scoreLabel.fontSize *= 0.4f;
        scoreLabel.glow = false;
        drawHud(scoreLabel, "SCORE");
    }

    // Combo — always visible with background panel
    {
        int combo = m_score.getCombo();
        char comboBuf[32];
        snprintf(comboBuf, sizeof(comboBuf), "%d", combo);

        // Draw background panel behind combo
        const HudTextConfig& ch = m_gameplayConfig.comboHud;
        float fs = ch.fontSize * ch.scale;
        ImFont* font = ImGui::GetFont();
        ImVec2 textSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, comboBuf);
        float fx = sw * ch.pos[0], fy = sh * ch.pos[1];
        float pad = 10.f;
        float panelH = textSz.y + ch.fontSize * ch.scale * 0.5f + pad * 2;
        dl->AddRectFilled(
            ImVec2(fx - textSz.x / 2 - pad * 2, fy - textSz.y / 2 - pad),
            ImVec2(fx + textSz.x / 2 + pad * 2, fy + panelH - pad),
            IM_COL32(0, 0, 0, 120), 8.f);

        drawHud(m_gameplayConfig.comboHud, comboBuf);

        // "COMBO" label below
        HudTextConfig comboLabel = m_gameplayConfig.comboHud;
        comboLabel.pos[1] += comboLabel.fontSize * comboLabel.scale / sh * 1.2f;
        comboLabel.fontSize *= 0.45f;
        drawHud(comboLabel, "COMBO");
    }

    // Pause or results overlay
    if (m_showResults) {
        renderResultsOverlay();
    } else if (m_gameplayPaused) {
        renderPauseOverlay();
    }
}

void Engine::renderPauseOverlay() {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;

    // Semi-transparent background
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySz);
    ImGui::Begin("##pause_bg", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(0, 0), displaySz, IM_COL32(0, 0, 0, 160));
    ImGui::End();

    // Centered pause menu
    ImVec2 menuSize(200, 180);
    ImGui::SetNextWindowPos(ImVec2((displaySz.x - menuSize.x) * 0.5f,
                                    (displaySz.y - menuSize.y) * 0.5f));
    ImGui::SetNextWindowSize(menuSize);
    ImGui::Begin("Paused", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Resume", ImVec2(-1, 40))) {
        togglePause();
    }
    ImGui::Spacing();
    if (ImGui::Button("Restart", ImVec2(-1, 40))) {
        m_audio.stop();
        m_audio.play();
        m_clock.setSongTime(0.0);
        m_judgment.reset();
        m_score.reset();
        m_activeTouches.clear();
        m_gameplayPaused = false;
        m_sceneViewer.setPlaying(true);
    }
    ImGui::Spacing();
    if (ImGui::Button("Exit", ImVec2(-1, 40))) {
        exitGameplay();
    }

    ImGui::End();
}

void Engine::renderResultsOverlay() {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;

    // Semi-transparent background
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySz);
    ImGui::Begin("##results_bg", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(0, 0), displaySz, IM_COL32(0, 0, 0, 180));
    ImGui::End();

    // Centered results panel
    ImVec2 panelSz(280, 300);
    ImGui::SetNextWindowPos(ImVec2((displaySz.x - panelSz.x) * 0.5f,
                                    (displaySz.y - panelSz.y) * 0.5f));
    ImGui::SetNextWindowSize(panelSz);
    ImGui::Begin("Results", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Score:     %07d", m_score.getScore());
    ImGui::Text("Max Combo: %d", m_score.getMaxCombo());
    ImGui::Separator();

    const auto& stats = m_judgment.getStats();
    ImGui::TextColored(ImVec4(1.f, 0.95f, 0.2f, 1.f), "Perfect: %d", stats.perfect);
    ImGui::TextColored(ImVec4(0.2f, 1.f, 0.4f, 1.f),  "Good:    %d", stats.good);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.f, 1.f),  "Bad:     %d", stats.bad);
    ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),  "Miss:    %d", stats.miss);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Back", ImVec2(-1, 40))) {
        exitGameplay();
    }

    ImGui::End();
}

// ── Gesture handlers ─────────────────────────────────────────────────────────

void Engine::dispatchHitResult(const HitResult& hit, int lane) {
    auto judgment = m_judgment.judge(hit.timingDelta);
    m_judgment.recordJudgment(judgment);
    m_score.onJudgment(judgment);

    if (m_activeMode)
        m_activeMode->showJudgment(lane >= 0 ? lane : 0, judgment);

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
    int tc = m_gameplayConfig.trackCount;
    int lane = static_cast<int>(evt.pos.x / static_cast<float>(screenW) * tc);
    lane = std::clamp(lane, 0, tc - 1);

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
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        std::cout << "[ESC] testMode=" << engine->m_testMode
                  << " layer=" << (int)engine->m_currentLayer << "\n";
        if (engine->m_currentLayer == EditorLayer::GamePlay) {
            if (engine->m_showResults)
                engine->exitGameplay();
            else
                engine->togglePause();
        }
        else if (engine->m_testMode) {
            engine->exitTestMode();
        }
        else {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }
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
