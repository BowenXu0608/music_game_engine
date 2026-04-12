#pragma once
#include "game/chart/ChartTypes.h"
#include <glm/glm.hpp>
#include <optional>
#include <vector>
#include <unordered_map>

struct HitResult {
    uint32_t noteId;
    float    timingDelta;
    NoteType noteType;
};

struct MissedNote {
    uint32_t noteId;
    NoteType noteType;
    int      lane;   // -1 if unknown
};

// A sample-point tick fired while an active hold has passed one of its
// authored HoldSamplePoint offsets. Players do not tap these — they just
// need to keep the hold alive past the sample time. Each tick is scored
// like a slide tick (Perfect on match, Miss on lane mismatch).
struct HoldSampleTick {
    uint32_t noteId;
    int      lane;     // the lane the tick was authored at (current expected lane)
    bool     hit;      // true = touch was on the expected lane → Perfect
                       // false = touch drifted off the curve → Miss
};

class HitDetector {
public:
    static constexpr float HIT_RADIUS_PX = 90.0f;

    void init(const ChartData& chart);
    void setTrackCount(int count) { m_trackCount = count; }
    std::vector<MissedNote> update(double songTime);

    // Pop any sample-point ticks that have elapsed since the last call.
    // Each tick is gated on whether the held touch is currently on the
    // expected lane (computed via evalHoldLaneAt). Holds that have already
    // ended never tick their remaining samples.
    std::vector<HoldSampleTick> consumeSampleTicks(double songTime);

    // Update the lane currently occupied by the touch holding `noteId`.
    // Bandori-style cross-lane holds compare this against the expected lane
    // at sample-tick time; if they diverge for two consecutive ticks, the
    // hold is broken (see consumeBrokenHolds).
    void updateHoldLane(uint32_t noteId, int lane);

    // Pop the noteIds of any holds whose touch wandered off the expected
    // lane long enough to break (≥2 consecutive missed sample ticks). The
    // caller should also clear its m_activeTouches mapping for these.
    std::vector<uint32_t> consumeBrokenHolds();

    // Lane-based hit (Bandori, Cytus, Lanota)
    std::optional<HitResult> checkHit(int lane, double songTime);

    // Consume any Drag notes in `lane` within a generous timing window.
    // Drag notes auto-hit when the player's finger passes through the lane
    // (no precise tap needed). Returns all consumed drags.
    std::vector<HitResult> consumeDrags(int lane, double songTime);

    // Id-based hit consumption — used when the caller (e.g. LanotaRenderer's
    // touch picker) has already chosen the specific note geometrically and
    // just needs the detector to validate the timing window, build the
    // HitResult, and erase the note from the active list.
    std::optional<HitResult> consumeNoteById(uint32_t noteId, double songTime);

    // Position-based hit for Arcaea ground taps
    std::optional<HitResult> checkHitPosition(glm::vec2 screenPos,
                                               glm::vec2 screenSize,
                                               double songTime);

    // Position-based hit for Phigros (projects touch onto rotating judgment line)
    std::optional<HitResult> checkHitPhigros(glm::vec2 screenPos,
                                              glm::vec2 lineOrigin,
                                              float lineRotation,
                                              double songTime);

    // Hold tracking — finds a HoldData note at the given lane, registers it
    std::optional<uint32_t> beginHold(int lane, double songTime);
    std::optional<uint32_t> beginHoldPosition(glm::vec2 screenPos,
                                               glm::vec2 screenSize,
                                               double songTime);

    // Id-based hold begin — caller already picked the specific HoldData note.
    // Returns HitResult with timingDelta for the hold head judgment.
    std::optional<HitResult> beginHoldById(uint32_t noteId, double songTime);

    // Finalise a tracked hold; returns HitResult with timingDelta = release error
    std::optional<HitResult> endHold(uint32_t noteId, double releaseTime);

    // Record a slide position sample for accuracy scoring
    void  updateSlide(uint32_t noteId, glm::vec2 currentPos, double songTime);

    // Average position error across all slide samples (0 = perfect)
    float getSlideAccuracy(uint32_t noteId) const;

    struct ActiveHold {
        uint32_t  noteId;
        double    startTime;
        double    noteStartTime;
        float     noteDuration;
        NoteType  noteType;
        int       lane = -1;                     // hold's starting lane
        int       currentLane = -1;              // lane the touch is currently on
        int       consecutiveMissedTicks = 0;    // run length of bad sample ticks
        bool      broken = false;                // set when a break threshold is crossed
        HoldData  holdData{};                    // captured for evalHoldLaneAt
        std::vector<glm::vec2> positionSamples;  // (Arcaea arc sampling / slide tracking)
        std::vector<float>     sampleOffsets;    // authored hold sample point times
        size_t                 nextSampleIdx = 0;
    };

    // Access an active hold's state (read-only). Returns nullptr if not found.
    const ActiveHold* getActiveHold(uint32_t noteId) const;

private:

    // Convert LanotaRingData angle to integer lane (keyboard compat)
    int angleToLane(float angle) const;

    std::vector<NoteEvent>                    m_activeNotes;
    std::unordered_map<uint32_t, ActiveHold>  m_activeHolds;
    size_t m_nextNoteIndex = 0;
    int    m_trackCount    = 7;
};
