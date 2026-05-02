// ============================================================================
// AndroidEngine Implementation
// ============================================================================
#include "AndroidEngine.h"
#include "AndroidFileIO.h"
#include "ui/SettingsPageUI.h"
#include <android/log.h>
#include <android/input.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "MusicGame", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MusicGame", __VA_ARGS__)

// ImGui Vulkan backend (cross-platform)
#include <imgui_impl_vulkan.h>

void AndroidEngine::init(android_app* app, const std::string& shaderDir) {
    m_app = app;
    m_shaderDir = shaderDir;
    m_running = true;

    m_clock.start();
    m_audio.init();

    // Setup input callbacks
    m_input.init();
    m_input.setGestureCallback([this](const GestureEvent& evt) {
        if (m_screen != GameScreen::Gameplay || !m_activeMode) return;
        double songTime = m_clock.songTime();
        glm::vec2 screenSize = {(float)m_renderer.width(), (float)m_renderer.height()};

        if (evt.type == GestureType::Tap) {
            auto hit = m_hitDetector.checkHitPosition(evt.pos, screenSize, songTime);
            if (hit) {
                auto judgment = m_judgment.judge(hit->timingDelta);
                m_judgment.recordJudgment(judgment);
                m_score.onJudgment(judgment);
                if (m_activeMode && hit->noteId >= 0)
                    m_activeMode->showJudgment(0, judgment);
            }
        }
    });

    loadStartScreen();
    loadProject();
    loadPlayerSettingsFile();
    applyPlayerSettings();
    LOGI("AndroidEngine initialized");
}

void AndroidEngine::shutdown() {
    if (m_activeMode) {
        m_activeMode->onShutdown(m_renderer);
        m_activeMode.reset();
    }
    m_audio.shutdown();
    if (m_vulkanReady) {
        vkDeviceWaitIdle(m_renderer.context().device());
        m_renderer.shutdown();
    }
    m_vulkanReady = false;
    m_running = false;
}

void AndroidEngine::onWindowInit(ANativeWindow* window) {
    if (m_vulkanReady) return;
    if (!window) {
        LOGE("onWindowInit called with null window");
        return;
    }

    androidSetNativeWindow(window);
    androidSetSwapchainWindow(window);

    // Extract shaders to internal storage (Pipeline reads .spv via std::ifstream)
    const char* shaderFiles[] = {
        "quad.vert.spv", "quad.frag.spv",
        "quad_unlit.frag.spv", "quad_glow.frag.spv",
        "quad_scroll.frag.spv", "quad_pulse.frag.spv", "quad_gradient.frag.spv",
        "line.vert.spv", "line.frag.spv",
        "mesh.vert.spv",
        "mesh_unlit.frag.spv", "mesh_glow.frag.spv",
        "mesh_scroll.frag.spv", "mesh_pulse.frag.spv", "mesh_gradient.frag.spv",
        "composite.vert.spv", "composite.frag.spv",
        "bloom_downsample.comp.spv", "bloom_upsample.comp.spv"
    };
    for (auto& name : shaderFiles) {
        std::string assetPath = std::string("shaders/") + name;
        AndroidFileIO::extractToInternal(assetPath);
    }
    m_shaderDir = AndroidFileIO::internalPath() + "shaders";

    try {
        m_renderer.init(nullptr, m_shaderDir, false, true);
    } catch (const std::exception& e) {
        LOGE("Renderer init failed: %s", e.what());
        throw;  // re-throw so android_main writes crash.txt
    }

    // Setup ImGui (minimal — just for HUD text)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    applyTheme();

    // ImGui Vulkan init
    ImGui_ImplVulkan_InitInfo initInfo{};
    memset(&initInfo, 0, sizeof(initInfo));
    initInfo.Instance       = m_renderer.context().instance();
    initInfo.PhysicalDevice = m_renderer.context().physicalDevice();
    initInfo.Device         = m_renderer.context().device();
    initInfo.QueueFamily    = m_renderer.context().queueFamilies().graphics.value();
    initInfo.Queue          = m_renderer.context().graphicsQueue();
    initInfo.MinImageCount  = 2;
    initInfo.ImageCount     = 3;
    initInfo.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    initInfo.RenderPass     = m_renderer.swapchainRenderPass();

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128}
    };
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCI.maxSets       = 128;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = poolSizes;

    VkDescriptorPool imguiPool;
    if (vkCreateDescriptorPool(m_renderer.context().device(), &poolCI, nullptr, &imguiPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    }
    initInfo.DescriptorPool = imguiPool;

    ImGui_ImplVulkan_Init(&initInfo);

    // Upload fonts
    ImGui_ImplVulkan_CreateFontsTexture();

    m_vulkanReady = true;
    LOGI("Vulkan renderer ready: %dx%d", m_renderer.width(), m_renderer.height());
}

