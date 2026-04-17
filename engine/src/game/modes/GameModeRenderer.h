#pragma once
#include "renderer/Camera.h"
#include "game/chart/ChartTypes.h"
#include "gameplay/JudgmentSystem.h"
#include <unordered_set>
#include <cstdint>

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

    // Set by Engine each frame so renderers can highlight hold notes that
    // are currently being held (bloom/glow visual feedback).
    void setActiveHoldIds(const std::vector<uint32_t>& ids) {
        m_activeHoldIds.clear();
        for (uint32_t id : ids) m_activeHoldIds.insert(id);
    }

protected:
    std::unordered_set<uint32_t> m_activeHoldIds;
};
