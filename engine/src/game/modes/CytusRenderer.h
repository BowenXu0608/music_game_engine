#pragma once
#include "GameModeRenderer.h"
#include <vector>

class CytusRenderer : public GameModeRenderer {
public:
    void onInit(Renderer& renderer, const ChartData& chart,
                const GameModeConfig* config = nullptr) override;
    void onResize(uint32_t w, uint32_t h) override;
    void onUpdate(float dt, double songTime) override;
    void onRender(Renderer& renderer) override;
    void onShutdown(Renderer& renderer) override;
    void showJudgment(int lane, Judgment judgment) override;
    const Camera& getCamera() const override { return m_camera; }

private:
    struct CytusNote {
        float    x;                    // screen X (pixels)
        float    y;                    // screen Y (pixels)
        double   time;                 // hit time in seconds
        bool     isHold;
        float    holdDuration;
        int      lane      = 0;        // for showJudgment lookup
        bool     isHit     = false;
        float    hitTimer  = 0.f;      // seconds since hit (drives hit effect)
    };

    Camera   m_camera;
    uint32_t m_width = 0, m_height = 0;
    double   m_songTime     = 0.0;
    float    m_pageDuration = 4.f;
    float    m_scanLineY    = 0.f;
    float    m_approachSecs = 1.f;   // notes visible this many seconds before/after hit

    std::vector<CytusNote> m_notes;
};