void AndroidEngine::onWindowResize() {
    if (!m_vulkanReady) return;
    vkDeviceWaitIdle(m_renderer.context().device());
    try {
        m_renderer.onResize(nullptr);
        LOGI("Swapchain recreated: %dx%d", m_renderer.width(), m_renderer.height());
    } catch (const std::exception& e) {
        LOGE("onResize failed: %s", e.what());
    }
}

void AndroidEngine::onWindowTerm() {
    if (!m_vulkanReady) return;
    vkDeviceWaitIdle(m_renderer.context().device());
    releaseTextures();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
    m_renderer.shutdown();
    m_vulkanReady = false;
}

void AndroidEngine::onTouchEvent(AInputEvent* event) {
    int action = AMotionEvent_getAction(event);
    int maskedAction = action & AMOTION_EVENT_ACTION_MASK;
    int pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    auto now = std::chrono::high_resolution_clock::now();
    double timestamp = std::chrono::duration<double>(now.time_since_epoch()).count();

    auto inject = [&](int idx, TouchPhase phase) {
        int32_t id = AMotionEvent_getPointerId(event, idx);
        float x = AMotionEvent_getX(event, idx);
        float y = AMotionEvent_getY(event, idx);
        m_input.injectTouch(id, phase, {x, y}, timestamp);
    };

    // Mirror the primary pointer into ImGui as a single mouse, so widgets
    // like InvisibleButton on the start screen / music selection page see it.
    ImGuiIO& io = ImGui::GetIO();
    float px = AMotionEvent_getX(event, pointerIndex);
    float py = AMotionEvent_getY(event, pointerIndex);

    switch (maskedAction) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            inject(pointerIndex, TouchPhase::Began);
            io.AddMousePosEvent(px, py);
            io.AddMouseButtonEvent(0, true);
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            inject(pointerIndex, TouchPhase::Ended);
            io.AddMousePosEvent(px, py);
            io.AddMouseButtonEvent(0, false);
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            for (size_t i = 0; i < AMotionEvent_getPointerCount(event); ++i)
                inject(i, TouchPhase::Moved);
            io.AddMousePosEvent(px, py);
            break;
        case AMOTION_EVENT_ACTION_CANCEL:
            for (size_t i = 0; i < AMotionEvent_getPointerCount(event); ++i)
                inject(i, TouchPhase::Cancelled);
            io.AddMouseButtonEvent(0, false);
            break;
    }
}

