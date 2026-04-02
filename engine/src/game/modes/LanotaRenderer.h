#pragma once
#include "GameModeRenderer.h"
#include <vector>
#include <glm/glm.hpp>

class LanotaRenderer : public GameModeRenderer {
public:
    void onInit(Renderer& renderer, const ChartData& chart) override;
    void onResize(uint32_t w, uint32_t h) override;
    void onUpdate(float dt, double songTime) override;
    void onRender(Renderer& renderer) override;
    void onShutdown(Renderer& renderer) override;
    const Camera& getCamera() const override { return m_camera; }

private:
    struct Ring {
        float  radius;          // world-space radius at Z=0 hit plane
        float  rotationSpeed;   // radians / second
        float  currentAngle;
        std::vector<NoteEvent> notes;
    };

    // Same NDC→screen mapping as BandoriRenderer.
    static glm::vec2 w2s(glm::vec3 pos, const glm::mat4& vp, float sw, float sh);
    void buildRingPolyline(float radius, std::vector<glm::vec2>& out) const;

    Camera     m_camera;        // ortho (0..w, 0..h) — used by all batchers
    glm::mat4  m_perspVP{1.f};  // perspective VP — used only for w2s projection
    float      m_proj11y = 0.f; // |proj[1][1]| for perspective-correct pixel sizes

    uint32_t m_width = 0, m_height = 0;
    double   m_songTime = 0.0;

    std::vector<Ring> m_rings;

    static constexpr int   RING_SEGMENTS  = 64;
    static constexpr float BASE_RADIUS    = 1.8f;  // world units — innermost ring radius
    static constexpr float RING_SPACING   = 0.6f;  // world units between ring radii
    static constexpr float NOTE_WORLD_R   = 0.22f; // note visual radius in world units
    static constexpr float SCROLL_SPEED_Z = 14.f;  // world units / second along Z
    static constexpr float APPROACH_SECS  = 2.5f;  // seconds before note reaches ring
    static constexpr float FOV_Y_DEG      = 60.f;
};
