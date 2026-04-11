#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <variant>
#include <cstdint>

// ── Note types ──────────────────────────────────────────────────────────────

enum class NoteType { Tap, Hold, Flick, Drag, Arc, ArcTap, Ring, Slide };

struct TapData   { float laneX; };
struct FlickData { float laneX; int direction = 0; };  // direction: -1=left, 0=up, 1=right

// A single sample point along a Hold note's path.
struct HoldWaypoint { double time; float laneX; };

// Hold = a long note played with one sustained finger press.
// Two waypoints at the same lane = simple straight hold.
// Multiple waypoints at different lanes = a slide hold (lane-changing path).
struct HoldData {
    std::vector<HoldWaypoint> waypoints;  // sorted by time; first=start, last=end

    float startLane() const { return waypoints.empty() ? 0.f : waypoints.front().laneX; }
    float endLane()   const { return waypoints.empty() ? 0.f : waypoints.back().laneX; }
    float duration()  const {
        return waypoints.size() < 2 ? 0.f
             : static_cast<float>(waypoints.back().time - waypoints.front().time);
    }
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

// ── Unified chart data ───────────────────────────────────────────────────────

struct ChartData {
    std::string title;
    std::string artist;
    float       offset = 0.f;   // audio sync offset in seconds

    std::vector<TimingPoint>       timingPoints;
    std::vector<NoteEvent>         notes;
    std::vector<JudgmentLineEvent> judgmentLines;  // Phigros only
};
