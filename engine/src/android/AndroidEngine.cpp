// ============================================================================
// AndroidEngine Implementation
// ============================================================================
#include "AndroidEngine.h"
#include "AndroidFileIO.h"
#include "input/ScreenMetrics.h"
#include "ui/SettingsPageUI.h"
#include <android/log.h>
#include <android/input.h>
#include <android/configuration.h>
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
        double t = m_clock.songTime();

        // Mirror the desktop Engine's per-mode dispatch. checkHitPosition()
        // (the old single path) only matches Arcaea-style position notes, so
        // lane-based modes (Bandori 2D/3D drop, etc.) never registered taps
        // and never began holds.
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

    loadStartScreen();
    loadProject();
    loadPlayerSettingsFile();
    applyPlayerSettings();

    // Eager-unpack: extract music_selection.json + start_screen.json and
    // every file path they reference so the shared View classes can fopen
    // them via standard filesystem calls. Vulkan can't read directly from
    // inside the APK; everything the player views render must live on disk.
    {
        AndroidFileIO::extractToInternal("music_selection.json");
        AndroidFileIO::extractToInternal("start_screen.json");

        auto extractRefsIn = [](const std::string& json) {
            // Walk every quoted "...":" ..." pair; if the right-hand value
            // looks like a relative file path with an extension and the file
            // exists in the APK, materialize it to internal storage.
            size_t pos = 0;
            while (pos < json.size()) {
                size_t colon = json.find(':', pos);
                if (colon == std::string::npos) break;
                size_t q1 = json.find('"', colon + 1);
                if (q1 == std::string::npos) break;
                // Make sure no '\n' between ':' and the opening quote (we want
                // a string-valued field, not a key on the next line).
                size_t nl = json.find('\n', colon);
                if (nl != std::string::npos && nl < q1) { pos = q1; continue; }
                size_t q2 = json.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                std::string v = json.substr(q1 + 1, q2 - q1 - 1);
                if (!v.empty() &&
                    v.find('/') != std::string::npos &&
                    v.find('.') != std::string::npos &&
                    v.find('\\') == std::string::npos &&
                    AndroidFileIO::exists(v)) {
                    AndroidFileIO::extractToInternal(v);
                }
                pos = q2 + 1;
            }
        };

        std::string ms = AndroidFileIO::readString("music_selection.json");
        if (!ms.empty()) extractRefsIn(ms);
        std::string ss = AndroidFileIO::readString("start_screen.json");
        if (!ss.empty()) extractRefsIn(ss);

        m_assetsPath = AndroidFileIO::internalPath();
        if (!m_assetsPath.empty() && m_assetsPath.back() == '/')
            m_assetsPath.pop_back();
        LOGI("Eager unpack done; assets root = %s", m_assetsPath.c_str());
    }

    // Material asset library. Charts reference materials by asset *name*
    // ({slot, assetName}), so the JSON eager-unpack above (which only sees
    // quoted path-like values) never extracts the .mat files — extract the
    // whole dir explicitly, then load it like desktop Engine::openProject.
    // Seed/migrate are editor-only; build_apk.bat ships pre-migrated charts
    // and the full assets/materials/ tree, so a plain load is sufficient.
    AndroidFileIO::extractDirToInternal("assets/materials");
    m_materialLibrary.loadFromProject(m_assetsPath);

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
        "quad_pbr.vert.spv", "quad_pbr.frag.spv",
        "line.vert.spv", "line.frag.spv",
        "mesh.vert.spv", "mesh_pbr.frag.spv",
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

    // DPI scaling: query device density and rebuild the font atlas at the
    // matching pixel size so glyphs are crisp on high-DPI phones, and scale
    // ImGui's padding/rounding so widgets aren't physically tiny.
    float dpiScale = 1.0f;
    if (m_app && m_app->config) {
        int32_t density = AConfiguration_getDensity(m_app->config);
        if (density > 0) {
            dpiScale = (float)density / 160.0f;
            if (dpiScale < 1.0f) dpiScale = 1.0f;  // floor at mdpi
            if (dpiScale > 4.0f) dpiScale = 4.0f;  // sanity cap
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * dpiScale;
        io.Fonts->AddFontDefault(&cfg);
    }
    ImGui::GetStyle().ScaleAllSizes(dpiScale);
    TouchThresholds::scaleByDpi(dpiScale);
    m_hudView.setUiScale(dpiScale);
    m_dpiScale = dpiScale;
    LOGI("DPI scale = %.2f (density bucket = %d)",
         dpiScale,
         (m_app && m_app->config) ? AConfiguration_getDensity(m_app->config) : -1);

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

    // Wrap the offscreen scene image as an ImGui texture so we can blit it
    // full-screen during gameplay. The renderer never composites the scene
    // to the swapchain itself — desktop draws it via ImGui::Image, and we
    // do the same here. Without this, gameplay renders to an unseen
    // framebuffer and only HUD overlays appear.
    refreshSceneTexture();

    m_vulkanReady = true;
    LOGI("Vulkan renderer ready: %dx%d", m_renderer.width(), m_renderer.height());
}

