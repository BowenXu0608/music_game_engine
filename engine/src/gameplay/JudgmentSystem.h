#pragma once
#include <cmath>

enum class Judgment { Perfect, Good, Bad, Miss };

enum class InputType {
    Tap,      // Single tap
    Hold,     // Press and hold
    Flick,    // Swipe/flick gesture
    Slide,    // Continuous slide
    Arc,      // Arcaea arc (sky note with finger tracking)
    SkyNote   // Arcaea sky tap
};

struct JudgmentStats {
    int perfect = 0;
    int good = 0;
    int bad = 0;
    int miss = 0;
};

class JudgmentSystem {
public:
    // Tap/Flick judgment - timing based
    Judgment judge(float timingDelta) {
        float absDelta = std::abs(timingDelta);
        if (absDelta <= 0.02f) return Judgment::Perfect;
        if (absDelta <= 0.06f) return Judgment::Good;
        if (absDelta <= 0.1f)  return Judgment::Bad;
        return Judgment::Miss;
    }

    // Hold judgment - checks if held for duration
    Judgment judgeHold(float pressTime, float releaseTime, float noteStart, float noteDuration) {
        float startDelta = std::abs(pressTime - noteStart);
        float endDelta = std::abs(releaseTime - (noteStart + noteDuration));

        // Both start and end must be accurate
        if (startDelta <= 0.02f && endDelta <= 0.05f) return Judgment::Perfect;
        if (startDelta <= 0.06f && endDelta <= 0.1f)  return Judgment::Good;
        if (startDelta <= 0.1f && endDelta <= 0.15f)  return Judgment::Bad;
        return Judgment::Miss;
    }

    // Flick/Swipe judgment - timing + direction
    Judgment judgeFlick(float timingDelta, float directionAccuracy) {
        float absDelta = std::abs(timingDelta);

        // Direction must be accurate (0.0-1.0, where 1.0 is perfect direction)
        if (directionAccuracy < 0.7f) return Judgment::Miss;

        if (absDelta <= 0.02f && directionAccuracy >= 0.95f) return Judgment::Perfect;
        if (absDelta <= 0.06f && directionAccuracy >= 0.85f) return Judgment::Good;
        if (absDelta <= 0.1f && directionAccuracy >= 0.7f)   return Judgment::Bad;
        return Judgment::Miss;
    }

    // Slide judgment - continuous accuracy check
    Judgment judgeSlide(float averagePositionError, float completionRatio) {
        // completionRatio: 0.0-1.0, how much of the slide was followed
        if (completionRatio < 0.8f) return Judgment::Miss;

        if (averagePositionError <= 0.05f && completionRatio >= 0.98f) return Judgment::Perfect;
        if (averagePositionError <= 0.15f && completionRatio >= 0.9f)  return Judgment::Good;
        if (averagePositionError <= 0.3f && completionRatio >= 0.8f)   return Judgment::Bad;
        return Judgment::Miss;
    }

    // Arc judgment (Arcaea) - continuous sky note tracking
    Judgment judgeArc(float averageTrackingError, float completionRatio) {
        // Similar to slide but for sky notes with finger tracking
        if (completionRatio < 0.85f) return Judgment::Miss;

        if (averageTrackingError <= 0.04f && completionRatio >= 0.98f) return Judgment::Perfect;
        if (averageTrackingError <= 0.12f && completionRatio >= 0.92f) return Judgment::Good;
        if (averageTrackingError <= 0.25f && completionRatio >= 0.85f) return Judgment::Bad;
        return Judgment::Miss;
    }

    // Sky note judgment (Arcaea) - timing only, no position requirement
    Judgment judgeSkyNote(float timingDelta) {
        float absDelta = std::abs(timingDelta);
        // Sky notes are more lenient than ground taps
        if (absDelta <= 0.03f) return Judgment::Perfect;
        if (absDelta <= 0.08f) return Judgment::Good;
        if (absDelta <= 0.12f) return Judgment::Bad;
        return Judgment::Miss;
    }

    void recordJudgment(Judgment j) {
        switch (j) {
            case Judgment::Perfect: m_stats.perfect++; break;
            case Judgment::Good:    m_stats.good++;    break;
            case Judgment::Bad:     m_stats.bad++;     break;
            case Judgment::Miss:    m_stats.miss++;    break;
        }
    }

    void reset() { m_stats = {}; }
    const JudgmentStats& getStats() const { return m_stats; }

private:
    JudgmentStats m_stats;
};
