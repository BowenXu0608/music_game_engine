#pragma once
#include "renderer/Renderer.h"
#include "game/modes/GameModeRenderer.h"
#include "engine/GameClock.h"
#include "engine/AudioEngine.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

enum class GameMode { Bandori, Cytus, Phigros, Arcaea, Lanota };

class Engine {
public:
    void init(uint32_t width, uint32_t height, const std::string& title,
              const std::string& shaderDir);
    void shutdown();
    void run();

    void loadChart(GameMode mode, const std::string& chartPath);
    void setMode(GameMode mode, const ChartData& chart);
    void loadAudio(const std::string& path);  // load + auto-play on next setMode

private:
    void mainLoop();
    void update(float dt);
    void render();

    static void framebufferResizeCallback(GLFWwindow* window, int w, int h);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    GLFWwindow*                        m_window = nullptr;
    Renderer                           m_renderer;
    GameClock                          m_clock;
    AudioEngine                        m_audio;
    std::unique_ptr<GameModeRenderer>  m_activeMode;
    bool                               m_framebufferResized = false;
    bool                               m_running = false;
};
