#pragma once
#include "GameModeRenderer.h"
#include "renderer/Material.h"
#include <glm/glm.hpp>
#include <vector>
#include <utility>
#include <unordered_map>
#include <optional>

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

    // ── Spatial picker ─────────────────────────────────────────────────
    // Scan-line input layer calls this on every tap/flick/hold-begin.
    // Returns the note whose stored screen position is closest to
    // `screenPx` within `pixelTol`, AND whose timing window covers
    // `songTime` (±0.15s). Timing gate keeps the picker from firing on
    // notes far in the future even if their on-screen position is
    // coincidentally near the tap.
    struct PickResult {
        uint32_t noteId;
        NoteType type;
    };
    std::optional<PickResult> pickNoteAt(glm::vec2 screenPx,
                                         double    songTime,
                                         float     pixelTol) const;

    // Mark a note consumed so onRender stops drawing it and fires the
    // expanding-ring hit effect.
    void markNoteHit(uint32_t noteId);

    // Evaluate expected screen position along a slide's path at `songTime`.
    // Returns false if noteId is not a slide or time is out of range.
    bool slideExpectedPos(uint32_t noteId, double songTime,
                          glm::vec2& outScreen) const;

    // Return sample-point times (absolute) for a slide note that haven't
    // been consumed yet. Marks consumed ones internally.
    struct SlideTick {
        uint32_t noteId;
        float    expectedX, expectedY; // screen-space expected position
    };
    std::vector<SlideTick> consumeSlideTicks(double songTime);

private:
    // ── Scan-line schedule ─────────────────────────────────────────────
    // Base period = 240/BPM seconds per sweep (1 bar @ 4/4).
    // With speed events, the actual scan position is driven by a
    // precomputed phase-accumulation table.
    float  scanLinePeriod() const;             // base period (constant)
    float  scanLineFrac(double t) const;       // normalized [0..1], 0 = top
    bool   scanLineGoingUp(double t) const;

    // Phase-table helpers
    void   buildPhaseTable();
    double interpolatePhase(double t) const;
    static float applyDiskEasing(float t, DiskEasing e);

    // Author-space → screen conversion
    float scanToScreenX(float nx) const { return nx * (float)m_width;  }
    float scanToScreenY(float ny) const { return ny * (float)m_height; }

    struct ScanNote {
        uint32_t id         = 0;
        double time;
        float  sx, sy;
        bool   isTap      = false;
        bool   isFlick    = false;
        bool   isHold     = false;
        bool   isSlide    = false;
        int    lane       = 0;
        bool   goingUpAtTime = true;

        float  endY        = 0.f;
        double endTime     = 0.0;
        int    holdSweeps  = 0;   // extra sweeps the hold crosses

        std::vector<std::pair<float,float>> path;
        double slideEndTime = 0.0;
        std::vector<float>  samplePoints;

        bool   isHit         = false;
        float  hitTimer      = 0.f;
        size_t nextSampleIdx = 0;
    };

    // Phase accumulation table for variable-speed scan line
    struct PhaseEntry {
        double time;
        double phase;  // accumulated phase (1.0 = one full sweep)
        double speed;  // instantaneous speed multiplier at this time
    };

    Renderer* m_renderer = nullptr;
    Camera   m_camera;
    uint32_t m_width = 0, m_height = 0;
    double   m_songTime = 0.0;
    float    m_bpm      = 120.f;
    double   m_basePeriod = 2.0;

    std::vector<ScanNote>       m_notes;
    std::vector<ScanSpeedEvent> m_speedEvents;
    std::vector<PhaseEntry>     m_phaseTable;

    // Per-slot chart material overrides, keyed by slot id.
    std::unordered_map<uint16_t, Material> m_chartMaterials;
    glm::vec4 slotTint(uint16_t slot, glm::vec4 fallbackRGBA) const;
};
