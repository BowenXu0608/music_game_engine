// ============================================================================
// AndroidEngine — Game loop and state management for Android
// Replaces the desktop Engine class. Uses Renderer (with Android Vulkan/Swapchain
// implementations), InputManager, and all shared game logic.
// ============================================================================
#pragma once
#include "renderer/Renderer.h"
#include "renderer/MaterialAssetLibrary.h"
#include "input/InputManager.h"
#include "engine/GameClock.h"
#include "engine/AudioEngine.h"
#include "gameplay/HitDetector.h"
#include "gameplay/JudgmentSystem.h"
#include "gameplay/ScoreTracker.h"
#include "game/chart/ChartLoader.h"
#include "game/chart/ChartTypes.h"
#include "game/PlayerSettings.h"
#include "game/screens/StartScreenView.h"
#include "game/screens/MusicSelectionView.h"
#include "game/screens/GameplayHudView.h"
#include "game/screens/ResultsView.h"
#include "AndroidEngineAdapter.h"
#include "ui/ProjectHub.h"  // for GameModeConfig, GameModeType, DropDimension
#include "game/modes/GameModeRenderer.h"
#include "game/modes/BandoriRenderer.h"
#include "game/modes/CytusRenderer.h"
#include "game/modes/PhigrosRenderer.h"
#include "game/modes/ArcaeaRenderer.h"
#include "game/modes/LanotaRenderer.h"

#include "renderer/vulkan/TextureManager.h"
#include <imgui.h>
#include <android_native_app_glue.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct ANativeWindow;

// Forward declare the global window setter (defined in AndroidVulkanContext.cpp)
extern void androidSetNativeWindow(ANativeWindow* win);
extern void androidSetSwapchainWindow(ANativeWindow* win);

enum class GameScreen { StartScreen, MusicSelection, Settings, Gameplay, Results };

class AndroidEngine {
public:
    void init(android_app* app, const std::string& shaderDir);
    void shutdown();
    void mainLoop();

    // Called from android_main event handlers
    void onWindowInit(ANativeWindow* window);
    void onWindowTerm();
    void onWindowResize();
    void onTouchEvent(AInputEvent* event);

    bool isRunning() const { return m_running; }

private:
    void update(float dt);
    void render();
    void renderStartScreen();
    void renderGameplayHUD();
    void renderResultsHUD();
    void renderMusicSelection();
    void renderSettings();

    void loadProject();
    void loadStartScreen();
    void loadPlayerSettingsFile();   // reads player_settings.json + applies live
    void savePlayerSettingsFile();
    void applyPlayerSettings();      // push values into audio + active renderer
    void startGameplay(int songIndex, bool autoPlay = false);
    void exitGameplay();
    void togglePause();
    void requestStop();
    void restartGameplay();
    void renderPauseOverlay();

    std::unique_ptr<GameModeRenderer> createRenderer(const GameModeConfig& config);

    android_app*   m_app = nullptr;
    bool           m_running = false;
    bool           m_vulkanReady = false;
    std::string    m_shaderDir;
    std::string    m_assetsPath;  // internal storage path for extracted assets

    // Vulkan renderer (uses Android VulkanContext/Swapchain implementations)
    Renderer       m_renderer;

    // Game state
    GameScreen     m_screen = GameScreen::StartScreen;

    // ── Start screen content (parsed from start_screen.json) ────────────────
    std::string    m_startTitleText;
    std::string    m_startTapText;
    std::string    m_startBgPath;
    std::string    m_startLogoImagePath;
    bool           m_startLogoIsImage = false;
    ImVec2         m_startTitlePos    = {0.5f, 0.30f};   // normalized
    ImVec2         m_startTapPos      = {0.5f, 0.80f};
    float          m_startTitleFontPx = 72.0f;
    float          m_startTapFontPx   = 24.0f;
    ImVec4         m_startTitleColor  = {1.0f, 0.9f, 0.25f, 1.0f};

    // ── Music selection content ─────────────────────────────────────────────
    std::string    m_musicBgPath;
    std::string    m_fcImagePath;
    std::string    m_apImagePath;

    // ── ImGui texture cache (asset path → ImGui descriptor) ─────────────────
    // The Texture structs are kept alive for cleanup on shutdown.
    std::unordered_map<std::string, ImTextureID> m_imguiTextures;
    std::vector<Texture> m_loadedTextures;

    // ImGui descriptor wrapping the offscreen scene image, used to blit it
    // full-screen during gameplay (desktop SceneViewer equivalent).
    VkDescriptorSet m_sceneTexSet = VK_NULL_HANDLE;

    // Cached device DPI scale for HUD text + FPS counter sizing.
    float m_dpiScale = 1.f;

    // Helpers
    ImTextureID loadAssetTexture(const std::string& assetPath);
    void        releaseTextures();
    void        applyTheme();
    GameClock      m_clock;
    AudioEngine    m_audio;
    InputManager   m_input;
    HitDetector    m_hitDetector;
    JudgmentSystem m_judgment;
    ScoreTracker   m_score;

    std::unique_ptr<GameModeRenderer> m_activeMode;
    ChartData      m_currentChart;
    GameModeConfig m_gameplayConfig;
    std::unordered_map<int32_t, uint32_t> m_activeTouches;

    // Gameplay timing
    float  m_gameplayLeadIn = 0.f;
    bool   m_audioStarted = false;
    std::string m_pendingAudioPath;
    bool   m_showResults = false;
    bool   m_gameplayPaused = false;
    bool   m_autoPlay = false;    // honoured by autoPlayTick path in update()
    int    m_lastSongIndex = -1;  // for Restart from pause overlay

    // Music selection state
    struct SongEntry {
        std::string name;
        std::string artist;
        std::string audioFile;
        std::string chartPath;
        std::string coverImage;
        std::string achievement;          // "FC", "AP", or empty
        GameModeConfig gameMode;
        int score = 0;
    };
    std::vector<SongEntry> m_songs;
    int m_selectedSong = 0;

    // Player-facing runtime settings (persisted to player_settings.json in
    // app internal storage). See game/PlayerSettings.h.
    PlayerSettings m_playerSettings;
    std::string    m_settingsPath;

    // Per-APK material asset library — populated at init from bundled assets.
    MaterialAssetLibrary m_materialLibrary;

    // Shared player views (game-side, also used by desktop preview). Each is
    // driven through m_adapter so they never see AndroidEngine directly.
    StartScreenView      m_startView;
    MusicSelectionView   m_musicView;
    GameplayHudView      m_hudView;
    ResultsView          m_resultsView;

    // Adapter that exposes AndroidEngine as IPlayerEngine to the views.
    AndroidEngineAdapter m_adapter{*this};

    // Lazy-init flag: views call initVulkan + load on first render after the
    // project assets are extracted to internal storage.
    bool m_viewsReady = false;

    friend class AndroidEngineAdapter;
};
