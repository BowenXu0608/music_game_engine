#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <variant>
#include <cstdint>

// ── Note types ───────────────────────────────────────────────────────────────

enum class NoteType { Tap, Hold, Flick, Drag, Arc, ArcTap, Ring, Slide };

struct TapData  { float laneX; };
struct HoldData { float laneX; float duration; };
struct FlickData{ float laneX; int direction; };  // direction: -1=left, 1=right

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
};

struct NoteEvent {
    double   time;
    NoteType type;
    uint32_t id;
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
