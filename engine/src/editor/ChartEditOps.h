#pragma once
#include "ui/SongEditor.h"  // for EditorNote / EditorNoteType
#include "game/chart/ChartTypes.h"  // DiskRotationEvent, ScanSpeedEvent, etc.

#include <string>
#include <variant>
#include <vector>

class SongEditor;  // forward — extended apply path reaches into its difficulty maps

// Op vocabulary exposed to the AI copilot. The AI is instructed (via the
// per-mode skill docs under assets/copilot_skills/) to emit a JSON envelope
// of the form { explanation, ops: [...] }; parseChartEditOps() pulls out
// the list, applyChartEditOp() mutates a notes vector in place.
//
// Each op is a distinct struct; the public ChartEditOp is a std::variant
// so each op carries only the fields it actually needs (no more bloated
// struct-with-every-field). New op families (arc / slide / hold waypoint
// / disk / scan-speed) plug in as additional variants in later phases.

// ── Per-op payloads ─────────────────────────────────────────────────────────

struct DeleteRangeOp {
    float       tFrom = 0.f;
    float       tTo   = 0.f;
    std::string typeFilter = "any";   // "any"|"tap"|"hold"|"flick"
};

struct InsertOp {
    float          time     = 0.f;
    int            track    = 0;
    EditorNoteType type     = EditorNoteType::Tap;
    float          duration = 0.3f;   // used when type == Hold
};

struct MirrorLanesOp {
    float tFrom = 0.f;
    float tTo   = 0.f;
};

struct ShiftLanesOp {
    float tFrom     = 0.f;
    float tTo       = 0.f;
    int   deltaLane = 0;
};

struct ShiftTimeOp {
    float tFrom    = 0.f;
    float tTo      = 0.f;
    float deltaSec = 0.f;
};

struct ConvertTypeOp {
    float          tFrom    = 0.f;
    float          tTo      = 0.f;
    EditorNoteType fromType = EditorNoteType::Tap;
    EditorNoteType toType   = EditorNoteType::Tap;
    float          duration = 0.3f;   // used when toType == Hold
};

// ── 3D / Arcaea mode-specific ops ───────────────────────────────────────────

// Add one arc. Coordinates are normalized [0..1]; color is 0 (cyan/blue)
// or 1 (pink/red); void arcs are invisible carriers for ArcTaps. Easing
// codes arrive as strings ("s"/"si"/"so"/"sisi"/...) and are resolved
// to EditorNote's float ease value at apply time.
struct AddArcOp {
    float       time     = 0.f;
    float       duration = 1.f;
    float       startX   = 0.f;
    float       endX     = 0.f;
    float       startY   = 0.f;
    float       endY     = 0.f;
    std::string easeX    = "s";
    std::string easeY    = "s";
    int         color    = 0;
    bool        isVoid   = false;
};

// Delete every Arc in [tFrom, tTo]; also cascades to ArcTap children
// pointing at the deleted arcs so the chart never holds dangling refs.
struct DeleteArcOp {
    float tFrom = 0.f;
    float tTo   = 0.f;
};

// Add deltaY to both endpoints of every arc whose start-time is in
// [tFrom, tTo]. Values clamp to [0, 1]. Use to raise or lower a phrase.
struct ShiftArcHeightOp {
    float tFrom  = 0.f;
    float tTo    = 0.f;
    float deltaY = 0.f;
};

// Add one ArcTap at `time`; the parent arc is resolved at apply time by
// finding the arc whose [time, time+duration] brackets this tap's time.
// If no arc brackets the tap, the op is a no-op and contributes to the
// rejected-op count.
struct AddArcTapOp {
    float time = 0.f;
};

// Delete every ArcTap in [tFrom, tTo].
struct DeleteArcTapOp {
    float tFrom = 0.f;
    float tTo   = 0.f;
};

// ── ScanLine / Cytus mode-specific ops ──────────────────────────────────────

// Add one slide. Coordinates are normalized [0..1] in the sweep frame.
// `scanPath` is the polyline the user drags along; `samplePoints` are
// slide-tick times (seconds from note start) that feed combo scoring.
// The skill doc tells the LLM that paths should move in the direction of
// the sweep at `time`.
struct AddSlideOp {
    float                              time     = 0.f;
    float                              duration = 0.5f;
    float                              scanX    = 0.f;   // start x (also path[0].x)
    float                              scanY    = 0.f;   // start y (also path[0].y)
    std::vector<std::pair<float,float>> scanPath;        // >= 2 points
    std::vector<float>                 samplePoints;     // 0..duration
};

// Delete every Slide in [tFrom, tTo].
struct DeleteSlideOp {
    float tFrom = 0.f;
    float tTo   = 0.f;
};

// ── Hold-waypoint ops (drop + circle modes — anywhere Hold exists) ──────────

