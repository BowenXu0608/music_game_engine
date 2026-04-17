#pragma once
#include "GameModeRenderer.h"
#include <vector>
#include <unordered_set>
#include <optional>
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

    // ── Touch / keyboard hit picking ─────────────────────────────────────────
    // Result of a screen-space pick: which note the gesture should consume.
    struct PickResult {
        uint32_t noteId;
        int      ringIdx;
        NoteType type;
    };

    // Find the active note whose current screen position is closest to screenPx,
    // within the given pixel tolerance and the temporal hit window.  Returns
    // nothing if no candidate is close enough.  Used by the Engine's touch
    // gesture handler to map a tap to a specific note id.
    std::optional<PickResult> pickNoteAt(glm::vec2 screenPx,
                                         double    songTime,
                                         float     pixelTol) const;

    // Mark a note as consumed so onRender stops drawing it.
    void markNoteHit(uint32_t noteId);

    // Emit the particle burst + judgment visual at the note's current screen
    // position.  Used by both the touch path (called directly by Engine) and
    // the keyboard path (called from showJudgment after the angle search).
    void emitHitFeedback(uint32_t noteId, Judgment judgment);

    // Keyboard test path: dispatchHitResult routes here with a lane index.
    // We reverse-map lane → synthesized angle (matching the fallback in onInit)
    // and find the best matching note in any ring.
    void showJudgment(int lane, Judgment judgment) override;

    // Segment-based disk animation keyframes live in ChartData::diskAnimation
    // and are imported into the local m_rotationEvents / m_moveEvents /
    // m_scaleEvents lists in onInit.  Type aliases keep the old local names
    // working.
    using RotationEvent = DiskRotationEvent;
    using MoveEvent     = DiskMoveEvent;
    using ScaleEvent    = DiskScaleEvent;
    using EasingType    = DiskEasing;

    // Sample the disk transform at a given song time using the current
    // chart keyframes.  Editor uses this (via computeEnabledLanesAt) to
    // determine which lanes are reachable at any point in the timeline,
    // without running gameplay.
    uint32_t computeEnabledLanesAt(double songTime) const;

    // Re-seed disk animation keyframes from a (possibly edited) chart.
    // Used by the editor when the user adds/moves/resizes keyframes so
    // the live preview follows the edits without a full scene reload.
    void reloadDiskAnimation(const DiskAnimation& anim);

private:
    struct Ring {
        float  baseRadius;          // authored world-space radius (before disk scale)
        float  radius;              // baseRadius * m_diskScale; read by onRender/picking
        float  rotationSpeed;       // radians/sec — unused when disk-level rotation drives m_diskRotation
        float  currentAngle;        // mirrors m_diskRotation so downstream code can stay uniform
        std::vector<NoteEvent>       notes;
    };

    // Returns the interpolated disk angle (radians) for songTime given a
    // sorted list of segment-based rotation keyframes.  Holds at 0 before
    // the first event and at the last target after the last event.
    static float getCurrentRotation(double songTime,
                                    const std::vector<RotationEvent>& events);

    // Maps t∈[0,1] through the chosen easing curve.
    static float applyEasing(float t, EasingType easing);

    // Returns the interpolated disk center (world XY) for songTime.  Holds
    // at {0,0} before the first event and at the last target after the last.
    static glm::vec2 getDiskCenter(double songTime,
                                   const std::vector<MoveEvent>& events);

    // Returns the interpolated disk scale for songTime.  Holds at 1.0
    // before the first event and at the last target after the last.
    static float getDiskScale(double songTime,
                              const std::vector<ScaleEvent>& events);

    // Rebuilds m_perspVP from the current m_diskCenter.
    // Called by onResize and whenever m_diskCenter changes in onUpdate.
    void rebuildPerspVP();

    // Same NDC→screen mapping as BandoriRenderer.
    static glm::vec2 w2s(glm::vec3 pos, const glm::mat4& vp, float sw, float sh);
    void buildRingPolyline(float radius, std::vector<glm::vec2>& out) const;
    void drawHoldBodies(Renderer& renderer);

    // Project the note's current 3D position (using the same angle / Z scroll
    // formula as onRender) to screen space.  Returns false if the note id
    // can't be found or the point is behind the camera.
    bool projectNoteScreen(uint32_t noteId, glm::vec2& outScreen) const;

    // Find an unhit note whose stored chart angle (after ring rotation is
    // *removed* — i.e. the angle the note was authored at) is closest to
    // targetAngle within angularTol, and whose timing is within ±0.15s.
    // Used by showJudgment to map a keyboard lane press → note id.
    std::optional<uint32_t> findNoteByAngle(float targetAngle,
                                            float angularTol) const;

    Renderer*  m_renderer = nullptr;   // for particle emission on hit
    Camera     m_camera;        // ortho (0..w, 0..h) — used by all batchers
    glm::mat4  m_perspVP{1.f};  // perspective VP — used only for w2s projection
    float      m_proj11y = 0.f; // |proj[1][1]| for perspective-correct pixel sizes

    uint32_t  m_width = 0, m_height = 0;
    double    m_songTime = 0.0;
    glm::vec2 m_diskCenter{0.f, 0.f};  // current world-space XY of disk center
    float     m_diskScale        = 1.f;  // current uniform scale applied to radii
    float     m_diskInitialScale = 1.f;  // base multiplier from GameModeConfig::diskInitialScale
    float     m_diskRotation     = 0.f;  // current whole-disk rotation (radians)

    std::vector<Ring>              m_rings;
    std::vector<RotationEvent>     m_rotationEvents; // sorted by startTime; may be empty
    std::vector<MoveEvent>         m_moveEvents;     // sorted by startTime; may be empty
    std::vector<ScaleEvent>        m_scaleEvents;    // sorted by startTime; may be empty
    std::unordered_set<uint32_t>   m_hitNotes;       // consumed notes (skipped in onRender)
    int                            m_trackCount = 7; // for keyboard lane → angle mapping

    // Hold context — populated in onInit from the original chart notes so the
    // cross-lane hold body can be drawn even when the main note loop has
    // converted them to angle-based ring notes (which drops the HoldData).
    struct HoldBody {
        uint32_t noteId;
        double   startTime;
        HoldData data;
    };
    std::vector<HoldBody> m_holdBodies;

    static constexpr int   RING_SEGMENTS  = 64;
    // Per-instance disk layout, seeded from GameModeConfig in onInit and
    // falling back to the legacy defaults when no config is provided.
    float INNER_RADIUS  = 0.9f;
    float BASE_RADIUS   = 2.4f;
    float RING_SPACING  = 0.6f;
    static constexpr float NOTE_WORLD_R   = 0.22f; // note visual radius in world units
    static constexpr float APPROACH_SECS  = 2.0f;  // seconds to travel inner → outer
    static constexpr float FOV_Y_DEG      = 60.f;
};
