#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <algorithm>
#include <utility>

// ── Note types ───────────────────────────────────────────────────────────────

enum class NoteType { Tap, Hold, Flick, Drag, Arc, ArcTap, Ring, Slide };

struct TapData  {
    float laneX;
    int   laneSpan = 1;

    // ── ScanLine mode (normalized 0..1 scene coordinates) ─────────────
    // Used by both NoteType::Tap and NoteType::Slide (slides reuse this
    // variant). scanPath is only populated for slides; for plain taps it
    // stays empty. `duration` is also slide-only (seconds from note time
    // to the end of the drawn path) and stays 0 for plain taps.
    float scanX    = 0.f;
    float scanY    = 0.f;
    float duration = 0.f;
    std::vector<std::pair<float,float>> scanPath;
    std::vector<float> samplePoints; // seconds from note start, slide ticks
};

// Lane-change transition style for cross-lane Hold notes.
// Straight : no lane change (endLaneX == laneX).
// Angle90  : body stays at laneX, snaps to endLaneX over the last transitionLen seconds.
// Curve    : smoothstep from laneX to endLaneX over the last transitionLen seconds.
// Rhomboid : diamond-shaped body spanning both lanes across the last transitionLen seconds.
enum class HoldTransition { Straight = 0, Angle90 = 1, Curve = 2, Rhomboid = 3 };

// Hold sample point: a checkpoint inside a Hold note. Players do NOT tap these —
// they just need to keep holding. Each passed sample awards slide-tick combo/score.
struct HoldSamplePoint { float tOffset; };  // seconds from note start (0 < t < duration)

// One waypoint along a Hold's lane path. The hold reaches `lane` at time
// `tOffset` (seconds from note start); the lane change FROM the previous
// waypoint took `transitionLen` seconds and ends exactly at `tOffset`,
// using `style` to interpolate the path.
//
// The first waypoint always sits at tOffset==0 with transitionLen==0 and
// represents the starting lane. Subsequent waypoints describe each lane
// crossing the author drew while drag-recording the hold.
struct HoldWaypoint {
    float          tOffset       = 0.f;
    int            lane          = 0;
    float          transitionLen = 0.f;
    HoldTransition style         = HoldTransition::Curve;
};

struct HoldData {
    float laneX;          // starting lane (== waypoints[0].lane when waypoints non-empty)
    float duration;       // total hold duration (seconds)
    int   laneSpan = 1;

    // New (preferred) representation: a sorted-by-time list of lane targets
    // recorded during drag authoring. When non-empty, takes precedence over
    // the legacy single-transition fields below.
    std::vector<HoldWaypoint> waypoints;

    // Legacy single-transition fields. Still parsed for old chart files and
    // used by `evalHoldLaneAt` when `waypoints` is empty.
    float endLaneX        = -1.f;       // end lane; if <0 → straight hold at laneX
    HoldTransition transition = HoldTransition::Straight;
    float transitionLen   = 0.f;        // seconds the lane change occupies
    float transitionStart = -1.f;       // when the change begins (sec from note start);
                                        // <0 → legacy default (pinned to last transitionLen seconds)

    std::vector<HoldSamplePoint> samplePoints;

    // ── ScanLine mode (normalized 0..1 scene coordinates) ─────────────
    float scanX    = 0.f;
    float scanY    = 0.f;
    float scanEndY = -1.f;  // -1 = unused
    int   scanHoldSweeps = 0; // extra sweeps the hold crosses (0 = single sweep)

    float effectiveEndLane() const {
        if (!waypoints.empty()) return static_cast<float>(waypoints.back().lane);
        return endLaneX < 0.f ? laneX : endLaneX;
    }
};

// Resolves the absolute begin time of the transition window inside the hold,
// honouring the legacy "pinned to end" sentinel and clamping to a valid range.
inline float holdTransitionBegin(const HoldData& h) {
    float tLen = std::clamp(h.transitionLen, 0.f, h.duration);
    float ts   = (h.transitionStart < 0.f) ? (h.duration - tLen) : h.transitionStart;
    return std::clamp(ts, 0.f, std::max(0.f, h.duration - tLen));
}

// Helper used by the legacy single-transition path.
inline float evalHoldLaneLegacy_(const HoldData& h, float tOffset) {
    const float dur   = h.duration;
    const float start = h.laneX;
    const float end   = h.endLaneX < 0.f ? h.laneX : h.endLaneX;
    if (h.transition == HoldTransition::Straight || dur <= 0.f || start == end)
        return start;
    const float tLen   = std::clamp(h.transitionLen, 0.f, dur);
    const float tBegin = holdTransitionBegin(h);
    const float tEnd   = tBegin + tLen;
    if (tOffset <= tBegin) return start;
    if (tOffset >= tEnd || tLen <= 0.f) return end;
    const float u = (tOffset - tBegin) / tLen;
    switch (h.transition) {
        case HoldTransition::Angle90: return end;
        case HoldTransition::Curve: {
            const float s = u * u * (3.f - 2.f * u);
            return start + (end - start) * s;
        }
        case HoldTransition::Rhomboid:
            return start + (end - start) * u;
        default: return start;
    }
}