void AndroidEngine::refreshSceneTexture() {
    if (m_sceneTexSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_sceneTexSet);
        m_sceneTexSet = VK_NULL_HANDLE;
    }
    m_sceneTexSet = ImGui_ImplVulkan_AddTexture(
        m_renderer.postProcess().bloomSampler(),
        m_renderer.sceneImageView(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

    // PostProcess::resize destroyed + recreated the scene image and view, so
    // the cached ImGui descriptor now points at a freed handle (black/garbage
    // gameplay scene, or a GPU fault on stricter drivers). Rebind it.
    refreshSceneTexture();
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

        // Auto play: consume every note whose time has arrived and dispatch
        // Perfect hits. Runs before the miss sweep so nothing decays to Miss.
        // Mirrors Engine::update on desktop.
        if (m_autoPlay) {
            auto autoHits = m_hitDetector.autoPlayTick(songT);
            for (auto& ah : autoHits) {
                m_judgment.recordJudgment(Judgment::Perfect);
                m_score.onJudgment(Judgment::Perfect);
                if (ah.lane >= 0)
                    m_activeMode->showJudgment(ah.lane, Judgment::Perfect);
            }
        }

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
        // Sync active-hold ids so BandoriRenderer keeps drawing the hold body
        // (and lights up the active glow). Without this, holds disappear the
        // instant their head crosses the judgement line — even with autoplay,
        // because the renderer's m_activeHoldIds stays empty and the stale-hold
        // cull (note.time + kBadWindow) trips at 0.15s past head.
        m_activeMode->setActiveHoldIds(m_hitDetector.activeHoldIds());
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
        refreshSceneTexture();   // scene image view was recreated
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

    // Blit the offscreen scene image full-screen. Renderer::endFrame leaves
    // the swapchain pass open and never composites — desktop pulls the scene
    // texture in via ImGui::Image too. Without this the entire playfield
    // (lanes, judgment line, notes, bg) is invisible and only HUD shows.
    if (m_sceneTexSet != VK_NULL_HANDLE) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(displaySz);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##gameplay_scene", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs);
        ImGui::Image((ImTextureID)m_sceneTexSet, displaySz);
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Background dim overlay (under the HUD, over the rendered scene).
    if (m_playerSettings.backgroundDim > 0.f) {
        const float a = std::min(m_playerSettings.backgroundDim, 1.f) * 0.75f;
        ImU32 dim = IM_COL32(0, 0, 0, (int)(a * 255.f));
        ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(0, 0), displaySz, dim);
    }

    // FPS counter (Android-only; not part of the shared HUD view).
    if (m_playerSettings.fpsCounter) {
        ImGui::SetNextWindowPos(ImVec2(10 * m_dpiScale, 10 * m_dpiScale));
        ImGui::SetNextWindowSize(ImVec2(80 * m_dpiScale, 28 * m_dpiScale));
        ImGui::Begin("##fps", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(0.2f, 1.f, 0.4f, 1.f),
                           "%.0f fps", ImGui::GetIO().Framerate);
        ImGui::End();
    }

    m_hudView.render(displaySz, m_adapter);

    if (m_gameplayPaused) {
        renderPauseOverlay();
    }
}

