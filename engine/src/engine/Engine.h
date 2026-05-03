#pragma once
#include "renderer/Renderer.h"
#include "renderer/MaterialAssetLibrary.h"
#include "renderer/vulkan/TextureManager.h"
#include "game/modes/GameModeRenderer.h"
#include "game/screens/GameplayHudView.h"
#include "game/screens/ResultsView.h"
#include "engine/GameClock.h"
#include "engine/AudioEngine.h"
#include "engine/IPlayerEngine.h"
#include "ui/ImGuiLayer.h"
#include "ui/SceneViewer.h"
#include "ui/ProjectHub.h"
#include "ui/StartScreenEditor.h"
#include "ui/MusicSelectionEditor.h"
#include "ui/SongEditor.h"
#include "ui/SettingsEditor.h"
#include "ui/GameFlowPreview.h"
#include "game/PlayerSettings.h"
#include "input/InputManager.h"
#include "gameplay/HitDetector.h"
#include "gameplay/JudgmentSystem.h"
#include "gameplay/ScoreTracker.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <unordered_map>

enum class GameMode { Bandori, Cytus, Phigros, Arcaea, Lanota };
enum class EditorLayer { ProjectHub, StartScreen, MusicSelection, Settings, SongEditor, GamePlay };

class Engine : public IPlayerEngine {
public:
    Engine();
    ~Engine() override;

    void init(uint32_t width, uint32_t height, const std::string& title,
              const std::string& shaderDir, bool vsync = true);
    void shutdown();
    void run();
    void runHub();

    void setMode(GameModeRenderer* renderer, const ChartData& chart,
                 const GameModeConfig* config = nullptr);
    bool loadAudio(const std::string& path);
    void switchLayer(EditorLayer layer) { m_currentLayer = layer; }

    void launchGameplay(const SongInfo& song, Difficulty difficulty,
                        const std::string& projectPath, bool autoPlay = false) override;
    void launchGameplayDirect(const SongInfo& song, const ChartData& chart,
                              const std::string& projectPath);
    void exitGameplay() override;
    void requestStop() override;
    void restartGameplay();
    StartScreenEditor& startScreenEditor() { return m_startScreenEditor; }
    MusicSelectionEditor& musicSelectionEditor() { return m_musicSelectionEditor; }
    SongEditor& songEditor() { return m_songEditor; }
    SettingsEditor& settingsEditor() { return m_settingsEditor; }
    GameFlowPreview& gameFlowPreview() { return m_gameFlowPreview; }
    PlayerSettings& playerSettings() override { return m_playerSettings; }
    // Push m_playerSettings into the live audio engine, hit detector, and
    // active game-mode renderer. Safe to call at any time — applies what's
    // currently available.
    void applyPlayerSettings();
    ProjectHub& hub() { return m_hub; }
    AudioEngine& audio() override { return m_audio; }
    Renderer& renderer() override { return m_renderer; }
    GameClock& clock() override { return m_clock; }
    ImGuiLayer* imguiLayer() override { return &m_imgui; }
    ScoreTracker& score() override { return m_score; }
    JudgmentSystem& judgment() override { return m_judgment; }
    HitDetector& hitDetector() override { return m_hitDetector; }
    GameModeRenderer* activeMode() override { return m_activeMode.get(); }
    const GameModeConfig& gameplayConfig() const override { return m_gameplayConfig; }

    // Preview: set up a renderer and render one frame at the given time
    void setupPreviewMode(const GameModeConfig& config, const ChartData& chart,
                          const std::string& projectPath = "");
    void renderPreviewFrame(double songTime);
    void clearPreviewMode();
    bool hasPreviewMode() const { return m_previewMode != nullptr; }
    VkDescriptorSet sceneTexture() const { return m_sceneViewer.sceneTexture(); }

    // Expose InputManager so platform code (Android JNI, iOS bridge) can inject touches
    InputManager& inputManager() override { return m_input; }
    MaterialAssetLibrary& materialLibrary() override { return m_materialLibrary; }

    // Point the material library at a project and reload its assets.
    // ProjectHub/StartScreenEditor call this when a project is opened; the
    // library is then shared by every chart in the project.
    void openProject(const std::string& projectPath);

    // Shared editor-preview aspect ratio (Start Screen + Music Selection).
    // Editors letterbox/pillarbox their preview boxes to this ratio so the
    // author sees their scene as it will appear on the target device.
    struct PreviewAspect {
        int   w = 16;
        int   h = 9;
        int   presetIdx = 0;   // 0 = first preset (16:9 Desktop)
    };
    PreviewAspect& previewAspect() { return m_previewAspect; }

private:
    void mainLoop();
    void update(float dt);
    void render();

    void dispatchHitResult(const HitResult& hit, int lane = -1);
    void handleGestureLaneBased(const GestureEvent& evt, double songTime);
    void handleGestureArcaea(const GestureEvent& evt, double songTime);
    void handleGesturePhigros(const GestureEvent& evt, double songTime);
    void handleGestureCircle(class LanotaRenderer& lan,
                             const GestureEvent& evt, double songTime);
    void handleGestureScanLine(class CytusRenderer& cyt,
                               const GestureEvent& evt, double songTime);

    static std::unique_ptr<GameModeRenderer> createRenderer(const GameModeConfig& config);
    void togglePause();
    void renderGameplayHUD();
    void renderPauseOverlay();
    void renderResultsOverlay();

