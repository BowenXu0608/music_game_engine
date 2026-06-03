#pragma once
#include "GameModeRenderer.h"
#include "gameplay/JudgmentDisplay.h"
#include "renderer/Material.h"
#include "renderer/ParticleSystem.h"   // ParticleEmit
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <string>
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

    void showJudgment(int lane, Judgment judgment, float timingDelta = 0.f) override;
    const std::vector<JudgmentDisplay>& judgmentDisplays() const override {
        return m_judgmentDisplays;
    }
    void showHitEffect(HitEventKind kind, int lane,
                       NoteType type, Judgment judgment) override;
    void emitHoldAura(float dt) override;

private:
    // Resolve the particle effect bound to each Bandori event slot (from the
    // config bindings, falling back to the seeded defaults) into ready-to-emit
    // ParticleEmit structs. Compiles any Custom-kind shaders once.
    void resolveParticleEffects(const GameModeConfig* config);
    // Screen position of the judgment line for a (possibly fractional) lane,
    // where hit effects spawn. Fractional lanes occur mid cross-lane transition.
    glm::vec2 laneHitPos(float lane) const;

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
    std::vector<JudgmentDisplay> m_judgmentDisplays;

    // Lane count (set from chart or config)
    int m_laneCount = 7;
    // Author camera knobs (relative to the baked baseline; see onResize).
    float     m_camDistance = 1.f;   // x baseline eye-distance
    float     m_camFovDeg   = 0.f;   // 0 = baseline FOV, else degrees
    float     m_playfieldWidthPct = 0.9f;  // highway screen-width fraction
    float     m_playfieldHeightPct = 0.85f; // highway vertical fill (0.3..1)
    float m_laneSpacing  = 1.2f;    // world units between lane centres (auto-scaled)
    static constexpr float HIT_ZONE_Z   = 0.f;
    static constexpr float APPROACH_Z   = -55.f;  // baseline far edge (reference)
    // Far draw/cull distance, driven by Playfield Height each onResize. More
    // negative = the highway extends further toward the vanishing point (taller
    // field, same bottom width, same lane angle).
    float m_approachZ = APPROACH_Z;
    static constexpr float SCROLL_SPEED = 14.f;
    // Note width = full lane (set from m_laneSpacing in onResize)
    float m_noteWorldW = 1.2f;

    // Per-slot material overrides loaded from chart.materials at onInit.
    // Empty → fall back to the per-slot default in MaterialSlots.cpp.
    std::unordered_map<uint16_t, Material> m_chartMaterials;

    // Resolved particle effects keyed by event-slot slug (see ParticleSlots.h).
    std::map<std::string, ParticleEmit> m_particleEffects;
    // Hold note id → index into m_notes, so the sustained aura can evaluate the
    // hold's cross-lane curve at the current song time and follow the note.
    std::unordered_map<uint32_t, size_t> m_holdNoteIdx;

    // Returns the chart override for `slot` if present, else `fallback`.
    Material slotOrFallback(uint16_t slot, const Material& fallback) const;
};
