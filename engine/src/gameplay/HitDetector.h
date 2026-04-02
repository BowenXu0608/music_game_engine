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

class HitDetector {
public:
    static constexpr float HIT_RADIUS_PX = 90.0f;

    void init(const ChartData& chart);
    void update(double songTime);

    // Lane-based hit (Bandori, Cytus, Lanota)
    std::optional<HitResult> checkHit(int lane, double songTime);

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

    // Finalise a tracked hold; returns HitResult with timingDelta = release error
    std::optional<HitResult> endHold(uint32_t noteId, double releaseTime);

    // Record a slide position sample for accuracy scoring
    void  updateSlide(uint32_t noteId, glm::vec2 currentPos, double songTime);

    // Average position error across all slide samples (0 = perfect)
    float getSlideAccuracy(uint32_t noteId) const;

private:
    struct ActiveHold {
        uint32_t  noteId;
        double    startTime;
        double    noteStartTime;
        float     noteDuration;
        NoteType  noteType;
        std::vector<glm::vec2> positionSamples;
    };

    std::vector<NoteEvent>                    m_activeNotes;
    std::unordered_map<uint32_t, ActiveHold>  m_activeHolds;
    size_t m_nextNoteIndex = 0;
};