void AndroidEngine::renderResultsHUD() {
    m_resultsView.render(ImGui::GetIO().DisplaySize, m_adapter);
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
    if (!m_viewsReady && !m_assetsPath.empty()) {
        m_startView.initVulkan(m_renderer.context(), m_renderer.buffers(), nullptr);
        m_musicView.initVulkan(m_renderer.context(), m_renderer.buffers(), nullptr);
        m_startView.load(m_assetsPath);
        m_musicView.load(m_assetsPath);
        m_viewsReady = true;
    }

    ImVec2 displaySz = ImGui::GetIO().DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySz);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##startscreen", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    m_startView.renderGamePreview(origin, displaySz);

    if (ImGui::InvisibleButton("##startTap", displaySz)) {
        m_screen = GameScreen::MusicSelection;
        LOGI("StartScreen tapped -> MusicSelection");
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void AndroidEngine::renderMusicSelection() {
    if (!m_viewsReady && !m_assetsPath.empty()) {
        m_startView.initVulkan(m_renderer.context(), m_renderer.buffers(), nullptr);
        m_musicView.initVulkan(m_renderer.context(), m_renderer.buffers(), nullptr);
        m_startView.load(m_assetsPath);
        m_musicView.load(m_assetsPath);
        m_viewsReady = true;
    }

    ImVec2 displaySz = ImGui::GetIO().DisplaySize;
    float dt = ImGui::GetIO().DeltaTime;
    m_musicView.update(dt, &m_adapter);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySz);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##musicsel", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    ImVec2 origin = ImGui::GetCursorScreenPos();
    m_musicView.renderGamePreview(origin, displaySz, &m_adapter);

    ImGui::End();
    ImGui::PopStyleVar();
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

void AndroidEngine::startGameplay(int songIndex, bool autoPlay) {
    if (songIndex < 0 || songIndex >= (int)m_songs.size()) return;
    m_lastSongIndex = songIndex;
    m_autoPlay = autoPlay;
    auto& song = m_songs[songIndex];

    // Load chart
    std::string chartFs = AndroidFileIO::extractToInternal(song.chartPath);
    m_currentChart = ChartLoader::load(chartFs);

    // Create renderer
    m_activeMode = createRenderer(song.gameMode);
    // Must precede onInit: renderers resolve chart.materials assetNames
    // through the library during onInit. Without this the library pointer is
    // null and every asset-referenced material falls back to a hardcoded
    // default. Mirrors Engine::setMode on desktop.
    m_activeMode->setMaterialLibrary(&m_materialLibrary);
    m_activeMode->onInit(m_renderer, m_currentChart, &song.gameMode);
    m_hitDetector.init(m_currentChart);
    m_judgment.reset();
    m_score.reset();
    m_activeTouches.clear();

    // Cache the launched mode so engine.gameplayConfig() returns this song's
    // HUD layout. Without this, scoreHud and comboHud read default-constructed
    // {0.5, 0.5} positions and stack their numbers on top of each other in
    // the centre of the screen — the "bloom on combo" the user sees.
    m_gameplayConfig = song.gameMode;

    // Lead-in
    float leadIn = 2.0f;
    m_clock.setSongTime(-leadIn - song.gameMode.audioOffset);
    m_gameplayLeadIn = leadIn + song.gameMode.audioOffset;
    m_audioStarted = false;
    m_pendingAudioPath = song.audioFile;
    m_showResults = false;
    m_gameplayPaused = false;

    // togglePause()→exitGameplay leaves the clock paused. Without resuming
    // here, the next startGameplay call sets songTime=-leadIn but tick()
    // returns 0 forever, so audio never starts and the screen looks frozen on
    // the music-selection page. Matches Engine::launchGameplay on desktop.
    m_clock.resume();

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

void AndroidEngine::togglePause() {
    m_gameplayPaused = !m_gameplayPaused;
    if (m_gameplayPaused) {
        m_audio.pause();
        m_clock.pause();
    } else {
        m_audio.resume();
        m_clock.resume();
    }
}

void AndroidEngine::requestStop() {
    // Esc-equivalent for the on-screen Stop button: from results we exit,
    // from active gameplay we toggle pause. Matches Engine::requestStop on
    // desktop so the shared HUD has identical semantics on both targets.
    if (m_screen != GameScreen::Gameplay && m_screen != GameScreen::Results) return;
    if (m_showResults) exitGameplay();
    else               togglePause();
}

void AndroidEngine::restartGameplay() {
    if (m_lastSongIndex < 0) return;
    int idx = m_lastSongIndex;
    bool ap = m_autoPlay;
    exitGameplay();
    startGameplay(idx, ap);
}

void AndroidEngine::renderPauseOverlay() {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;

    // Dim layer
    ImGui::GetForegroundDrawList()->AddRectFilled(
        ImVec2(0, 0), displaySz, IM_COL32(0, 0, 0, 160));

    // Centered pause menu, DPI-scaled
    const float ui = m_dpiScale;
    ImVec2 menuSize(260.f * ui, 220.f * ui);
    ImGui::SetNextWindowPos(ImVec2((displaySz.x - menuSize.x) * 0.5f,
                                    (displaySz.y - menuSize.y) * 0.5f));
    ImGui::SetNextWindowSize(menuSize);
    ImGui::Begin("Paused", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    float btnH = 48.f * ui;
    if (ImGui::Button("Resume",  ImVec2(-1, btnH))) togglePause();
    ImGui::Spacing();
    if (ImGui::Button("Restart", ImVec2(-1, btnH))) restartGameplay();
    ImGui::Spacing();
    if (ImGui::Button("Exit",    ImVec2(-1, btnH))) exitGameplay();

    ImGui::End();
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
            // Match desktop Engine::createRenderer: Circle is always the
            // Lanota rotating-disk renderer (the dimension toggle is not
            // exposed for this mode in the editor).
            return std::make_unique<LanotaRenderer>();
        case GameModeType::ScanLine:
            // Match desktop: ScanLine = Cytus sweep-line renderer. Mapping
            // this to PhigrosRenderer (a stubbed mode) is why Scan Line was
            // unplayable on Android. CytusRenderer.cpp is shared, so the
            // legacy-flat note-draw fix applies here too.
            return std::make_unique<CytusRenderer>();
    }
    return std::make_unique<BandoriRenderer>();
}

// ============================================================================
// Gameplay gesture handlers — faithful ports of the desktop Engine handlers
// (Engine.cpp). Same logic, minus the desktop std::cout score logging. Keeping
// these byte-for-byte equivalent to the desktop is deliberate: any divergence
// reintroduces the Android-only input/hold drift this fix removes.
// ============================================================================

void AndroidEngine::dispatchHitResult(const HitResult& hit, int lane) {
    auto judgment = m_judgment.judge(hit.timingDelta);
    m_judgment.recordJudgment(judgment);
    m_score.onJudgment(judgment);
    if (m_activeMode)
        m_activeMode->showJudgment(lane >= 0 ? lane : 0, judgment);
}

void AndroidEngine::handleGestureLaneBased(const GestureEvent& evt, double songTime) {
    int screenW = static_cast<int>(m_renderer.width());
    int tc = m_gameplayConfig.trackCount;
    int lane = static_cast<int>(evt.pos.x / static_cast<float>(screenW) * tc);
    lane = std::clamp(lane, 0, tc - 1);

    switch (evt.type) {
        case GestureType::Tap: {
            auto hit = m_hitDetector.checkHit(lane, songTime);
            if (hit) dispatchHitResult(*hit, lane);
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
                    dispatchHitResult(*hit, lane);
                }
            }
            break;
        }
        case GestureType::HoldBegin: {
            auto noteId = m_hitDetector.beginHold(lane, songTime);
            if (noteId) {
                m_activeTouches[evt.touchId] = *noteId;
                HitResult headHit{*noteId, 0.f, NoteType::Hold};
                dispatchHitResult(headHit, lane);
            }
            break;
        }
        case GestureType::SlideBegin:
        case GestureType::SlideMove: {
            auto it = m_activeTouches.find(evt.touchId);
            if (it != m_activeTouches.end())
                m_hitDetector.updateHoldLane(it->second, lane);
            for (auto& dh : m_hitDetector.consumeDrags(lane, songTime))
                dispatchHitResult(dh, lane);
            break;
        }
        case GestureType::HoldEnd:
        case GestureType::SlideEnd: {
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

void AndroidEngine::handleGestureArcaea(const GestureEvent& evt, double songTime) {
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

void AndroidEngine::handleGesturePhigros(const GestureEvent& evt, double songTime) {
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

void AndroidEngine::handleGestureCircle(LanotaRenderer& lan,
                                        const GestureEvent& evt, double songTime) {
    constexpr float CIRCLE_PICK_DP = 48.f;
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

void AndroidEngine::handleGestureScanLine(CytusRenderer& cyt,
                                          const GestureEvent& evt, double songTime) {
    constexpr float SCAN_PICK_DP = 48.f;
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