// Append a waypoint to an existing Hold note. `noteTime` identifies the
// hold (matches the note whose start time equals `noteTime`); `atTime` is
// the absolute time along the hold body at which to pin the lane change;
// `lane` is the target integer lane at that point. `style` is one of
// `"straight"`, `"angle90"`, `"curve"`, `"rhomboid"`.
// No-op if no Hold at `noteTime` or `atTime` falls outside the body.
struct AddHoldWaypointOp {
    float       noteTime = 0.f;
    float       atTime   = 0.f;
    int         lane     = 0;
    std::string style    = "curve";
};

// Remove the waypoint of the hold at `noteTime` whose absolute time equals
// `atTime`. No-op if no match.
struct RemoveHoldWaypointOp {
    float noteTime = 0.f;
    float atTime   = 0.f;
};

// Set the transition style for every Hold in [tFrom, tTo]. Applies to the
// legacy `transition` field AND rewrites every waypoint's `style`, so the
// behavior matches "Apply to All Holds" from the Note tab.
struct SetHoldTransitionOp {
    float       tFrom = 0.f;
    float       tTo   = 0.f;
    std::string style = "curve";
};

// ── Circle / Lanota disk animation ops ──────────────────────────────────────
// Easing codes: "linear", "sineInOut", "quadInOut", "cubicInOut".
// Target units: rotation in radians (absolute), move as {x, y} in world
// coords (float pair), scale as a unit-centered multiplier.

struct AddDiskRotationOp {
    float       startTime   = 0.f;
    float       duration    = 1.f;
    float       targetAngle = 0.f;
    std::string easing      = "sineInOut";
};

struct AddDiskMoveOp {
    float       startTime = 0.f;
    float       duration  = 1.f;
    float       targetX   = 0.f;
    float       targetY   = 0.f;
    std::string easing    = "sineInOut";
};

struct AddDiskScaleOp {
    float       startTime   = 0.f;
    float       duration    = 1.f;
    float       targetScale = 1.f;
    std::string easing      = "sineInOut";
};

// Remove one disk event by (kind, startTime). `kind` is "rotation",
// "move", or "scale". Matches on startTime within a 1 ms tolerance.
struct DeleteDiskEventOp {
    std::string kind      = "rotation";
    float       startTime = 0.f;
};

// ── ScanLine / Cytus page-speed ops ─────────────────────────────────────────

// Set (or upsert) the speed multiplier for one scan page. Pages without an
// override use 1.0. `pageIndex` is 0-based.
struct SetPageSpeedOp {
    int   pageIndex = 0;
    float speed     = 1.f;
};

// Add a low-level scan-speed event. Rarely needed — prefer SetPageSpeedOp
// for page-boundary step changes.
struct AddScanSpeedEventOp {
    float       startTime   = 0.f;
    float       duration    = 0.5f;
    float       targetSpeed = 1.f;
    std::string easing      = "sineInOut";
};

// Remove scan-speed events in [tFrom, tTo] (matching on startTime).
struct DeleteScanSpeedEventOp {
    float tFrom = 0.f;
    float tTo   = 0.f;
};

// Public op type. Add new op structs to the variant when expanding.
using ChartEditOp = std::variant<
    DeleteRangeOp,
    InsertOp,
    MirrorLanesOp,
    ShiftLanesOp,
    ShiftTimeOp,
    ConvertTypeOp,
    AddArcOp,
    DeleteArcOp,
    ShiftArcHeightOp,
    AddArcTapOp,
    DeleteArcTapOp,
    AddSlideOp,
    DeleteSlideOp,
    AddHoldWaypointOp,
    RemoveHoldWaypointOp,
    SetHoldTransitionOp,
    AddDiskRotationOp,
    AddDiskMoveOp,
    AddDiskScaleOp,
    DeleteDiskEventOp,
    SetPageSpeedOp,
    AddScanSpeedEventOp,
    DeleteScanSpeedEventOp
>;

// Returns true if `op` is allowed in the given game-mode name. Mode-gating
// is enforced by the Copilot send path: disallowed ops are moved to the
// `lastError` buffer and never reach apply. `modeName` uses the same
// strings as buildCopilotSystemPrompt ("bandori"/"arcaea"/"lanota"/"cytus").
bool isOpAllowedForMode(const ChartEditOp& op, const std::string& modeName);

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

// Extended apply surface for ops that live outside the notes vector —
// disk animation keyframes and scan-speed / scan-page overrides. The
// SongEditor reference is the canonical owner of those per-difficulty
// vectors. Note-vector ops fall through to applyChartEditOp above.
ChartEditApplyStats applyChartEditOpExtended(SongEditor& editor,
                                              int laneCount,
                                              const ChartEditOp& op);

// Returns true if `op` is one of the extended ops (disk / scan-speed).
// Used by the Copilot apply loop to route between the two entry points.
bool isExtendedOp(const ChartEditOp& op);

// Friendly one-liner for preview UI.
std::string describeChartEditOp(const ChartEditOp& op);
