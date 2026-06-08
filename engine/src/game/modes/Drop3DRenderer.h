#pragma once
#include "GameModeRenderer.h"
#include "renderer/MeshRenderer.h"
#include "renderer/Material.h"
#include "renderer/ParticleSystem.h"   // ParticleEmit
#include "ui/ProjectHub.h"             // GameModeConfig (stored by value)
#include <vector>
#include <unordered_map>
#include <map>
#include <string>

class Drop3DRenderer : public GameModeRenderer {
public:
    void onInit(Renderer& renderer, const ChartData& chart,
                const GameModeConfig* config = nullptr) override;
    void onResize(uint32_t w, uint32_t h) override;
    void onUpdate(float dt, double songTime) override;
    void onRender(Renderer& renderer) override;
    void onShutdown(Renderer& renderer) override;
    void showJudgment(int lane, Judgment judgment, float timingDelta = 0.f) override;
    void showHitEffect(HitEventKind kind, int lane,
                       NoteType type, Judgment judgment) override;
    const Camera& getCamera() const override { return m_camera; }

private:
    struct ArcMesh {
        Mesh     mesh;        // hexagonal prism tracing the arc
        Mesh     shadow;      // flat ribbon on the ground directly below
        ArcData  data;
        double   startTime;
    };

    Mesh buildDynamicArcMesh(Renderer& renderer);
    Mesh buildDynamicArcShadowMesh(Renderer& renderer);
    Mesh buildGroundMesh(Renderer& renderer);
    Mesh buildTapMesh(Renderer& renderer);
    // Gate is split into 4 sub-meshes so each bar can carry its own material.
    Mesh buildGateBar(Renderer& renderer, float x0, float y0, float x1, float y1);
    Mesh buildArcTapMesh(Renderer& renderer);
    Mesh buildArcTapShadowMesh(Renderer& renderer);
    glm::vec2 evalArc(const ArcData& arc, float t) const;
    void writeArcVertices(ArcMesh& am, float tClip);
    void writeArcShadowVertices(ArcMesh& am, float tClip);

    // Returns the chart override for `slot` if present, else `fallback`.
    Material slotOrFallback(uint16_t slot, const Material& fallback) const;

    // Resolved particle effects keyed by event-slot slug (ParticleSlots.h).
    // Drop3D effects emit in WORLD space (world-scale params).
    std::map<std::string, ParticleEmit> m_particleEffects;
    void resolveParticleEffects(const GameModeConfig* config);

    Renderer* m_renderer = nullptr;
    Camera    m_camera;
    uint32_t  m_width = 0, m_height = 0;
    double    m_songTime = 0.0;

    Mesh m_groundMesh;
    Mesh m_tapMesh;
    Mesh m_gateBottom;    // thick bright bar — "Judgment Bar" slot
    Mesh m_gateSky;       // thin sky line   — "Sky Line" slot
    Mesh m_gateLeftPost;  // vertical post   — "Side Posts" slot
    Mesh m_gateRightPost; // vertical post   — "Side Posts" slot
    Mesh m_arcTapMesh;
    Mesh m_arcTapShadowMesh;

    // Per-slot material overrides imported from chart.materials. Empty entries
    // fall through to the per-slot default in MaterialSlots.cpp.
    std::unordered_map<uint16_t, Material> m_chartMaterials;

    struct HitEvent {
        double    time;
        glm::vec2 worldPos;   // (wx, wy) on the judgment plane (z = JUDGMENT_Z)
    };

    std::vector<NoteEvent> m_tapNotes;
    std::vector<NoteEvent> m_holdNotes;   // long lane notes — drawn as Z-stretched tap mesh
    std::vector<NoteEvent> m_arcTaps;     // sky taps — float in the sky band at (arcX, arcY)
    std::vector<ArcMesh>   m_arcs;
    std::vector<HitEvent>  m_hitEvents;   // sorted by time; used for particle positioning

    int   m_laneCount = 7;
    float m_skyHeight = 1.f;

    // Free-camera config (position/rotation/projection/clip + plane dims),
    // copied from the chart's GameModeConfig in onInit and consumed in onResize.
    GameModeConfig m_camCfg;

    // --- Single source of truth for the playfield geometry ------------------
    // The ground mesh, the judgment gate, the tap-lane mapping, and the arc
    // coord mapping ALL reference these. m_laneHalfWidth / m_laneFarZ derive
    // from the config's playfieldWidth / playfieldLength (computed in onInit
    // before the meshes build).
    static constexpr float SCROLL_SPEED      = 8.f;
    static constexpr float GROUND_Y          = -2.f;   // lane ground plane (world y)
    float m_laneHalfWidth = 3.f;    // = playfieldWidth * 0.5 (near-edge half span)
    float m_laneFarZ      = -60.f;  // = -playfieldLength (lane back edge)
    static constexpr float JUDGMENT_Z        = 0.f;   // lane front edge / judgment plane
    static constexpr int   ARC_SEGMENTS      = 32;
    // Arcs are drawn as hexagonal prisms (6-sided polygonal columns) tracing
    // along the arc centerline. Per-segment cross-section is a regular hexagon
    // of `ARC_RADIUS` in the xy plane, centered on the curve at each z sample.
    static constexpr int   ARC_SIDES         = 6;
    static constexpr float ARC_RADIUS        = 0.15f;
};
