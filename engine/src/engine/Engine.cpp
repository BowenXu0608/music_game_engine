#include "Engine.h"
#include "game/modes/BandoriRenderer.h"
#include "game/modes/CytusRenderer.h"
#include "game/modes/PhigrosRenderer.h"
#include "game/modes/ArcaeaRenderer.h"
#include "game/modes/LanotaRenderer.h"
#include "game/chart/ChartLoader.h"
#include "input/TouchTypes.h"
#include "input/ScreenMetrics.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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

    // Keyboard callback (backward compat — desktop lane input).
    // Hold notes now carry Bandori-style sample points (authored by dropping
    // a Tap inside the hold's zone). For keyboard play we must actually start
    // an ActiveHold on press so consumeSampleTicks can gate ticks against the
    // currently-pressed lane — otherwise the sample points never fire on
    // desktop.
    m_input.setKeyCallback([this](int lane, bool pressed) {
        if (!m_activeMode || !m_sceneViewer.isPlaying()) return;
        double songT = m_clock.songTime();

        if (pressed) {
            // Every other active keyboard hold tracks whichever lane the
            // player just pressed — lets cross-lane sample ticks follow the
            // finger as it jumps between lane keys.
            for (auto& [_, id] : m_keyboardHolds)
                m_hitDetector.updateHoldLane(id, lane);

            // Try to start a hold for a hold-head in this lane. If one begins,
            // still dispatch a tap-judgment for the head; the sample ticks
            // (and the tail release) are handled by the sample-tick loop.
            auto holdId = m_hitDetector.beginHold(lane, songT);
            if (holdId) {
                m_keyboardHolds[lane] = *holdId;
                HitResult headHit{*holdId, 0.f, NoteType::Hold};
                // Use a dummy zero delta — head judgment isn't critical for
                // keyboard play, the real feedback is sample-tick combo.
                dispatchHitResult(headHit, lane);
                return;
            }
            auto hit = m_hitDetector.checkHit(lane, songT);
            if (hit) dispatchHitResult(*hit, lane);
        } else {
            // Key release: end the keyboard-started hold (if any) for this lane.
            auto it = m_keyboardHolds.find(lane);
            if (it != m_keyboardHolds.end()) {
                auto hit = m_hitDetector.endHold(it->second, songT);
                if (hit) dispatchHitResult(*hit, lane);
                m_keyboardHolds.erase(it);
            }
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
        else if (auto* lan = dynamic_cast<LanotaRenderer*>(m_activeMode.get()))
            handleGestureCircle(*lan, evt, t);
        else if (auto* cyt = dynamic_cast<CytusRenderer*>(m_activeMode.get()))
            handleGestureScanLine(*cyt, evt, t);
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
                if (loadAudio(m_pendingAudioPath)) {
                    m_audioStarted = true;
                }
                m_pendingAudioPath.clear();
            } else {
                // No audio file — proceed without audio sync
                m_audioStarted = true;
            }
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
        double songT = m_clock.songTime();

        // Auto play: consume every note whose time has arrived and dispatch
        // Perfect hits. Runs before the miss sweep so nothing decays to Miss.
        if (m_autoPlay) {
            auto autoHits = m_hitDetector.autoPlayTick(songT);
            for (auto& ah : autoHits) {
                dispatchHitResult(ah.result, ah.lane);
            }
        }

        auto missed = m_hitDetector.update(songT);
        for (auto& m : missed) {
            m_judgment.recordJudgment(Judgment::Miss);
            m_score.onJudgment(Judgment::Miss);
            if (m_activeMode && m.lane >= 0)
                m_activeMode->showJudgment(m.lane, Judgment::Miss);
        }

        // Hold sample-point ticks — Bandori-style. Each tick checks whether
        // the player's touch is on the lane the cross-lane hold expects at
        // that moment. A match awards Perfect; a mismatch awards Miss and
        // counts toward breaking the hold.
        auto ticks = m_hitDetector.consumeSampleTicks(songT);
        for (auto& t : ticks) {
            Judgment j = t.hit ? Judgment::Perfect : Judgment::Miss;
            m_judgment.recordJudgment(j);
            m_score.onJudgment(j);
            if (m_activeMode && t.lane >= 0)
                m_activeMode->showJudgment(t.lane, j);
            // No SFX on hold sample ticks. playClickSfx() allocates a new
            // ma_audio_buffer + ma_sound per call and leaks them both, which
            // adds up fast on dense sample-point holds — the leaked source
            // list keeps growing inside miniaudio's mixer and the audio
            // thread starts to stutter, which the player hears as music
            // lag whenever a hold body crosses the judgement line. The
            // player is already pressing the lane during a hold, so a
            // per-tick click is unnecessary feedback anyway.
        }
        // Holds whose touch wandered off the curve for ≥2 consecutive ticks
        // are marked broken inside the detector. Remove their touch mapping
        // here so the held finger no longer references a dead hold.
        auto broken = m_hitDetector.consumeBrokenHolds();
        for (uint32_t id : broken) {
            for (auto it = m_activeTouches.begin(); it != m_activeTouches.end(); ) {
                if (it->second == id) it = m_activeTouches.erase(it);
                else                  ++it;
            }
        }

        // Scan-line slide sample ticks — compare touch position against
        // expected path position and score Perfect/Miss.
        if (auto* cyt = dynamic_cast<CytusRenderer*>(m_activeMode.get())) {
            auto slideTicks = cyt->consumeSlideTicks(songT);
            for (auto& st : slideTicks) {
                // Find which touch is tracking this slide's hold
                glm::vec2 expected{st.expectedX, st.expectedY};
                bool hit = false;
                for (auto& [touchId, noteId] : m_activeTouches) {
                    if (noteId != st.noteId) continue;
                    // Check last known slide position from HitDetector
                    auto holdIt = m_hitDetector.getActiveHold(st.noteId);
                    if (holdIt && !holdIt->positionSamples.empty()) {
                        glm::vec2 touchPos = holdIt->positionSamples.back();
                        float dist = glm::length(touchPos - expected);
                        hit = dist < ScreenMetrics::dp(64.f); // generous radius
                    }
                    break;
                }
                Judgment j = hit ? Judgment::Perfect : Judgment::Miss;
                m_judgment.recordJudgment(j);
                m_score.onJudgment(j);
            }
        }

        m_activeMode->setActiveHoldIds(m_hitDetector.activeHoldIds());
        m_activeMode->onUpdate(dt, songT);
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
    m_previewMode->setEditorPreview(true);
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

bool Engine::loadAudio(const std::string& path) {
    std::cout << "[Engine] Loading audio: " << path << "\n";
    if (!m_audio.load(path)) {
        std::cout << "[Engine] FAILED to load audio: " << path << "\n";
        return false;
    }
    m_audio.play();
    std::cout << "[Engine] Audio playing\n";
    return true;
}

void Engine::setMode(GameModeRenderer* renderer, const ChartData& chart,
                     const GameModeConfig* config) {
    if (m_activeMode) m_activeMode->onShutdown(m_renderer);

    m_activeMode.reset(renderer);
    m_activeMode->onInit(m_renderer, chart, config);
    m_hitDetector.init(chart);
    if (config) m_hitDetector.setTrackCount(config->trackCount);
    m_judgment.reset();
    m_score.reset();
    m_activeTouches.clear();
    m_keyboardHolds.clear();
    m_currentChart = chart;
}

// ── Game mode factory ────────────────────────────────────────────────────────

std::unique_ptr<GameModeRenderer> Engine::createRenderer(const GameModeConfig& config) {
    switch (config.type) {
        case GameModeType::DropNotes:
            if (config.dimension == DropDimension::ThreeD)
                return std::make_unique<ArcaeaRenderer>();
            return std::make_unique<BandoriRenderer>();
        case GameModeType::Circle:
            // Circle = rotating disk with ring notes (Lanota-style).
            // Dimension toggle isn't exposed for this mode in the editor, so
            // there's only one renderer here regardless of config.dimension.
            return std::make_unique<LanotaRenderer>();
        case GameModeType::ScanLine:
            // ScanLine = horizontal sweep line crossing notes (Cytus-style).
            return std::make_unique<CytusRenderer>();
    }
    return std::make_unique<BandoriRenderer>();
}

// ── Gameplay lifecycle ───────────────────────────────────────────────────────

void Engine::launchGameplay(const SongInfo& song, Difficulty difficulty,
                            const std::string& projectPath, bool autoPlay) {
    m_autoPlay = autoPlay;
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
    m_currentProjectPath = projectPath;

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
    m_currentAudioPath = audioPath;

    // If the previous gameplay exited via the pause menu the clock is still
    // paused; without this resume tick() returns 0 forever, songTime stays
    // at -leadIn, loadAudio never fires, and the new run looks frozen.
    m_clock.resume();

    switchLayer(EditorLayer::GamePlay);

    std::cout << "[Engine] Gameplay started: " << song.name
              << " (offset=" << song.gameMode.audioOffset << "s, leadIn=" << leadIn << "s)\n";
}

void Engine::launchGameplayDirect(const SongInfo& song, const ChartData& chart,
                                  const std::string& projectPath) {
    std::string audioPath = projectPath + "/" + song.audioFile;

    auto renderer = createRenderer(song.gameMode);
    setMode(renderer.release(), chart, &song.gameMode);
    m_currentProjectPath = projectPath;

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
    m_currentAudioPath = m_pendingAudioPath;

    // See launchGameplay — same clock-resume fix.
    m_clock.resume();

    switchLayer(EditorLayer::GamePlay);

    std::cout << "[Engine] Gameplay started (direct): " << song.name << "\n";
}

void Engine::restartGameplay() {
    // Re-run the same setup the launchGameplay path does so renderer state,
    // hit detector, judgment, score, lead-in, and audio gating are all back
    // to a clean slate. The previous restart path only reset the score and
    // clock — leaving consumed-note sets, active holds, and the audio-start
    // gate stale, which is why the second run looked dead.
    if (!m_activeMode) return;

    m_audio.stop();

    auto renderer = createRenderer(m_gameplayConfig);
    setMode(renderer.release(), m_currentChart, &m_gameplayConfig);

    m_gameplayPaused = false;
    m_showResults = false;
    m_sceneViewer.setPlaying(true);

    float leadIn = 2.0f;
    m_clock.setSongTime(-leadIn - m_gameplayConfig.audioOffset);
    m_gameplayLeadIn = leadIn + m_gameplayConfig.audioOffset;
    m_audioStarted = false;
    // The first launch consumed m_pendingAudioPath after loading. Restore
    // it from the cached path so the lead-in handler reloads + replays the
    // same song instead of falling into the silent "no audio" branch (which
    // would also instantly trip the song-end guard and pop the results
    // screen, since !m_audio.isPlaying() becomes true forever).
    m_pendingAudioPath = m_currentAudioPath;

    // The Restart button is reachable from the pause menu, which paused
    // the clock via togglePause(). Resume it explicitly so tick() returns
    // a real dt again — without this, songTime stays frozen and nothing
    // on screen moves.
    m_clock.resume();
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
        // In test mode, exiting a song should hand the player back to the
        // music selection page so they can pick another song. Test mode
        // itself stays active — the standalone window only closes via
        // ESC from the start screen / a full exitTestMode() path.
        switchLayer(EditorLayer::MusicSelection);
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

bool Engine::spawnTestGameProcess(const std::string& projectPath) {
    // Persist the latest music_selection edits so the child process sees
    // the current trackCount / game mode / notes.
    m_musicSelectionEditor.save();

#ifdef _WIN32
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::filesystem::path absProject = std::filesystem::absolute(projectPath);
    std::string projectArg = absProject.string();

    std::wstring cmdLine = std::wstring(L"\"") + exePath + L"\" --test \"";
    int len = MultiByteToWideChar(CP_UTF8, 0, projectArg.c_str(), -1, nullptr, 0);
    std::wstring wProject(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, projectArg.c_str(), -1, wProject.data(), len);
    cmdLine += wProject + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                       0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        std::cout << "[Engine] Spawned test game child process\n";
        return true;
    }
    std::cout << "[Engine] Failed to spawn test game process (err=" << GetLastError() << ")\n";
    return false;
#else
    std::string cmd = std::string("\"/proc/self/exe\" --test \"")
                    + std::filesystem::absolute(projectPath).string() + "\" &";
    return system(cmd.c_str()) == 0;
#endif
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
        restartGameplay();
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
            // Consume any drag notes at this lane (auto-hit on touch)
            for (auto& dh : m_hitDetector.consumeDrags(lane, songTime))
                dispatchHitResult(dh, lane);
            break;
        }
        case GestureType::Flick: {
            auto hit = m_hitDetector.checkHit(lane, songTime);
            if (hit) {
                if (hit->noteType == NoteType::Flick) {
                    float speed = glm::length(evt.velocity);
                    float dirAcc = speed > 0.f ? std::abs(evt.velocity.x) / speed : 0.f;
                    auto judgment = m_judgment.judgeFlick(hit->timingDelta, dirAcc);
                    m_judgment.recordJudgment(judgment);
                    m_score.onJudgment(judgment);
                    if (m_activeMode) m_activeMode->showJudgment(lane, judgment);
                } else {
                    // Flick gesture on a non-flick note — treat as plain tap
                    dispatchHitResult(*hit, lane);
                }
            }
            break;
        }
        case GestureType::HoldBegin: {
            auto noteId = m_hitDetector.beginHold(lane, songTime);
            if (noteId) {
                m_activeTouches[evt.touchId] = *noteId;
                // Dispatch head judgment for the hold start
                HitResult headHit{*noteId, 0.f, NoteType::Hold};
                dispatchHitResult(headHit, lane);
            }
            break;
        }
        // A held finger that starts moving turns into a Slide. For cross-lane
        // holds we feed every position update back into the detector so the
        // sample-tick gate (Bandori-style) sees the player's current lane.
        case GestureType::SlideBegin:
        case GestureType::SlideMove: {
            auto it = m_activeTouches.find(evt.touchId);
            if (it != m_activeTouches.end())
                m_hitDetector.updateHoldLane(it->second, lane);
            // Drag notes auto-hit when a sliding finger passes through
            for (auto& dh : m_hitDetector.consumeDrags(lane, songTime))
                dispatchHitResult(dh, lane);
            break;
        }
        case GestureType::HoldEnd:
        case GestureType::SlideEnd: {
            // SlideEnd is the natural end-of-touch for any hold that began
            // straight and then started moving — without this branch the
            // m_activeTouches entry would leak and the hold never resolves.
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

// ── Circle (Lanota) gesture dispatch ─────────────────────────────────────────
// Touch input for the rotating-disk mode.  Unlike the lane-based handler, this
// asks the LanotaRenderer to *pick* the specific note whose current screen
// position is closest to the tap, then asks HitDetector to consume that note
// by id.  We bypass dispatchHitResult so that visual feedback can be emitted
// at the picked note's exact disk position rather than via lane-based search.
//
// CIRCLE_PICK_DP is in density-independent pixels (160-DPI reference); on a
// 480-DPI phone it expands to 144 px so a fingertip-sized region around the
// tap is searched for notes.  See engine/src/input/ScreenMetrics.h.
void Engine::handleGestureCircle(LanotaRenderer& lan,
                                 const GestureEvent& evt, double songTime) {
    constexpr float CIRCLE_PICK_DP = 48.f;  // ≈ 7.6 mm fingertip radius
    const float pickPx = ScreenMetrics::dp(CIRCLE_PICK_DP);

    auto judgeAndFeedback = [this, &lan](const HitResult& hit, Judgment j) {
        m_judgment.recordJudgment(j);
        m_score.onJudgment(j);
        lan.markNoteHit(hit.noteId);
        lan.emitHitFeedback(hit.noteId, j);
    };

    switch (evt.type) {
        case GestureType::Tap: {
            auto pick = lan.pickNoteAt(evt.pos, songTime, pickPx);
            if (!pick) break;
            auto hit = m_hitDetector.consumeNoteById(pick->noteId, songTime);
            if (!hit) break;
            judgeAndFeedback(*hit, m_judgment.judge(hit->timingDelta));
            break;
        }
        case GestureType::Flick: {
            auto pick = lan.pickNoteAt(evt.pos, songTime, pickPx);
            if (!pick) break;
            auto hit = m_hitDetector.consumeNoteById(pick->noteId, songTime);
            if (!hit) break;
            if (hit->noteType == NoteType::Flick) {
                float speed  = glm::length(evt.velocity);
                float dirAcc = speed > 0.f ? std::abs(evt.velocity.x) / speed : 0.f;
                judgeAndFeedback(*hit, m_judgment.judgeFlick(hit->timingDelta, dirAcc));
            } else {
                // Flick gesture on a non-flick note — fall back to plain tap.
                judgeAndFeedback(*hit, m_judgment.judge(hit->timingDelta));
            }
            break;
        }
        case GestureType::HoldBegin: {
            auto pick = lan.pickNoteAt(evt.pos, songTime, pickPx);
            if (!pick) break;
            auto hit = m_hitDetector.beginHoldById(pick->noteId, songTime);
            if (hit) {
                m_activeTouches[evt.touchId] = hit->noteId;
                judgeAndFeedback(*hit, m_judgment.judge(hit->timingDelta));
            }
            break;
        }
        case GestureType::HoldEnd: {
            auto it = m_activeTouches.find(evt.touchId);
            if (it == m_activeTouches.end()) break;
            auto hit = m_hitDetector.endHold(it->second, songTime);
            if (hit) judgeAndFeedback(*hit, m_judgment.judge(hit->timingDelta));
            m_activeTouches.erase(it);
            break;
        }
        default: break;
    }
}

// ── Scan Line (Cytus) gesture dispatch ───────────────────────────────────────
// Free-position 2D mode. CytusRenderer::pickNoteAt maps the tap to the
// nearest chart note within a fingertip-sized pixel tolerance *and* a
// timing window; HitDetector then validates/consumes that note by id and
// JudgmentSystem classifies the timing error. Visual feedback is emitted
// on the picked note's stored screen position via markNoteHit.
//
// Slide sample-point ticks are not yet wired — the HitDetector hold/tick
// state machine is lane-based, and scan-line slides travel a free path
// rather than a lane index. This is a documented follow-up.

void Engine::handleGestureScanLine(CytusRenderer& cyt,
                                   const GestureEvent& evt, double songTime) {
    constexpr float SCAN_PICK_DP = 48.f; // ~fingertip radius (DPI-normalized)
    const float pickPx = ScreenMetrics::dp(SCAN_PICK_DP);

    auto judgeAndFeedback = [this, &cyt](const HitResult& hit, Judgment j) {
        m_judgment.recordJudgment(j);
        m_score.onJudgment(j);
        cyt.markNoteHit(hit.noteId);
    };

    switch (evt.type) {
        case GestureType::Tap: {
            auto pick = cyt.pickNoteAt(evt.pos, songTime, pickPx);
            if (!pick) break;
            auto hit = m_hitDetector.consumeNoteById(pick->noteId, songTime);
            if (!hit) break;
            judgeAndFeedback(*hit, m_judgment.judge(hit->timingDelta));
            break;
        }
        case GestureType::Flick: {
            auto pick = cyt.pickNoteAt(evt.pos, songTime, pickPx);
            if (!pick) break;
            auto hit = m_hitDetector.consumeNoteById(pick->noteId, songTime);
            if (!hit) break;
            if (hit->noteType == NoteType::Flick) {
                float speed  = glm::length(evt.velocity);
                float dirAcc = speed > 0.f ? std::abs(evt.velocity.x) / speed : 0.f;
                judgeAndFeedback(*hit, m_judgment.judgeFlick(hit->timingDelta, dirAcc));
            } else {
                judgeAndFeedback(*hit, m_judgment.judge(hit->timingDelta));
            }
            break;
        }
        case GestureType::HoldBegin: {
            auto pick = cyt.pickNoteAt(evt.pos, songTime, pickPx);
            if (!pick) break;
            // Both Hold and Slide begin as a held touch — beginHoldById
            // handles either NoteType as long as HitDetector has it as a
            // duration-bearing note.
            auto hit = m_hitDetector.beginHoldById(pick->noteId, songTime);
            if (hit) {
                m_activeTouches[evt.touchId] = hit->noteId;
                judgeAndFeedback(*hit, m_judgment.judge(hit->timingDelta));
            }
            break;
        }
        case GestureType::SlideBegin:
        case GestureType::SlideMove: {
            auto it = m_activeTouches.find(evt.touchId);
            if (it != m_activeTouches.end())
                m_hitDetector.updateSlide(it->second, evt.pos, songTime);
            break;
        }
        case GestureType::SlideEnd:
        case GestureType::HoldEnd: {
            auto it = m_activeTouches.find(evt.touchId);
            if (it == m_activeTouches.end()) break;
            auto hit = m_hitDetector.endHold(it->second, songTime);
            if (hit) judgeAndFeedback(*hit, m_judgment.judge(hit->timingDelta));
            m_activeTouches.erase(it);
            break;
        }
        default: break;
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
    try {
        auto* engine = reinterpret_cast<Engine*>(glfwGetWindowUserPointer(window));
        std::vector<std::string> srcPaths;
        srcPaths.reserve(count);
        for (int i = 0; i < count; ++i)
            srcPaths.emplace_back(paths[i]);

        switch (engine->m_currentLayer) {
        case EditorLayer::StartScreen:
            if (!engine->m_startScreenEditor.projectPath().empty())
                engine->m_startScreenEditor.importFiles(srcPaths);
            break;
        case EditorLayer::MusicSelection:
            if (!engine->m_musicSelectionEditor.projectPath().empty())
                engine->m_musicSelectionEditor.importFiles(srcPaths);
            break;
        case EditorLayer::SongEditor:
            if (!engine->m_songEditor.projectPath().empty())
                engine->m_songEditor.importFiles(srcPaths);
            break;
        default:
            break;
        }
    } catch (const std::exception& e) {
        std::cerr << "[drop] Error: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[drop] Unknown error\n";
    }
}
