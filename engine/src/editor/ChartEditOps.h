#pragma once
#include "ui/SongEditor.h"  // for EditorNote / EditorNoteType

#include <string>
#include <vector>

// Op vocabulary exposed to the AI copilot. The AI is instructed to emit a
// JSON envelope of the form { explanation, ops: [...] }; parseOps() pulls
// out the list, applyOp() mutates a notes vector in place.
//
// Deliberately minimal so round-trips through buildChartFromNotes() stay
// safe. Arc / ArcTap / scan-path / hold-waypoint edits are out of scope.

enum class ChartEditOpKind {
    Unknown,
    DeleteRange,   // from, to, typeFilter
    Insert,        // time, track, type, duration (for Hold)
    MirrorLanes,   // from, to (track := laneCount-1 - track)
    ShiftLanes,    // from, to, deltaLane
    ShiftTime,     // from, to, deltaSec
    ConvertType,   // from, to, fromType, toType, duration (for →Hold)
};

struct ChartEditOp {
    ChartEditOpKind kind      = ChartEditOpKind::Unknown;
    float           tFrom     = 0.f;
    float           tTo       = 0.f;
    float           time      = 0.f;           // Insert only
    int             track     = 0;             // Insert / ShiftLanes delta
    int             deltaLane = 0;             // ShiftLanes
    float           deltaSec  = 0.f;           // ShiftTime
    EditorNoteType  type      = EditorNoteType::Tap;      // Insert / ConvertType to
    EditorNoteType  fromType  = EditorNoteType::Tap;      // ConvertType from
    std::string     typeFilter;                            // DeleteRange: "any"|"tap"|"hold"|"flick"
    float           duration  = 0.3f;          // Insert/ConvertType when Hold
};

struct ChartEditParseResult {
    bool                     success = false;
    std::string              explanation;
    std::vector<ChartEditOp> ops;
    std::string              errorMessage;
};

// Accept the raw assistant message; strips optional ```json ... ``` fences,
// parses the envelope, returns ops. errorMessage is populated on failure.
ChartEditParseResult parseChartEditOps(const std::string& assistantMsg);

// Apply one op to the supplied note list. Clamps out-of-range tracks, skips
// unknown ops, sorts by time when useful. Returns count of notes
// inserted / deleted / mutated for status messages.
struct ChartEditApplyStats {
    int inserted = 0;
    int deleted  = 0;
    int mutated  = 0;
};

ChartEditApplyStats applyChartEditOp(std::vector<EditorNote>& notes,
                                      int laneCount,
                                      const ChartEditOp& op);

// Friendly one-liner for preview UI.
std::string describeChartEditOp(const ChartEditOp& op);