void AndroidEngine::mainLoop() {
    while (m_running) {
        // Process Android events
        int events;
        android_poll_source* source;
        while (ALooper_pollOnce(m_vulkanReady ? 0 : -1, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(m_app, source);
            if (m_app->destroyRequested) {
                m_running = false;
                return;
            }
        }

        if (!m_vulkanReady) continue;

        float dt = m_clock.tick();
        update(dt);
        render();
    }
}

void AndroidEngine::update(float dt) {
    // Lead-in timing (same logic as desktop Engine)
    if (m_screen == GameScreen::Gameplay && !m_audioStarted && !m_gameplayPaused) {
        double songT = m_clock.songTime() + dt;
        m_clock.setSongTime(songT);
        if (songT >= 0.0) {
            if (!m_pendingAudioPath.empty()) {
                // Extract audio to internal storage for miniaudio
                std::string fsPath = AndroidFileIO::extractToInternal(m_pendingAudioPath);
                if (!fsPath.empty() && m_audio.load(fsPath)) {
                    m_audio.play();
                }
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

    m_input.update(m_clock.songTime());

    // Gameplay update
    if (m_activeMode && m_screen == GameScreen::Gameplay && !m_gameplayPaused) {
        double songT = m_clock.songTime();
        auto missed = m_hitDetector.update(songT);
        for (auto& m : missed) {
            m_judgment.recordJudgment(Judgment::Miss);
            m_score.onJudgment(Judgment::Miss);
        }
        auto ticks = m_hitDetector.consumeSampleTicks(songT);
        for (auto& t : ticks) {
            Judgment j = t.hit ? Judgment::Perfect : Judgment::Miss;
            m_judgment.recordJudgment(j);
            m_score.onJudgment(j);
            if (t.hit) m_audio.playClickSfx();
        }
        auto broken = m_hitDetector.consumeBrokenHolds();
        (void)broken;
        m_activeMode->onUpdate(dt, songT);
    }

    // Song-end detection
    if (m_screen == GameScreen::Gameplay && !m_gameplayPaused && !m_showResults) {
        if (m_clock.songTime() > 2.0 && !m_audio.isPlaying()) {
            m_showResults = true;
            m_screen = GameScreen::Results;
        }
    }
}

void AndroidEngine::render() {
    if (!m_renderer.beginFrame()) {
        m_renderer.onResize(nullptr);
        return;
    }

    // Render game content into scene framebuffer
    if (m_activeMode && m_screen == GameScreen::Gameplay) {
        float sw = (float)m_renderer.width();
        float sh = (float)m_renderer.height();
        Camera cam = Camera::makeOrtho(0.f, sw, sh, 0.f);
        m_renderer.setCamera(cam);
        m_activeMode->onRender(m_renderer);
    }

    m_renderer.endFrame();

    // ImGui HUD (rendered in swapchain pass)
    // No platform backend on Android — set display size and delta time manually
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)m_renderer.width(), (float)m_renderer.height());
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    io.DeltaTime = std::chrono::duration<float>(now - lastTime).count();
    if (io.DeltaTime <= 0.f) io.DeltaTime = 1.f / 60.f;
    lastTime = now;

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    switch (m_screen) {
        case GameScreen::StartScreen:
            renderStartScreen();
            break;
        case GameScreen::MusicSelection:
            renderMusicSelection();
            break;
        case GameScreen::Settings:
            renderSettings();
            break;
        case GameScreen::Gameplay:
            renderGameplayHUD();
            break;
        case GameScreen::Results:
            renderResultsHUD();
            break;
    }

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_renderer.currentCmd());

    m_renderer.finishFrame();
}

void AndroidEngine::renderGameplayHUD() {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;

    // Background dim overlay (under the HUD, over the rendered scene).
    if (m_playerSettings.backgroundDim > 0.f) {
        const float a = std::min(m_playerSettings.backgroundDim, 1.f) * 0.75f;
        ImU32 dim = IM_COL32(0, 0, 0, (int)(a * 255.f));
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(0, 0), displaySz, dim);
    }

    // FPS counter (top-left corner).
    if (m_playerSettings.fpsCounter) {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(80, 28));
        ImGui::Begin("##fps", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(0.2f, 1.f, 0.4f, 1.f),
                           "%.0f fps", ImGui::GetIO().Framerate);
        ImGui::End();
    }

    // Combo display
    ImGui::SetNextWindowPos(ImVec2(displaySz.x * 0.5f - 50, 20));
    ImGui::SetNextWindowSize(ImVec2(100, 60));
    ImGui::Begin("##combo", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    ImGui::SetWindowFontScale(2.0f);
    int combo = m_score.getCombo();
    if (combo > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", combo);
        ImVec2 tsz = ImGui::CalcTextSize(buf);
        ImGui::SetCursorPosX((100 - tsz.x) * 0.5f);
        ImGui::TextColored(ImVec4(1.f, 0.9f, 0.3f, 1.f), "%s", buf);
    }
    ImGui::End();

    // Score display
    ImGui::SetNextWindowPos(ImVec2(displaySz.x - 160, 10));
    ImGui::SetNextWindowSize(ImVec2(150, 40));
    ImGui::Begin("##score", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    ImGui::Text("SCORE: %07d", m_score.getScore());
    ImGui::End();
}

void AndroidEngine::renderResultsHUD() {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;

    ImVec2 panelSz(300, 350);
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

    if (ImGui::Button("Retry", ImVec2(-1, 50))) {
        startGameplay(m_selectedSong);
    }
    if (ImGui::Button("Back", ImVec2(-1, 50))) {
        exitGameplay();
    }

    ImGui::End();
}

// Aspect-fill the source rect onto dest; returns the [uv0, uv1] crop region.
static void aspectFillUV(float srcW, float srcH, float dstW, float dstH,
                         ImVec2& uv0, ImVec2& uv1) {
    if (srcW <= 0.f || srcH <= 0.f || dstW <= 0.f || dstH <= 0.f) {
        uv0 = ImVec2(0, 0); uv1 = ImVec2(1, 1); return;
    }
    float srcAR = srcW / srcH;
    float dstAR = dstW / dstH;
    if (srcAR > dstAR) {
        // Crop horizontally
        float keep = dstAR / srcAR;
        float pad  = (1.f - keep) * 0.5f;
        uv0 = ImVec2(pad, 0.f); uv1 = ImVec2(1.f - pad, 1.f);
    } else {
        // Crop vertically
        float keep = srcAR / dstAR;
        float pad  = (1.f - keep) * 0.5f;
        uv0 = ImVec2(0.f, pad); uv1 = ImVec2(1.f, 1.f - pad);
    }
}

void AndroidEngine::renderStartScreen() {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    // ── Background image (aspect-filled to fully cover the screen) ──────────
    ImTextureID bgTex = loadAssetTexture(m_startBgPath);
    if (bgTex) {
        ImVec2 uv0, uv1;
        // Approximate: assume the source is at least roughly 16:9; the texture
        // manager doesn't expose width/height through ImTextureID, but the cover
        // pass is forgiving for portrait/landscape mismatches.
        aspectFillUV(16.f, 9.f, displaySz.x, displaySz.y, uv0, uv1);
        bg->AddImage(bgTex, ImVec2(0, 0), displaySz, uv0, uv1);
        // Slight darken so text reads cleanly.
        bg->AddRectFilled(ImVec2(0, 0), displaySz, IM_COL32(8, 10, 14, 90));
    } else {
        // No bg → solid dark.
        bg->AddRectFilled(ImVec2(0, 0), displaySz, IM_COL32(14, 18, 24, 255));
    }

    // ── Title ───────────────────────────────────────────────────────────────
    const char* title = m_startTitleText.empty() ? "Music Game" : m_startTitleText.c_str();
    // ImGui's default font is 13 px; fontScale = pixelSize / 13.
    float titleScale = m_startTitleFontPx / 13.f;
    ImFont* font = ImGui::GetFont();
    float titlePx  = font->FontSize * titleScale;
    ImVec2 titleSz = font->CalcTextSizeA(titlePx, FLT_MAX, 0.0f, title);
    ImVec2 titlePos(displaySz.x * m_startTitlePos.x - titleSz.x * 0.5f,
                    displaySz.y * m_startTitlePos.y - titleSz.y * 0.5f);
    ImU32 titleCol = ImGui::ColorConvertFloat4ToU32(m_startTitleColor);
    bg->AddText(font, titlePx, titlePos, titleCol, title);

    // ── Tap prompt ──────────────────────────────────────────────────────────
    const char* tap = m_startTapText.empty() ? "Tap to Start" : m_startTapText.c_str();
    float tapScale = m_startTapFontPx / 13.f;
    float tapPx    = font->FontSize * tapScale;
    ImVec2 tapSz   = font->CalcTextSizeA(tapPx, FLT_MAX, 0.0f, tap);
    // Pulse the tap prompt
    float pulse = 0.6f + 0.4f * (0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 2.5f));
    ImVec2 tapPos(displaySz.x * m_startTapPos.x - tapSz.x * 0.5f,
                  displaySz.y * m_startTapPos.y - tapSz.y * 0.5f);
    ImU32 tapCol = IM_COL32(255, 255, 255, (int)(255 * pulse));
    bg->AddText(font, tapPx, tapPos, tapCol, tap);

    // ── Full-window tap capture ─────────────────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySz);
    ImGui::Begin("##startscreen", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    if (ImGui::InvisibleButton("##startTap", displaySz)) {
        m_screen = GameScreen::MusicSelection;
        LOGI("StartScreen tapped -> MusicSelection");
    }
    ImGui::End();
}

void AndroidEngine::renderMusicSelection() {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();

    // ── Background image + frosted overlay ──────────────────────────────────
    ImTextureID bgTex = loadAssetTexture(m_musicBgPath);
    if (bgTex) {
        ImVec2 uv0, uv1;
        aspectFillUV(16.f, 9.f, displaySz.x, displaySz.y, uv0, uv1);
        bg->AddImage(bgTex, ImVec2(0, 0), displaySz, uv0, uv1);
        bg->AddRectFilled(ImVec2(0, 0), displaySz, IM_COL32(6, 8, 12, 170));
    } else {
        bg->AddRectFilled(ImVec2(0, 0), displaySz, IM_COL32(14, 18, 24, 255));
    }

    // ── Header bar ──────────────────────────────────────────────────────────
    float headerH = displaySz.y * 0.10f;
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(displaySz.x, headerH),
                      IM_COL32(0, 0, 0, 130));
    ImFont* font = ImGui::GetFont();
    float titlePx = font->FontSize * 2.4f;
    bg->AddText(font, titlePx, ImVec2(displaySz.x * 0.04f, headerH * 0.30f),
                IM_COL32(255, 255, 255, 240), "Select a Song");

    // ── Song cards (rectangle grid; one row, scrolls horizontally if needed) ─
    ImTextureID fcTex = loadAssetTexture(m_fcImagePath);
    ImTextureID apTex = loadAssetTexture(m_apImagePath);

    float cardsAreaY = headerH + 24.f;
    float cardsAreaH = displaySz.y * 0.55f;
    int   nSongs     = (int)m_songs.size();
    if (nSongs > 0) {
        // Card sizing — fit up to 4 across, with 16-px gaps and 24-px side margins
        int  cols = std::min(nSongs, 4);
        float gap  = 16.f;
        float side = 24.f;
        float cardW = (displaySz.x - 2 * side - (cols - 1) * gap) / cols;
        float cardH = cardsAreaH * 0.85f;
        float startX = side;
        float startY = cardsAreaY + (cardsAreaH - cardH) * 0.5f;

        for (int i = 0; i < nSongs; ++i) {
            int col = i % cols;
            int row = i / cols;
            float x = startX + col * (cardW + gap);
            float y = startY + row * (cardH + gap);
            ImVec2 p0(x, y), p1(x + cardW, y + cardH);
            bool selected = (i == m_selectedSong);

            // Cover image
            ImTextureID coverTex = loadAssetTexture(m_songs[i].coverImage);
            if (coverTex) {
                ImVec2 uv0, uv1;
                aspectFillUV(1.f, 1.f, cardW, cardH, uv0, uv1);
                bg->AddImage(coverTex, p0, p1, uv0, uv1);
            } else {
                bg->AddRectFilled(p0, p1, IM_COL32(20, 24, 30, 230));
            }
            // Card border + selected ring
            bg->AddRect(p0, p1,
                        selected ? IM_COL32(80, 220, 240, 255) : IM_COL32(70, 90, 110, 180),
                        8.f, 0, selected ? 4.f : 1.5f);

            // Bottom gradient strip for the title to sit on
            float stripH = cardH * 0.30f;
            bg->AddRectFilledMultiColor(
                ImVec2(p0.x, p1.y - stripH), p1,
                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                IM_COL32(0, 0, 0, 220), IM_COL32(0, 0, 0, 220));

            // Title + artist
            float namePx = font->FontSize * 1.4f;
            float artPx  = font->FontSize * 1.0f;
            bg->AddText(font, namePx,
                        ImVec2(p0.x + 12, p1.y - stripH + 12),
                        IM_COL32(255, 255, 255, 245),
                        m_songs[i].name.c_str());
            bg->AddText(font, artPx,
                        ImVec2(p0.x + 12, p1.y - stripH + 12 + namePx + 4),
                        IM_COL32(220, 220, 230, 200),
                        m_songs[i].artist.c_str());

            // Best score (top-right)
            char scoreBuf[32];
            snprintf(scoreBuf, sizeof(scoreBuf), "%d", m_songs[i].score);
            ImVec2 ssz = font->CalcTextSizeA(font->FontSize * 1.0f, FLT_MAX, 0.f, scoreBuf);
            bg->AddText(font, font->FontSize * 1.0f,
                        ImVec2(p1.x - ssz.x - 10, p0.y + 8),
                        IM_COL32(255, 230, 100, 235), scoreBuf);

            // Achievement badge (top-left)
            ImTextureID badge = (m_songs[i].achievement == "AP") ? apTex
                              : (m_songs[i].achievement == "FC") ? fcTex
                              : (ImTextureID)0;
            if (badge) {
                float bsz = std::min(48.f, cardW * 0.20f);
                bg->AddImage(badge,
                             ImVec2(p0.x + 8, p0.y + 8),
                             ImVec2(p0.x + 8 + bsz, p0.y + 8 + bsz));
            }

            // Tap to select
            ImGui::SetCursorScreenPos(p0);
            char id[16]; snprintf(id, sizeof(id), "##song%d", i);
            if (ImGui::InvisibleButton(id, ImVec2(cardW, cardH))) {
                m_selectedSong = i;
            }
        }
    }

    // ── Footer buttons ──────────────────────────────────────────────────────
    float btnH    = 64.f;
    float btnPad  = 24.f;
    float btnRowY = displaySz.y - btnH - btnPad;
    float btnGap  = 16.f;
    float btnW    = (displaySz.x - 2 * btnPad - btnGap) * 0.5f;

    ImGui::SetCursorScreenPos(ImVec2(btnPad, btnRowY));
    if (ImGui::Button("SETTINGS", ImVec2(btnW, btnH))) {
        m_screen = GameScreen::Settings;
    }
    ImGui::SetCursorScreenPos(ImVec2(btnPad + btnW + btnGap, btnRowY));
    if (!m_songs.empty()) {
        if (ImGui::Button("PLAY", ImVec2(btnW, btnH))) {
            startGameplay(m_selectedSong);
        }
    }
}

