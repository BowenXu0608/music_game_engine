#pragma once
#include "renderer/Camera.h"
#include "game/chart/ChartTypes.h"
#include "gameplay/JudgmentSystem.h"

class Renderer;
struct GameModeConfig;

class GameModeRenderer {
public:
    virtual ~GameModeRenderer() = default;

    virtual void onInit(Renderer& renderer, const ChartData& chart,
                        const GameModeConfig* config = nullptr) = 0;
    virtual void onResize(uint32_t width, uint32_t height) = 0;
    virtual void onUpdate(float dt, double songTime) = 0;
    virtual void onRender(Renderer& renderer) = 0;
    virtual void onShutdown(Renderer& renderer) = 0;
    virtual const Camera& getCamera() const = 0;

    virtual void showJudgment(int lane, Judgment judgment) {}
};