    static void framebufferResizeCallback(GLFWwindow* window, int w, int h);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double x, double y);
    static void dropCallback(GLFWwindow* window, int count, const char** paths);

    GLFWwindow*                        m_window = nullptr;
    Renderer                           m_renderer;
    ImGuiLayer                         m_imgui;
    SceneViewer                        m_sceneViewer;
    ProjectHub                         m_hub;
    StartScreenEditor                  m_startScreenEditor;
    MusicSelectionEditor               m_musicSelectionEditor;
    SongEditor                         m_songEditor;
    SettingsEditor                     m_settingsEditor;
    GameFlowPreview                    m_gameFlowPreview;
    PlayerSettings                     m_playerSettings;
    EditorLayer                        m_currentLayer = EditorLayer::ProjectHub;
    GameClock                          m_clock;
    AudioEngine                        m_audio;
    InputManager                       m_input;
    HitDetector                        m_hitDetector;
    JudgmentSystem                     m_judgment;
    ScoreTracker                       m_score;
    std::unique_ptr<GameModeRenderer>  m_activeMode;
    std::unordered_map<int32_t, uint32_t> m_activeTouches; // touchId → noteId for holds
    std::unordered_map<int, uint32_t>     m_keyboardHolds; // lane → noteId for keyboard-initiated holds
    EditorLayer                        m_preGameplayLayer = EditorLayer::MusicSelection;
    GameModeConfig                     m_gameplayConfig;  // config for current gameplay session
    bool                               m_gameplayPaused = false;
    bool                               m_autoPlay = false;
    bool                               m_showResults = false;
    float                              m_gameplayLeadIn = 0.f;   // countdown before audio starts
    bool                               m_audioStarted = false;
    std::string                        m_pendingAudioPath;       // audio to load after lead-in
    // Cached chart + project path for the current gameplay session so the
    // Restart button can reload the same data without going through the
    // hub/song-select flow again.
    ChartData                          m_currentChart;
    std::string                        m_currentProjectPath;
    // Per-project MaterialAsset registry. Reloaded by openProject() whenever
    // the active project changes. ChartLoader callers migrate any legacy
    // inline material entries into this library before the chart hits a
    // renderer, so downstream code only ever sees asset references.
    MaterialAssetLibrary               m_materialLibrary;

    PreviewAspect                      m_previewAspect;
    GameplayHudView                    m_hudView;
    ResultsView                        m_resultsView;
    std::string                        m_currentAudioPath;       // resolved audio path for restart
    bool                               m_framebufferResized = false;
    bool                               m_running = false;
    bool                               m_hubMode = false;

    // ── Preview mode (scene preview in editor) ───────────────────────────
    std::unique_ptr<GameModeRenderer>  m_previewMode;

    // ── Gameplay background image ──────────────────────────────────────────
    Texture     m_bgTexture;
    bool        m_bgLoaded = false;
    void loadBackgroundTexture(const std::string& projectPath, const std::string& bgImage);
    void clearBackgroundTexture();

    // ── Test/Play mode (full-screen game, no editor panels) ─────────────────
    bool        m_testMode = false;
    EditorLayer m_testReturnLayer = EditorLayer::SongEditor;

    // Transition state for test mode page changes
    bool        m_testTransitioning = false;
    float       m_testTransProgress = 0.f;
    EditorLayer m_testTransFrom = EditorLayer::StartScreen;
    EditorLayer m_testTransTo   = EditorLayer::MusicSelection;

public:
    bool  isTestMode()         const override { return m_testMode; }
    bool  isTestTransitioning() const override { return m_testTransitioning; }
    float testTransProgress()   const override { return m_testTransProgress; }
    void enterTestMode(EditorLayer returnLayer);
    void exitTestMode();
    void testTransitionTo(EditorLayer target);

    // Spawn `MusicGameEngineTest.exe --test <project>` as a child process so
    // the test game runs in its own window and the editor process keeps
    // running. Saves music_selection.json first so the child sees the latest
    // edits. Returns true on success.
    bool spawnTestGameProcess(const std::string& projectPath);

    // ── Auto-save ──────────────────────────────────────────────────────────
    // Editor mutators (note edit, marker thin, song-field tweak, etc.) call
    // markEditorDirty() so the next autosave tick flushes; performAutoSaveNow
    // is the unified flush path used by the timer, the window-close hook, and
    // the unhandled-exception filter. The dirty flag is cleared inside the
    // flush; the 30 s timer resets every time we go from clean → dirty.
    void markEditorDirty();
    void performAutoSaveNow(const char* reason);
    bool autoSaveStatusActive() const { return m_autoSaveStatusTimer > 0.f; }
    const std::string& autoSaveStatusMsg() const { return m_autoSaveStatusMsg; }

    // Public so the file-static OS-level crash callbacks (window-close,
    // SetUnhandledExceptionFilter, SetConsoleCtrlHandler, std::set_terminate)
    // can target the live engine — they can't capture `this`.
    static Engine* s_autosaveInstance;

private:
    // Cross-call state lives on the instance; static pointer above is for
    // the OS-level callbacks.
    bool   m_editorDirty             = false;
    float  m_autoSaveTimer           = 0.f;
    static constexpr float kAutoSaveIntervalSec = 30.f;
    float  m_autoSaveStatusTimer     = 0.f;   // for the bottom status line
    std::string m_autoSaveStatusMsg;
    void   tickAutoSave(float dt);
    void   installCrashHooks();
};