void AndroidEngine::renderSettings() {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;
    SettingsPageUI::Host host;
    host.audio  = &m_audio;
    host.onSave = [this]() { savePlayerSettingsFile(); applyPlayerSettings(); };
    host.onBack = [this]() { m_screen = GameScreen::MusicSelection; };
    SettingsPageUI::render(ImVec2(0, 0), displaySz, m_playerSettings, host, /*readOnly=*/false);
}

// Minimal JSON string extractor: find "key":"value" and return value.
// Handles escaped quotes. Returns empty string if not found.
static std::string jsonString(const std::string& json, const std::string& key, size_t searchFrom = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, searchFrom);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + needle.size());  // skip to colon area
    if (pos == std::string::npos) return "";
    pos++;  // skip opening quote
    std::string result;
    for (size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) { result += json[++i]; continue; }
        if (json[i] == '"') break;
        result += json[i];
    }
    return result;
}

static double jsonDouble(const std::string& json, const std::string& key, size_t searchFrom = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, searchFrom);
    if (pos == std::string::npos) return 0.0;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return 0.0;
    return std::strtod(json.c_str() + pos + 1, nullptr);
}

static int jsonInt(const std::string& json, const std::string& key, size_t searchFrom = 0) {
    return static_cast<int>(jsonDouble(json, key, searchFrom));
}