// Evaluate the expected lane at time tOffset (seconds since note start).
// Multi-waypoint path takes precedence; falls back to legacy fields when
// `waypoints` is empty so older charts still render correctly.
inline float evalHoldLaneAt(const HoldData& h, float tOffset) {
    if (h.waypoints.empty())
        return evalHoldLaneLegacy_(h, tOffset);

    const auto& wp = h.waypoints;
    if (tOffset <= wp.front().tOffset) return static_cast<float>(wp.front().lane);
    if (tOffset >= wp.back().tOffset)  return static_cast<float>(wp.back().lane);

    for (size_t i = 1; i < wp.size(); ++i) {
        const HoldWaypoint& a = wp[i - 1];
        const HoldWaypoint& b = wp[i];
        if (tOffset > b.tOffset) continue;

        // Segment a → b. Lane change ends at b.tOffset and took b.transitionLen.
        const float tLen = std::clamp(b.transitionLen, 0.f, b.tOffset - a.tOffset);
        const float tEnd = b.tOffset;
        const float tBeg = tEnd - tLen;
        if (tOffset <= tBeg || tLen <= 0.f) {
            // Still cruising at the previous lane until the transition begins.
            return tOffset >= tEnd ? static_cast<float>(b.lane)
                                   : static_cast<float>(a.lane);
        }
        const float u = (tOffset - tBeg) / tLen;
        const float la = static_cast<float>(a.lane);
        const float lb = static_cast<float>(b.lane);
        switch (b.style) {
            case HoldTransition::Angle90:
                return lb;
            case HoldTransition::Curve: {
                const float s = u * u * (3.f - 2.f * u);
                return la + (lb - la) * s;
            }
            case HoldTransition::Rhomboid:
                return la + (lb - la) * u;
            case HoldTransition::Straight:
            default:
                return lb;
        }
    }
    return static_cast<float>(wp.back().lane);
}

// Find the waypoint segment whose transition window contains tOffset, if any.
// Returns the index of the *destination* waypoint (i.e. the one whose
// transitionLen window ends at its tOffset); -1 if tOffset is outside any
// active transition window.
inline int holdActiveSegment(const HoldData& h, float tOffset) {
    if (h.waypoints.size() < 2) return -1;
    for (size_t i = 1; i < h.waypoints.size(); ++i) {
        const auto& b = h.waypoints[i];
        const float tEnd = b.tOffset;
        const float tBeg = tEnd - std::max(0.f, b.transitionLen);
        if (tOffset >= tBeg && tOffset <= tEnd && b.transitionLen > 0.f)
            return static_cast<int>(i);
    }
    return -1;
}

struct FlickData{
    float laneX;
    int   direction = 0;  // -1=left, 0=up, 1=right
    // ── ScanLine mode (normalized 0..1 scene coordinates) ─────────────
    float scanX = 0.f;
    float scanY = 0.f;
};

struct ArcData {
    glm::vec2 startPos, endPos;
    float     duration;
    float     curveXEase, curveYEase;
    int       color;
    bool      isVoid;
};

struct PhigrosNoteData {
    float    posOnLine;
    NoteType subType;
    float    duration;
};

struct LanotaRingData {
    float angle;
    int   ringIndex;
    int   laneSpan = 1; // how many adjacent lanes the note covers (1, 2, or 3)
};

struct NoteEvent {
    double   time;
    NoteType type;
    uint32_t id;
    double   beatPosition = 0.0;  // accumulated beats from song start (set by computeBeatPositions)
    std::variant<TapData, HoldData, FlickData,
                 ArcData, PhigrosNoteData, LanotaRingData> data;
};

// ── Timing ───────────────────────────────────────────────────────────────────

struct TimingPoint {
    double time;   // seconds
    float  bpm;
    int    meter;  // beats per measure
};

// ── Judgment line (Phigros) ──────────────────────────────────────────────────

struct JudgmentLineEvent {
    double    time;
    glm::vec2 position;   // normalized [-1,1]
    float     rotation;   // radians
    float     speed;
    std::vector<NoteEvent> attachedNotes;
};

// ── Lanota / circle-mode disk animation ─────────────────────────────────────
//
// Segment-based keyframes: each event describes a transform change that
// starts at `startTime` and finishes `duration` seconds later. Between the
// end of one event and the start of the next, the value holds. Before the
// first event, the value is the base (0 rot, {0,0} pos, 1.0 scale).

