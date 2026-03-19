#pragma once
#include "GameModeRenderer.h"
#include "renderer/MeshRenderer.h"
#include <vector>

class ArcaeaRenderer : public GameModeRenderer {
public:
    void onInit(Renderer& renderer, const ChartData& chart) override;
    void onResize(uint32_t w, uint32_t h) override;
    void onUpdate(float dt, double songTime) override;
    void onRender(Renderer& renderer) override;
    void onShutdown(Renderer& renderer) override;
    const Camera& getCamera() const override { return m_camera; }

private:
    struct ArcMesh {
        Mesh     mesh;
        ArcData  data;
        double   startTime;
    };

    Mesh buildArcMesh(Renderer& renderer, const ArcData& arc);
    Mesh buildGroundMesh(Renderer& renderer);
    Mesh buildTapMesh(Renderer& renderer);
    glm::vec2 evalArc(const ArcData& arc, float t) const;

    Camera   m_camera;
    uint32_t m_width = 0, m_height = 0;
    double   m_songTime = 0.0;

    Mesh m_groundMesh;
    Mesh m_tapMesh;

    std::vector<NoteEvent> m_tapNotes;
    std::vector<ArcMesh>   m_arcs;

    static constexpr float SCROLL_SPEED = 8.f;
    static constexpr float GROUND_Y     = -2.f;
    static constexpr int   ARC_SEGMENTS = 32;
};