// Find a "key": [a, b, c, ...] number array starting at searchFrom and fill `out`.
static void jsonNumArray(const std::string& json, const std::string& key,
                         float* out, int n, size_t searchFrom = 0) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle, searchFrom);
    if (pos == std::string::npos) return;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return;
    const char* p = json.c_str() + pos + 1;
    char* endp = nullptr;
    for (int i = 0; i < n; ++i) {
        out[i] = std::strtof(p, &endp);
        if (endp == p) break;
        p = endp;
        while (*p == ',' || *p == ' ' || *p == '\n' || *p == '\t') ++p;
    }
}

void AndroidEngine::loadStartScreen() {
    std::string json = AndroidFileIO::readString("start_screen.json");
    if (json.empty()) {
        LOGI("No start_screen.json found in assets");
        return;
    }

    // Background
    size_t bgPos = json.find("\"background\"");
    if (bgPos != std::string::npos) {
        m_startBgPath = jsonString(json, "file", bgPos);
    }

    // Logo block
    size_t logoPos = json.find("\"logo\"");
    if (logoPos != std::string::npos) {
        std::string logoType = jsonString(json, "type", logoPos);
        m_startLogoIsImage   = (logoType == "image");
        m_startTitleText     = jsonString(json, "text", logoPos);
        m_startLogoImagePath = jsonString(json, "imageFile", logoPos);

        size_t posPos = json.find("\"position\"", logoPos);
        if (posPos != std::string::npos) {
            m_startTitlePos.x = static_cast<float>(jsonDouble(json, "x", posPos));
            m_startTitlePos.y = static_cast<float>(jsonDouble(json, "y", posPos));
        }
        m_startTitleFontPx = static_cast<float>(jsonDouble(json, "fontSize", logoPos));
        if (m_startTitleFontPx <= 0.f) m_startTitleFontPx = 72.f;

        float c[4] = {1, 0.9f, 0.25f, 1.f};
        jsonNumArray(json, "color", c, 4, logoPos);
        m_startTitleColor = ImVec4(c[0], c[1], c[2], c[3]);
    }

    // Tap prompt
    m_startTapText = jsonString(json, "tapText");
    size_t tapPos = json.find("\"tapTextPosition\"");
    if (tapPos != std::string::npos) {
        m_startTapPos.x = static_cast<float>(jsonDouble(json, "x", tapPos));
        m_startTapPos.y = static_cast<float>(jsonDouble(json, "y", tapPos));
    }
    m_startTapFontPx = static_cast<float>(jsonDouble(json, "tapTextSize"));
    if (m_startTapFontPx <= 0.f) m_startTapFontPx = 24.f;

    LOGI("StartScreen loaded: title='%s' bg='%s'",
         m_startTitleText.c_str(), m_startBgPath.c_str());
}

