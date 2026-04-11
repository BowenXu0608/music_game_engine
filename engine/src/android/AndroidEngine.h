// ============================================================================
// AndroidEngine — Game loop and state management for Android
// Replaces the desktop Engine class. Uses Renderer (with Android Vulkan/Swapchain
// implementations), InputManager, and all shared game logic.
// ============================================================================
#pragma once
#include "renderer/Renderer.h"
#include "input/InputManager.h"
#include "engine/GameClock.h"
#include "engine/AudioEngine.h"
#include "gameplay/HitDetector.h"
#include "gameplay/JudgmentSystem.h"
#include "gameplay/ScoreTracker.h"
#include "game/chart/ChartLoader.h"
#include "game/chart/ChartTypes.h"
#include "ui/ProjectHub.h"  // for GameModeConfig, GameModeType, DropDimension
#include "game/modes/GameModeRenderer.h"
#include "game/modes/BandoriRenderer.h"
#include "game/modes/CytusRenderer.h"
#include "game/modes/PhigrosRenderer.h"
#include "game/modes/ArcaeaRenderer.h"
#include "game/modes/LanotaRenderer.h"

#include <android_native_app_glue.h>
#include <memory>
#include <string>
#include <unordered_map>

struct ANativeWindow;

// Forward declare the global window setter (defined in AndroidVulkanContext.cpp)
extern void androidSetNativeWindow(ANativeWindow* win);
extern void androidSetSwapchainWindow(ANativeWindow* win);

enum class GameScreen { StartScreen, MusicSelection, Gameplay, Results };

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

    void loadProject();
    void loadStartScreen();
    void startGameplay(int songIndex);
    void exitGameplay();

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
    // Start screen content (parsed from start_screen.json)
    std::string    m_startTitleText;
    std::string    m_startTapText;
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

    // Music selection state
    struct SongEntry {
        std::string name;
        std::string artist;
        std::string audioFile;
        std::string chartPath;
        GameModeConfig gameMode;
        int score = 0;
    };
    std::vector<SongEntry> m_songs;
    int m_selectedSong = 0;
};
