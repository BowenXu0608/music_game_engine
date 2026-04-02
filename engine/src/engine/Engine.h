#pragma once
#include "renderer/Renderer.h"
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

    void setMode(GameModeRenderer* renderer, const ChartData& chart);
    void loadAudio(const std::string& path);
    void switchLayer(EditorLayer layer) { m_currentLayer = layer; }
    StartScreenEditor& startScreenEditor() { return m_startScreenEditor; }
    MusicSelectionEditor& musicSelectionEditor() { return m_musicSelectionEditor; }
    SongEditor& songEditor() { return m_songEditor; }
    GameFlowPreview& gameFlowPreview() { return m_gameFlowPreview; }
    ProjectHub& hub() { return m_hub; }

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
    bool                               m_framebufferResized = false;
    bool                               m_running = false;
    bool                               m_hubMode = false;
};