void AndroidEngine::loadProject() {
    std::string json = AndroidFileIO::readString("music_selection.json");
    if (json.empty()) {
        LOGI("No music_selection.json found in assets");
        return;
    }

    // Top-level page background and badge images.
    m_musicBgPath  = jsonString(json, "background");
    m_fcImagePath  = jsonString(json, "fcImage");
    m_apImagePath  = jsonString(json, "apImage");

    // Walk through each song object in the JSON
    // Find each "songs" array, then parse individual song objects
    size_t pos = 0;
    while (true) {
        // Find next song object by looking for "audioFile"
        size_t songPos = json.find("\"audioFile\"", pos);
        if (songPos == std::string::npos) break;

        // Find the enclosing object braces for this song
        // Search backwards for '{'
        size_t objStart = json.rfind('{', songPos);
        // Search forwards for matching '}' (count braces)
        int depth = 0;
        size_t objEnd = objStart;
        for (size_t i = objStart; i < json.size(); ++i) {
            if (json[i] == '{') depth++;
            else if (json[i] == '}') { depth--; if (depth == 0) { objEnd = i + 1; break; } }
        }
        std::string songJson = json.substr(objStart, objEnd - objStart);

        SongEntry entry;
        entry.name       = jsonString(songJson, "name");
        entry.artist     = jsonString(songJson, "artist");
        entry.audioFile  = jsonString(songJson, "audioFile");
        entry.chartPath  = jsonString(songJson, "chartHard");
        if (entry.chartPath.empty())
            entry.chartPath = jsonString(songJson, "chartMedium");
        if (entry.chartPath.empty())
            entry.chartPath = jsonString(songJson, "chartEasy");
        entry.score      = jsonInt(songJson, "score");
        entry.coverImage = jsonString(songJson, "coverImage");
        // Pick the strongest achievement across difficulties (AP > FC > "")
        std::string a = jsonString(songJson, "achievementHard");
        if (a != "AP") {
            std::string m = jsonString(songJson, "achievementMedium");
            if (m == "AP" || (a.empty() && !m.empty())) a = m;
            std::string e = jsonString(songJson, "achievementEasy");
            if (e == "AP" || (a.empty() && !e.empty())) a = e;
        }
        entry.achievement = a;

        // Parse gameMode
        size_t gmPos = songJson.find("\"gameMode\"");
        if (gmPos != std::string::npos) {
            // Find the gameMode object
            size_t gmObjStart = songJson.find('{', gmPos);
            int gd = 0;
            size_t gmObjEnd = gmObjStart;
            for (size_t i = gmObjStart; i < songJson.size(); ++i) {
                if (songJson[i] == '{') gd++;
                else if (songJson[i] == '}') { gd--; if (gd == 0) { gmObjEnd = i + 1; break; } }
            }
            std::string gmJson = songJson.substr(gmObjStart, gmObjEnd - gmObjStart);

            std::string typeStr = jsonString(gmJson, "type");
            if (typeStr == "dropNotes")     entry.gameMode.type = GameModeType::DropNotes;
            else if (typeStr == "circle")   entry.gameMode.type = GameModeType::Circle;
            else if (typeStr == "scanLine") entry.gameMode.type = GameModeType::ScanLine;
            else                            entry.gameMode.type = GameModeType::DropNotes;

            std::string dimStr = jsonString(gmJson, "dimension");
            entry.gameMode.dimension = (dimStr == "3D") ? DropDimension::ThreeD : DropDimension::TwoD;

            entry.gameMode.trackCount   = jsonInt(gmJson, "trackCount");
            if (entry.gameMode.trackCount == 0) entry.gameMode.trackCount = 4;
            entry.gameMode.audioOffset  = static_cast<float>(jsonDouble(gmJson, "audioOffset"));
            entry.gameMode.perfectMs    = static_cast<float>(jsonDouble(gmJson, "perfectMs"));
            entry.gameMode.goodMs       = static_cast<float>(jsonDouble(gmJson, "goodMs"));
            entry.gameMode.badMs        = static_cast<float>(jsonDouble(gmJson, "badMs"));
            entry.gameMode.perfectScore = jsonInt(gmJson, "perfectScore");
            entry.gameMode.goodScore    = jsonInt(gmJson, "goodScore");
            entry.gameMode.badScore     = jsonInt(gmJson, "badScore");
            if (entry.gameMode.perfectMs == 0.f) entry.gameMode.perfectMs = 50.f;
            if (entry.gameMode.goodMs == 0.f)    entry.gameMode.goodMs = 100.f;
            if (entry.gameMode.badMs == 0.f)     entry.gameMode.badMs = 150.f;
            if (entry.gameMode.perfectScore == 0) entry.gameMode.perfectScore = 1000;
            if (entry.gameMode.goodScore == 0)    entry.gameMode.goodScore = 600;
            if (entry.gameMode.badScore == 0)     entry.gameMode.badScore = 200;
        }

        if (!entry.name.empty() && !entry.audioFile.empty()) {
            LOGI("Song: %s by %s [%s]", entry.name.c_str(), entry.artist.c_str(), entry.chartPath.c_str());
            m_songs.push_back(std::move(entry));
        }

        pos = objEnd;
    }
    LOGI("Project loaded with %d songs", (int)m_songs.size());
}

