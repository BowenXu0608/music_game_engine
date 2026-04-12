#pragma once
#include "GameModeRenderer.h"
#include "ChartTypes.h"
#include <memory>
#include <string>

// Forward declarations
struct GLFWwindow;

class Engine {
public:
    Engine();
    ~Engine();

    void init(uint32_t width, uint32_t height, const std::string& title,
              const std::string& shaderDir, bool vsync = true);
    void shutdown();
    void run();

    void setMode(GameModeRenderer* renderer, const ChartData& chart);
    bool loadAudio(const std::string& path);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
