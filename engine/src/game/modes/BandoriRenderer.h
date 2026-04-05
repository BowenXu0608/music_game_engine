#pragma once
#include "GameModeRenderer.h"
#include "gameplay/JudgmentDisplay.h"
#include <vector>
#include <unordered_set>
#include <array>
#include <glm/glm.hpp>

class BandoriRenderer : public GameModeRenderer {
public:
    void onInit(Renderer& renderer, const ChartData& chart,
                const GameModeConfig* config = nullptr) override;
    void onResize(uint32_t w, uint32_t h) override;
    void onUpdate(float dt, double songTime) override;
    void onRender(Renderer& renderer) override;
    void onShutdown(Renderer& renderer) override;
    const Camera& getCamera() const override { return m_camera; }

    void showJudgment(int lane, Judgment judgment) override;

private:
    // Project a world-space point to screen coordinates matching the ortho batcher space.
    // Ortho convention: y=0 at screen bottom, y=h at screen top.
    static glm::vec2 w2s(glm::vec3 pos, const glm::mat4& vp, float sw, float sh);
    // Perspective-correct pixel size for a world-space extent.
    static float     pxSize(float worldSz, float clipW, float proj11y, float sh);

    Renderer*  m_renderer = nullptr;
    Camera     m_camera;        // ortho (0..w, 0..h) — used by all batchers
    glm::mat4  m_perspVP{1.f};  // perspective VP — used only for w2s projection
    float      m_proj11y = 0.f; // |proj[1][1]| for pxSize

    uint32_t m_width = 0, m_height = 0;
    double   m_songTime = 0.0;

    std::vector<NoteEvent>       m_notes;
    std::unordered_set<uint32_t> m_hitNotes;
    std::array<JudgmentDisplay, 12> m_judgmentDisplays;  // up to 12 tracks

    // Lane count (set from chart or config)
    int m_laneCount = 7;
    // Camera settings (from config or defaults)
    glm::vec3 m_camEye    = {0.f, 5.f, 6.f};
    glm::vec3 m_camTarget = {0.f, -1.f, -20.f};
    float     m_camFov    = 55.f;
    float m_laneSpacing  = 1.2f;    // world units between lane centres (auto-scaled)
    static constexpr float HIT_ZONE_Z   = 0.f;
    static constexpr float APPROACH_Z   = -55.f;
    static constexpr float SCROLL_SPEED = 14.f;
    // Note width = full lane (set from m_laneSpacing in onResize)
    float m_noteWorldW = 1.2f;
};
