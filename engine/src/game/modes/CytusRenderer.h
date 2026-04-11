#pragma once
#include "GameModeRenderer.h"
#include <glm/glm.hpp>
#include <vector>
#include <utility>
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

private:
    // ── Scan-line schedule ─────────────────────────────────────────────
    // Period = 240/BPM seconds per sweep (1 bar @ 4/4). Computed in
    // onInit from the chart's first TimingPoint (fallback 120 BPM).
    float scanLinePeriod() const;
    float scanLineFrac(double t) const;  // normalized [0..1], 0 = top
    bool  scanLineGoingUp(double t) const;

    // Author-space → screen conversion
    float scanToScreenX(float nx) const { return nx * (float)m_width;  }
    float scanToScreenY(float ny) const { return ny * (float)m_height; }

    struct ScanNote {
        uint32_t id         = 0;  // NoteEvent.id from chart
        double time;              // hit time (head)
        float  sx, sy;            // normalized head position
        bool   isTap      = false;
        bool   isFlick    = false;
        bool   isHold     = false;
        bool   isSlide    = false;
        int    lane       = 0;    // for legacy showJudgment
        bool   goingUpAtTime = true;

        // Hold-only
        float  endY       = 0.f;  // normalized
        double endTime    = 0.0;

        // Slide-only
        std::vector<std::pair<float,float>> path; // normalized
        double slideEndTime = 0.0;
        std::vector<float>  samplePoints; // seconds from head

        // Runtime state
        bool   isHit     = false;
        float  hitTimer  = 0.f;
    };

    Camera   m_camera;
    uint32_t m_width = 0, m_height = 0;
    double   m_songTime = 0.0;
    float    m_bpm      = 120.f;

    std::vector<ScanNote> m_notes;
};
