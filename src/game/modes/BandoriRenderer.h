#pragma once
#include "GameModeRenderer.h"
#include <vector>
#include <unordered_set>
#include <glm/glm.hpp>

class BandoriRenderer : public GameModeRenderer {
public:
    void onInit(Renderer& renderer, const ChartData& chart) override;
    void onResize(uint32_t w, uint32_t h) override;
    void onUpdate(float dt, double songTime) override;
    void onRender(Renderer& renderer) override;
    void onShutdown(Renderer& renderer) override;
    const Camera& getCamera() const override { return m_camera; }

private:
    // Project a world-space point to screen coordinates matching the ortho batcher space.
    // Ortho convention: y=0 at screen bottom, y=h at screen top.
    static glm::vec2 w2s(glm::vec3 pos, const glm::mat4& vp, float sw, float sh);
    // Perspective-correct pixel size for a world-space extent.
    static float     pxSize(float worldSz, float clipW, float proj11y, float sh);

    Camera     m_camera;        // ortho (0..w, 0..h) — used by all batchers
    glm::mat4  m_perspVP{1.f};  // perspective VP — used only for w2s projection
    float      m_proj11y = 0.f; // |proj[1][1]| for pxSize

    uint32_t m_width = 0, m_height = 0;
    double   m_songTime = 0.0;

    std::vector<NoteEvent>       m_notes;
    std::unordered_set<uint32_t> m_hitNotes;

    // World-space highway geometry constants
    static constexpr int   LANE_COUNT    = 7;
    static constexpr float LANE_SPACING  = 0.50f;   // world units between lane centres
    static constexpr float HIT_ZONE_Z   = 0.f;       // near end of highway
    static constexpr float APPROACH_Z   = -55.f;     // far end / vanishing zone
    static constexpr float SCROLL_SPEED = 14.f;      // world units / second
    static constexpr float NOTE_WORLD_W = 0.40f;     // note width in world units
    static constexpr float FOV_Y_DEG    = 52.f;
};
