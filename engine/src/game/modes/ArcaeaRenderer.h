#pragma once
#include "GameModeRenderer.h"
#include "renderer/MeshRenderer.h"
#include <vector>

class ArcaeaRenderer : public GameModeRenderer {
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
    struct ArcMesh {
        Mesh     mesh;
        ArcData  data;
        double   startTime;
    };

    Mesh buildDynamicArcMesh(Renderer& renderer);
    Mesh buildGroundMesh(Renderer& renderer);
    Mesh buildTapMesh(Renderer& renderer);
    Mesh buildGateMesh(Renderer& renderer, float skyHeight);
    Mesh buildArcTapMesh(Renderer& renderer);
    glm::vec2 evalArc(const ArcData& arc, float t) const;
    void writeArcVertices(ArcMesh& am, float tClip);

    Renderer* m_renderer = nullptr;
    Camera    m_camera;
    uint32_t  m_width = 0, m_height = 0;
    double    m_songTime = 0.0;

    Mesh m_groundMesh;
    Mesh m_tapMesh;
    Mesh m_gateMesh;
    Mesh m_arcTapMesh;

    struct HitEvent {
        double    time;
        glm::vec2 worldPos;   // (wx, wy) on the judgment plane (z = JUDGMENT_Z)
    };

    std::vector<NoteEvent> m_tapNotes;
    std::vector<NoteEvent> m_arcTaps;     // sky taps — float in the sky band at (arcX, arcY)
    std::vector<ArcMesh>   m_arcs;
    std::vector<HitEvent>  m_hitEvents;   // sorted by time; used for particle positioning

    int   m_laneCount = 7;
    float m_skyHeight = 1.f;

    // --- Single source of truth for the playfield geometry ------------------
    // The ground mesh, the judgment gate, the tap-lane mapping, and the arc
    // coord mapping ALL reference these. If you need to change the lane width
    // or the judgment plane location, edit them here — nowhere else.
    static constexpr float SCROLL_SPEED      = 8.f;
    static constexpr float GROUND_Y          = -2.f;   // lane ground plane (world y)
    static constexpr float LANE_HALF_WIDTH   = 3.f;    // lane near-edge spans x ∈ [-3, +3]
    static constexpr float LANE_FAR_Z        = -60.f; // lane back edge (far)
    static constexpr float JUDGMENT_Z        = 0.f;   // lane front edge / judgment plane
    static constexpr int   ARC_SEGMENTS      = 32;
};