// Lazy load: extract asset → upload to Vulkan → wrap as ImGui descriptor.
// Returns nullptr on failure. The descriptor handle is valid until shutdown.
ImTextureID AndroidEngine::loadAssetTexture(const std::string& assetPath) {
    if (assetPath.empty() || !m_vulkanReady) return (ImTextureID)0;
    auto it = m_imguiTextures.find(assetPath);
    if (it != m_imguiTextures.end()) return it->second;

    std::string fsPath = AndroidFileIO::extractToInternal(assetPath);
    if (fsPath.empty()) {
        LOGE("Texture asset missing: %s", assetPath.c_str());
        m_imguiTextures[assetPath] = (ImTextureID)0;
        return (ImTextureID)0;
    }

    Texture tex;
    try {
        tex = m_renderer.textures().loadFromFile(m_renderer.context(),
                                                 m_renderer.buffers(),
                                                 fsPath);
    } catch (const std::exception& e) {
        LOGE("loadFromFile failed for %s: %s", assetPath.c_str(), e.what());
        m_imguiTextures[assetPath] = (ImTextureID)0;
        return (ImTextureID)0;
    }

    VkDescriptorSet desc = ImGui_ImplVulkan_AddTexture(
        tex.sampler, tex.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ImTextureID id = reinterpret_cast<ImTextureID>(desc);
    m_imguiTextures[assetPath] = id;
    m_loadedTextures.push_back(tex);
    LOGI("Texture loaded: %s (%dx%d)", assetPath.c_str(), tex.width, tex.height);
    return id;
}

void AndroidEngine::releaseTextures() {
    if (!m_vulkanReady) return;
    for (auto& tex : m_loadedTextures) {
        m_renderer.textures().destroyTexture(m_renderer.context(), tex);
    }
    m_loadedTextures.clear();
    m_imguiTextures.clear();
}

// Dark theme close to the desktop editor: near-black panels, cyan primary,
// rounded buttons.
void AndroidEngine::applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding   = 8.0f;
    s.FrameRounding    = 6.0f;
    s.GrabRounding     = 6.0f;
    s.PopupRounding    = 6.0f;
    s.ScrollbarRounding= 6.0f;
    s.WindowPadding    = ImVec2(16, 16);
    s.FramePadding     = ImVec2(12, 8);
    s.ItemSpacing      = ImVec2(10, 10);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = ImVec4(0.04f, 0.05f, 0.07f, 0.92f);
    c[ImGuiCol_ChildBg]         = ImVec4(0.06f, 0.07f, 0.09f, 0.85f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.06f, 0.07f, 0.09f, 0.96f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.10f, 0.12f, 0.15f, 0.85f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.16f, 0.20f, 0.26f, 0.95f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.20f, 0.55f, 0.65f, 0.90f);
    c[ImGuiCol_TitleBg]         = ImVec4(0.05f, 0.06f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.08f, 0.10f, 0.13f, 1.00f);
    c[ImGuiCol_Button]          = ImVec4(0.13f, 0.16f, 0.20f, 0.92f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.18f, 0.50f, 0.62f, 0.95f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.85f, 0.20f, 0.55f, 0.95f);
    c[ImGuiCol_Header]          = ImVec4(0.18f, 0.40f, 0.50f, 0.50f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.20f, 0.55f, 0.65f, 0.85f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.85f, 0.20f, 0.55f, 0.85f);
    c[ImGuiCol_Border]          = ImVec4(0.20f, 0.55f, 0.65f, 0.30f);
    c[ImGuiCol_Text]            = ImVec4(0.92f, 0.94f, 0.96f, 1.00f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.55f, 0.58f, 0.62f, 1.00f);
    c[ImGuiCol_Separator]       = ImVec4(0.20f, 0.55f, 0.65f, 0.40f);
}

