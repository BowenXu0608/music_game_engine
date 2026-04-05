#pragma once
#include "GameModeRenderer.h"
#include <vector>
#include <glm/glm.hpp>

class LanotaRenderer : public GameModeRenderer {
public:
    void onInit(Renderer& renderer, const ChartData& chart,
                const GameModeConfig* config = nullptr) override;
    void onResize(uint32_t w, uint32_t h) override;
    void onUpdate(float dt, double songTime) override;
    void onRender(Renderer& renderer) override;
    void onShutdown(Renderer& renderer) override;
    const Camera& getCamera() const override { return m_camera; }

    // A chart-provided keyframe: at songTime==time the disk should be at targetAngle (radians).
    struct RotationEvent {
        double time;
        float  targetAngle;
    };

    // Easing applied over the segment FROM this event TO the next one.
    enum class EasingType { Linear, SineInOut, QuadInOut, CubicInOut };

    // Chart-provided disk center keyframe.
    struct DiskMoveEvent {
        double     time;
        glm::vec2  target;   // world-space XY the disk center should be at
        EasingType easing;   // how to interpolate from here to the next event
    };

private:
    struct Ring {
        float  radius;              // world-space radius at Z=0 hit plane
        float  rotationSpeed;       // radians/sec — used only when rotationEvents is empty
        float  currentAngle;        // updated each frame; read by onRender
        std::vector<NoteEvent>       notes;
        std::vector<RotationEvent>   rotationEvents; // sorted by time; may be empty
    };

    // Returns the interpolated disk angle (radians) for songTime given a sorted
    // list of rotation keyframes.  Falls back to fallbackAngle when list is empty.
    static float getCurrentRotation(double songTime,
                                    const std::vector<RotationEvent>& events,
                                    float fallbackAngle);

    // Maps t∈[0,1] through the chosen easing curve.
    static float applyEasing(float t, EasingType easing);

    // Returns the interpolated disk center (world XY) for songTime.
    // Clamps before first / after last event; returns {0,0} when list is empty.
    static glm::vec2 getDiskCenter(double songTime,
                                   const std::vector<DiskMoveEvent>& events);

    // Rebuilds m_perspVP from the current m_diskCenter.
    // Called by onResize and whenever m_diskCenter changes in onUpdate.
    void rebuildPerspVP();

    // Same NDC→screen mapping as BandoriRenderer.
    static glm::vec2 w2s(glm::vec3 pos, const glm::mat4& vp, float sw, float sh);
    void buildRingPolyline(float radius, std::vector<glm::vec2>& out) const;

    Camera     m_camera;        // ortho (0..w, 0..h) — used by all batchers
    glm::mat4  m_perspVP{1.f};  // perspective VP — used only for w2s projection
    float      m_proj11y = 0.f; // |proj[1][1]| for perspective-correct pixel sizes

    uint32_t  m_width = 0, m_height = 0;
    double    m_songTime = 0.0;
    glm::vec2 m_diskCenter{0.f, 0.f};  // current world-space XY of disk center

    std::vector<Ring>          m_rings;
    std::vector<DiskMoveEvent> m_moveEvents; // sorted by time; may be empty

    static constexpr int   RING_SEGMENTS  = 64;
    static constexpr float BASE_RADIUS    = 1.8f;  // world units — innermost ring radius
    static constexpr float RING_SPACING   = 0.6f;  // world units between ring radii
    static constexpr float NOTE_WORLD_R   = 0.22f; // note visual radius in world units
    static constexpr float SCROLL_SPEED_Z = 14.f;  // world units / second along Z
    static constexpr float APPROACH_SECS  = 2.5f;  // seconds before note reaches ring
    static constexpr float FOV_Y_DEG      = 60.f;
};