enum class DiskEasing { Linear = 0, SineInOut = 1, QuadInOut = 2, CubicInOut = 3 };

struct DiskRotationEvent {
    double     startTime   = 0.0;
    double     duration    = 0.0;
    float      targetAngle = 0.f;   // radians, absolute
    DiskEasing easing      = DiskEasing::SineInOut;
};

struct DiskMoveEvent {
    double     startTime = 0.0;
    double     duration  = 0.0;
    glm::vec2  target{0.f, 0.f};    // world-space XY of the disk center
    DiskEasing easing    = DiskEasing::SineInOut;
};

struct DiskScaleEvent {
    double     startTime   = 0.0;
    double     duration    = 0.0;
    float      targetScale = 1.f;   // multiplies base radii (1.0 = untouched)
    DiskEasing easing      = DiskEasing::SineInOut;
};

struct DiskAnimation {
    std::vector<DiskRotationEvent> rotations;
    std::vector<DiskMoveEvent>     moves;
    std::vector<DiskScaleEvent>    scales;
};

// ── Scan-line speed events ──────────────────────────────────────────────────
//
// Segment-based speed multiplier changes for the Cytus-style scan line.
// Identical shape to disk animation events: the speed transitions from
// the previous value to targetSpeed over `duration` seconds starting at
// `startTime`.  Between events the speed holds.  Before the first event,
// speed = 1.0 (the base BPM speed).

struct ScanSpeedEvent {
    double     startTime   = 0.0;
    double     duration    = 0.0;
    float      targetSpeed = 1.0f;  // multiplier: 1.0 = base BPM, 2.0 = 2x fast
    DiskEasing easing      = DiskEasing::SineInOut;
};

// ── Scan-line per-page speed overrides ──────────────────────────────────────
// Page-based speed model: the scan-line editor views the song one "page" at a
// time, where a page = one sweep of the scan line (top→bottom or bottom→top).
// Each page has a speed multiplier; default = 1.0 (uses base BPM). Overrides
// are sparse — pages without an entry are implicit 1.0. At load time these
// expand into synthetic ScanSpeedEvents so the runtime phase table stays
// unchanged.

struct ScanPageOverride {
    int   pageIndex = 0;
    float speed     = 1.0f;
};

// ── Shared Catmull-Rom path interpolation ───────────────────────────────────
//
// Evaluate a Catmull-Rom spline along a path of (x,y) pairs.
// `u` is in [0,1] spanning the entire path.  Used by both CytusRenderer
// (gameplay) and SongEditor (preview) for smooth slide curve evaluation.

inline std::pair<float,float> catmullRomPathEval(
    const std::vector<std::pair<float,float>>& pts, float u)
{
    if (pts.empty()) return {0.f, 0.f};
    if (pts.size() == 1 || u <= 0.f) return pts.front();
    if (u >= 1.f) return pts.back();

    float scaled = u * static_cast<float>(pts.size() - 1);
    size_t i = static_cast<size_t>(scaled);
    if (i >= pts.size() - 1) i = pts.size() - 2;
    float t = scaled - static_cast<float>(i);

    const auto& p1 = pts[i];
    const auto& p2 = pts[i + 1];
    const auto& p0 = (i == 0) ? p1 : pts[i - 1];
    const auto& p3 = (i + 2 < pts.size()) ? pts[i + 2] : p2;

    auto cr = [](float a, float b, float c, float d, float t) {
        float t2 = t * t, t3 = t2 * t;
        return 0.5f * ((2.f * b) +
                       (-a + c) * t +
                       (2.f*a - 5.f*b + 4.f*c - d) * t2 +
                       (-a + 3.f*b - 3.f*c + d) * t3);
    };
    return { cr(p0.first,  p1.first,  p2.first,  p3.first,  t),
             cr(p0.second, p1.second, p2.second, p3.second, t) };
}

// ── Unified chart data ───────────────────────────────────────────────────────

struct ChartData {
    std::string title;
    std::string artist;
    float       offset = 0.f;   // audio sync offset in seconds

    std::vector<TimingPoint>       timingPoints;
    std::vector<NoteEvent>         notes;
    std::vector<JudgmentLineEvent> judgmentLines;  // Phigros only

    DiskAnimation diskAnimation;                   // Lanota / circle mode
    std::vector<ScanSpeedEvent>    scanSpeedEvents;    // Cytus scan-line speed
    std::vector<ScanPageOverride>  scanPageOverrides;  // Cytus per-page speed

    // Beat markers authored for this (mode, difficulty). Mirrors what the
    // editor keeps in m_diffMarkers; persisted per chart file so reopening a
    // project restores AI-detected / hand-placed markers for every mode.
    std::vector<float>             markers;
};
