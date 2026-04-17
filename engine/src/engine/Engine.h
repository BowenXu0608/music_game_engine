#pragma once
#include "renderer/Renderer.h"
#include "renderer/vulkan/TextureManager.h"
#include "game/modes/GameModeRenderer.h"
#include "engine/GameClock.h"
#include "engine/AudioEngine.h"
#include "ui/ImGuiLayer.h"
#include "ui/SceneViewer.h"
#include "ui/ProjectHub.h"
#include "ui/StartScreenEditor.h"
#include "ui/MusicSelectionEditor.h"
#include "ui/SongEditor.h"
#include "ui/GameFlowPreview.h"
#include "input/InputManager.h"
#include "gameplay/HitDetector.h"
#include "gameplay/JudgmentSystem.h"
#include "gameplay/ScoreTracker.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <unordered_map>

enum class GameMode { Bandori, Cytus, Phigros, Arcaea, Lanota };
enum class EditorLayer { ProjectHub, StartScreen, MusicSelection, SongEditor, GamePlay };

class Engine {
public:
    Engine();
    ~Engine();

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
                        const std::string& projectPath, bool autoPlay = false);
    void launchGameplayDirect(const SongInfo& song, const ChartData& chart,
                              const std::string& projectPath);
    void exitGameplay();
    void restartGameplay();
    StartScreenEditor& startScreenEditor() { return m_startScreenEditor; }
    MusicSelectionEditor& musicSelectionEditor() { return m_musicSelectionEditor; }
    SongEditor& songEditor() { return m_songEditor; }
    GameFlowPreview& gameFlowPreview() { return m_gameFlowPreview; }
    ProjectHub& hub() { return m_hub; }
    AudioEngine& audio() { return m_audio; }
    Renderer& renderer() { return m_renderer; }
    GameClock& clock() { return m_clock; }

    // Preview: set up a renderer and render one frame at the given time
    void setupPreviewMode(const GameModeConfig& config, const ChartData& chart,
                          const std::string& projectPath = "");
    void renderPreviewFrame(double songTime);
    void clearPreviewMode();
    bool hasPreviewMode() const { return m_previewMode != nullptr; }
    VkDescriptorSet sceneTexture() const { return m_sceneViewer.sceneTexture(); }

    // Expose InputManager so platform code (Android JNI, iOS bridge) can inject touches
    InputManager& inputManager() { return m_input; }

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
    GameFlowPreview                    m_gameFlowPreview;
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
    bool isTestMode() const { return m_testMode; }
    bool isTestTransitioning() const { return m_testTransitioning; }
    float testTransProgress() const { return m_testTransProgress; }
    void enterTestMode(EditorLayer returnLayer);
    void exitTestMode();
    void testTransitionTo(EditorLayer target);

    // Spawn `MusicGameEngineTest.exe --test <project>` as a child process so
    // the test game runs in its own window and the editor process keeps
    // running. Saves music_selection.json first so the child sees the latest
    // edits. Returns true on success.
    bool spawnTestGameProcess(const std::string& projectPath);
};
