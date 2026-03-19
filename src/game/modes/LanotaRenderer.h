#pragma once
#include "GameModeRenderer.h"
#include "core/SceneNode.h"
#include <vector>

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
        NodeID nodeID;
        float  radius;
        float  rotationSpeed;  // radians per second
        float  currentAngle;
        std::vector<NoteEvent> notes;
    };

    void buildRingPolyline(float radius, std::vector<glm::vec2>& out) const;

    Camera     m_camera;
    SceneGraph m_scene;
    uint32_t   m_width = 0, m_height = 0;
    double     m_songTime = 0.0;

    std::vector<Ring>                    m_rings;
    std::unordered_map<uint32_t, NodeID> m_noteNodes;

    static constexpr int   RING_SEGMENTS = 64;
    static constexpr float BASE_RADIUS   = 200.f;
};