void AndroidEngine::startGameplay(int songIndex) {
    if (songIndex < 0 || songIndex >= (int)m_songs.size()) return;
    auto& song = m_songs[songIndex];

    // Load chart
    std::string chartFs = AndroidFileIO::extractToInternal(song.chartPath);
    m_currentChart = ChartLoader::load(chartFs);

    // Create renderer
    m_activeMode = createRenderer(song.gameMode);
    m_activeMode->onInit(m_renderer, m_currentChart, &song.gameMode);
    m_hitDetector.init(m_currentChart);
    m_judgment.reset();
    m_score.reset();
    m_activeTouches.clear();

    // Lead-in
    float leadIn = 2.0f;
    m_clock.setSongTime(-leadIn - song.gameMode.audioOffset);
    m_gameplayLeadIn = leadIn + song.gameMode.audioOffset;
    m_audioStarted = false;
    m_pendingAudioPath = song.audioFile;
    m_showResults = false;
    m_gameplayPaused = false;

    // Apply player settings to the freshly-created renderer / hit detector.
    applyPlayerSettings();

    m_screen = GameScreen::Gameplay;
    LOGI("Starting gameplay: %s", song.name.c_str());
}

void AndroidEngine::exitGameplay() {
    m_audio.stop();
    if (m_activeMode) {
        m_activeMode->onShutdown(m_renderer);
        m_activeMode.reset();
    }
    m_activeTouches.clear();
    m_showResults = false;
    m_gameplayPaused = false;
    m_screen = GameScreen::MusicSelection;
}

void AndroidEngine::loadPlayerSettingsFile() {
    m_settingsPath = AndroidFileIO::internalPath() + "player_settings.json";
    if (!loadPlayerSettings(m_settingsPath, m_playerSettings)) {
        LOGI("No player_settings.json yet - using defaults");
    } else {
        LOGI("Loaded player settings from %s", m_settingsPath.c_str());
    }
}

void AndroidEngine::savePlayerSettingsFile() {
    if (m_settingsPath.empty())
        m_settingsPath = AndroidFileIO::internalPath() + "player_settings.json";
    if (!savePlayerSettings(m_settingsPath, m_playerSettings))
        LOGE("Failed to save player settings to %s", m_settingsPath.c_str());
}

void AndroidEngine::applyPlayerSettings() {
    m_audio.setMusicVolume(m_playerSettings.musicVolume);
    m_audio.setSfxVolume(m_playerSettings.hitSoundVolume);
    m_audio.setHitSoundEnabled(m_playerSettings.hitSoundEnabled);

    // Convert ms → seconds for the hit detector.
    m_hitDetector.setAudioOffset(m_playerSettings.audioOffsetMs / 1000.f);

    // Note speed slider 1..10 maps to multiplier slider/5 (5 = 1.0x).
    const float mul = m_playerSettings.noteSpeed / 5.f;
    if (m_activeMode) m_activeMode->setNoteSpeedMultiplier(mul);
}

std::unique_ptr<GameModeRenderer> AndroidEngine::createRenderer(const GameModeConfig& config) {
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
