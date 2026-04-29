#include "ChartEditOps.h"
#include "ui/SongEditor.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>

using nlohmann::json;

namespace {

EditorNoteType parseType(const std::string& s) {
    if (s == "tap" || s == "click") return EditorNoteType::Tap;
    if (s == "hold")                return EditorNoteType::Hold;
    if (s == "flick")               return EditorNoteType::Flick;
    // Slide / Arc / ArcTap intentionally fall back to Tap so a bad string
    // from the model doesn't silently break the edit. Per-mode skill docs
    // instruct the LLM not to emit those in the shared vocabulary.
    return EditorNoteType::Tap;
}

const char* typeName(EditorNoteType t) {
    switch (t) {
        case EditorNoteType::Tap:    return "tap";
        case EditorNoteType::Hold:   return "hold";
        case EditorNoteType::Flick:  return "flick";
        case EditorNoteType::Slide:  return "slide";
        case EditorNoteType::Arc:    return "arc";
        case EditorNoteType::ArcTap: return "arctap";
    }
    return "?";
}

// Arcaea easing codes → EditorNote::arcEaseX/Y float encoding. Matches the
// convention used by ArcaeaRenderer::parseArcaeaEase. Unknown codes fall
// back to linear (0.f).
float parseArcEase(const std::string& s) {
    if (s == "s")    return  0.f;
    if (s == "b")    return  1.f;
    if (s == "si")   return  2.f;
    if (s == "so")   return -2.f;
    if (s == "sisi") return  3.f;
    if (s == "siso") return -3.f;
    if (s == "sosi") return  4.f;
    if (s == "soso") return -4.f;
    return 0.f;
}

const char* arcEaseName(float v) {
    if (v ==  0.f) return "s";
    if (v ==  1.f) return "b";
    if (v ==  2.f) return "si";
    if (v == -2.f) return "so";
    if (v ==  3.f) return "sisi";
    if (v == -3.f) return "siso";
    if (v ==  4.f) return "sosi";
    if (v == -4.f) return "soso";
    return "s";
}

// Hold transition style name → EditorHoldTransition. Unknown falls back
// to Curve (the editor default).
EditorHoldTransition parseHoldStyle(const std::string& s) {
    if (s == "straight") return EditorHoldTransition::Straight;
    if (s == "angle90")  return EditorHoldTransition::Angle90;
    if (s == "curve")    return EditorHoldTransition::Curve;
    if (s == "rhomboid") return EditorHoldTransition::Rhomboid;
    return EditorHoldTransition::Curve;
}

const char* holdStyleName(EditorHoldTransition t) {
    switch (t) {
        case EditorHoldTransition::Straight: return "straight";
        case EditorHoldTransition::Angle90:  return "angle90";
        case EditorHoldTransition::Curve:    return "curve";
        case EditorHoldTransition::Rhomboid: return "rhomboid";
    }
    return "curve";
}

DiskEasing parseEasing(const std::string& s) {
    if (s == "linear")     return DiskEasing::Linear;
    if (s == "sineInOut")  return DiskEasing::SineInOut;
    if (s == "quadInOut")  return DiskEasing::QuadInOut;
    if (s == "cubicInOut") return DiskEasing::CubicInOut;
    return DiskEasing::SineInOut;
}

const char* easingName(DiskEasing e) {
    switch (e) {
        case DiskEasing::Linear:     return "linear";
        case DiskEasing::SineInOut:  return "sineInOut";
        case DiskEasing::QuadInOut:  return "quadInOut";
        case DiskEasing::CubicInOut: return "cubicInOut";
    }
    return "sineInOut";
}

// Strip optional ```json ... ``` or ``` ... ``` fences, and any leading /
// trailing prose outside the first {...} pair, so we can parse what's left.
std::string extractJsonEnvelope(const std::string& msg) {
    auto open = msg.find('{');
    if (open == std::string::npos) return msg;
    int depth = 0;
    size_t close = std::string::npos;
    for (size_t i = open; i < msg.size(); ++i) {
        char c = msg[i];
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) { close = i; break; }
        }
    }
    if (close == std::string::npos) return msg.substr(open);
    return msg.substr(open, close - open + 1);
}

// Parse the two floats that every range op carries. Swaps if tTo < tFrom.
void readRange(const json& item, float& tFrom, float& tTo) {
    tFrom = (float)item.value("from", 0.0);
    tTo   = (float)item.value("to",   0.0);
    if (tTo < tFrom) std::swap(tFrom, tTo);
}

} // namespace

ChartEditParseResult parseChartEditOps(const std::string& assistantMsg) {
    ChartEditParseResult out;
    std::string trimmed = extractJsonEnvelope(assistantMsg);
    json doc;
    try {
        doc = json::parse(trimmed);
    } catch (const std::exception& e) {
        out.errorMessage = std::string("Couldn't parse ops JSON: ") + e.what();
        return out;
    }

    if (doc.contains("explanation") && doc["explanation"].is_string())
        out.explanation = doc["explanation"].get<std::string>();

    if (!doc.contains("ops") || !doc["ops"].is_array()) {
        out.success = true;  // valid envelope with no ops (e.g. clarifying question)
        return out;
    }

    for (const auto& item : doc["ops"]) {
        if (!item.is_object()) continue;
        std::string kind = item.value("op", "");

        if (kind == "delete_range") {
            DeleteRangeOp op;
            readRange(item, op.tFrom, op.tTo);
            op.typeFilter = item.value("type_filter", "any");
            out.ops.emplace_back(op);
        }
        else if (kind == "insert") {
            InsertOp op;
            op.time     = (float)item.value("time", 0.0);
            op.track    = (int)  item.value("track", 0);
            op.type     = parseType(item.value("type", "tap"));
            op.duration = (float)item.value("duration", 0.3);
            out.ops.emplace_back(op);
        }
        else if (kind == "mirror_lanes") {
            MirrorLanesOp op;
            readRange(item, op.tFrom, op.tTo);
            out.ops.emplace_back(op);
        }
        else if (kind == "shift_lanes") {
            ShiftLanesOp op;
            readRange(item, op.tFrom, op.tTo);
            op.deltaLane = (int)item.value("delta", 0);
            out.ops.emplace_back(op);
        }
        else if (kind == "shift_time") {
            ShiftTimeOp op;
            readRange(item, op.tFrom, op.tTo);
            op.deltaSec = (float)item.value("delta", 0.0);
            out.ops.emplace_back(op);
        }
        else if (kind == "convert_type") {
            ConvertTypeOp op;
            readRange(item, op.tFrom, op.tTo);
            op.fromType = parseType(item.value("from_type", "tap"));
            op.toType   = parseType(item.value("to_type",   "tap"));
            op.duration = (float)item.value("duration", 0.3);
            out.ops.emplace_back(op);
        }
        else if (kind == "add_arc") {
            AddArcOp op;
            op.time     = (float)item.value("time",     0.0);
            op.duration = (float)item.value("duration", 1.0);
            op.startX   = (float)item.value("startX",   0.0);
            op.endX     = (float)item.value("endX",     0.0);
            op.startY   = (float)item.value("startY",   0.0);
            op.endY     = (float)item.value("endY",     0.0);
            op.easeX    = item.value("easeX", std::string("s"));
            op.easeY    = item.value("easeY", std::string("s"));
            op.color    = (int) item.value("color",  0);
            op.isVoid   = (bool)item.value("void",   false);
            out.ops.emplace_back(op);
        }
        else if (kind == "delete_arc") {
            DeleteArcOp op;
            readRange(item, op.tFrom, op.tTo);
            out.ops.emplace_back(op);
        }
        else if (kind == "shift_arc_height") {
            ShiftArcHeightOp op;
            readRange(item, op.tFrom, op.tTo);
            op.deltaY = (float)item.value("delta", 0.0);
            out.ops.emplace_back(op);
        }
        else if (kind == "add_arctap") {
            AddArcTapOp op;
            op.time = (float)item.value("time", 0.0);
            out.ops.emplace_back(op);
        }
        else if (kind == "delete_arctap") {
            DeleteArcTapOp op;
            readRange(item, op.tFrom, op.tTo);
            out.ops.emplace_back(op);
        }
        else if (kind == "add_slide") {
            AddSlideOp op;
            op.time     = (float)item.value("time",     0.0);
            op.duration = (float)item.value("duration", 0.5);
            op.scanX    = (float)item.value("scanX",    0.0);
            op.scanY    = (float)item.value("scanY",    0.0);
            if (item.contains("path") && item["path"].is_array()) {
                for (const auto& p : item["path"]) {
                    if (p.is_array() && p.size() >= 2) {
                        op.scanPath.emplace_back(
                            (float)p[0].get<double>(),
                            (float)p[1].get<double>());
                    } else if (p.is_object()) {
                        op.scanPath.emplace_back(
                            (float)p.value("x", 0.0),
                            (float)p.value("y", 0.0));
                    }
                }
            }
            if (item.contains("samples") && item["samples"].is_array()) {
                for (const auto& s : item["samples"])
                    op.samplePoints.push_back((float)s.get<double>());
            }
            out.ops.emplace_back(op);
        }
        else if (kind == "delete_slide") {
            DeleteSlideOp op;
            readRange(item, op.tFrom, op.tTo);
            out.ops.emplace_back(op);
        }
        else if (kind == "add_hold_waypoint") {
            AddHoldWaypointOp op;
            op.noteTime = (float)item.value("note_time", 0.0);
            op.atTime   = (float)item.value("at_time",   0.0);
            op.lane     = (int)  item.value("lane",      0);
            op.style    = item.value("style", std::string("curve"));
            out.ops.emplace_back(op);
        }
        else if (kind == "remove_hold_waypoint") {
            RemoveHoldWaypointOp op;
            op.noteTime = (float)item.value("note_time", 0.0);
            op.atTime   = (float)item.value("at_time",   0.0);
            out.ops.emplace_back(op);
        }
        else if (kind == "set_hold_transition") {
            SetHoldTransitionOp op;
            readRange(item, op.tFrom, op.tTo);
            op.style = item.value("style", std::string("curve"));
            out.ops.emplace_back(op);
        }
        else if (kind == "add_disk_rotation") {
            AddDiskRotationOp op;
            op.startTime   = (float)item.value("start_time", 0.0);
            op.duration    = (float)item.value("duration",   1.0);
            op.targetAngle = (float)item.value("target",     0.0);
            op.easing      = item.value("easing", std::string("sineInOut"));
            out.ops.emplace_back(op);
        }
        else if (kind == "add_disk_move") {
            AddDiskMoveOp op;
            op.startTime = (float)item.value("start_time", 0.0);
            op.duration  = (float)item.value("duration",   1.0);
            if (item.contains("target") && item["target"].is_array()
                && item["target"].size() >= 2) {
                op.targetX = (float)item["target"][0].get<double>();
                op.targetY = (float)item["target"][1].get<double>();
            } else {
                op.targetX = (float)item.value("x", 0.0);
                op.targetY = (float)item.value("y", 0.0);
            }
            op.easing = item.value("easing", std::string("sineInOut"));
            out.ops.emplace_back(op);
        }
        else if (kind == "add_disk_scale") {
            AddDiskScaleOp op;
            op.startTime   = (float)item.value("start_time", 0.0);
            op.duration    = (float)item.value("duration",   1.0);
            op.targetScale = (float)item.value("target",     1.0);
            op.easing      = item.value("easing", std::string("sineInOut"));
            out.ops.emplace_back(op);
        }
        else if (kind == "delete_disk_event") {
            DeleteDiskEventOp op;
            op.kind      = item.value("kind", std::string("rotation"));
            op.startTime = (float)item.value("start_time", 0.0);
            out.ops.emplace_back(op);
        }
        else if (kind == "set_page_speed") {
            SetPageSpeedOp op;
            op.pageIndex = (int)  item.value("page_index", 0);
            op.speed     = (float)item.value("speed",      1.0);
            out.ops.emplace_back(op);
        }
        else if (kind == "add_scan_speed_event") {
            AddScanSpeedEventOp op;
            op.startTime   = (float)item.value("start_time", 0.0);
            op.duration    = (float)item.value("duration",   0.5);
            op.targetSpeed = (float)item.value("target",     1.0);
            op.easing      = item.value("easing", std::string("sineInOut"));
            out.ops.emplace_back(op);
        }
        else if (kind == "delete_scan_speed_event") {
            DeleteScanSpeedEventOp op;
            readRange(item, op.tFrom, op.tTo);
            out.ops.emplace_back(op);
        }
        // Unknown ops silently skipped — the LLM may emit future vocab
        // that isn't wired yet; the per-mode skill docs describe current
        // ops, so anything else is best dropped rather than misapplied.
    }
    out.success = true;
    return out;
}

// ── describe ────────────────────────────────────────────────────────────────
// Uses std::string formatting instead of a fixed char buffer so new ops
// with longer parameter lists (arcs, disks, slides) can't overflow.

namespace {

std::string fmtFloat(float v, int prec = 2) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return buf;
}

std::string fmtDelta(float v, int prec = 3) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+.*f", prec, v);
    return buf;
}

} // namespace

std::string describeChartEditOp(const ChartEditOp& op) {
    return std::visit([](const auto& o) -> std::string {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, DeleteRangeOp>) {
            return "delete_range  " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo)
                 + "s  filter=" + (o.typeFilter.empty() ? "any" : o.typeFilter);
        }
        else if constexpr (std::is_same_v<T, InsertOp>) {
            std::string s = "insert        t=" + fmtFloat(o.time)
                          + "s track=" + std::to_string(o.track)
                          + " type=" + typeName(o.type);
            if (o.type == EditorNoteType::Hold)
                s += " duration=" + fmtFloat(o.duration) + "s";
            return s;
        }
        else if constexpr (std::is_same_v<T, MirrorLanesOp>) {
            return "mirror_lanes  " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo) + "s";
        }
        else if constexpr (std::is_same_v<T, ShiftLanesOp>) {
            return "shift_lanes   " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo)
                 + "s  delta=" + (o.deltaLane >= 0 ? "+" : "")
                 + std::to_string(o.deltaLane);
        }
        else if constexpr (std::is_same_v<T, ShiftTimeOp>) {
            return "shift_time    " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo)
                 + "s  delta=" + fmtDelta(o.deltaSec) + "s";
        }
        else if constexpr (std::is_same_v<T, ConvertTypeOp>) {
            std::string s = "convert_type  " + fmtFloat(o.tFrom) + "-"
                          + fmtFloat(o.tTo) + "s  "
                          + typeName(o.fromType) + "->" + typeName(o.toType);
            if (o.toType == EditorNoteType::Hold)
                s += " duration=" + fmtFloat(o.duration) + "s";
            return s;
        }
        else if constexpr (std::is_same_v<T, AddArcOp>) {
            return "add_arc       t=" + fmtFloat(o.time) + "s dur="
                 + fmtFloat(o.duration) + "s  ("
                 + fmtFloat(o.startX) + "," + fmtFloat(o.startY) + ")->("
                 + fmtFloat(o.endX) + "," + fmtFloat(o.endY) + ")  ease="
                 + o.easeX + "/" + o.easeY + "  color="
                 + (o.color == 1 ? "pink" : "blue")
                 + (o.isVoid ? "  void" : "");
        }
        else if constexpr (std::is_same_v<T, DeleteArcOp>) {
            return "delete_arc    " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo) + "s";
        }
        else if constexpr (std::is_same_v<T, ShiftArcHeightOp>) {
            return "shift_arc_h   " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo)
                 + "s  dy=" + fmtDelta(o.deltaY);
        }
        else if constexpr (std::is_same_v<T, AddArcTapOp>) {
            return "add_arctap    t=" + fmtFloat(o.time) + "s";
        }
        else if constexpr (std::is_same_v<T, DeleteArcTapOp>) {
            return "delete_arctap " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo) + "s";
        }
        else if constexpr (std::is_same_v<T, AddSlideOp>) {
            return "add_slide     t=" + fmtFloat(o.time) + "s dur="
                 + fmtFloat(o.duration) + "s  nodes="
                 + std::to_string(o.scanPath.size())
                 + " samples=" + std::to_string(o.samplePoints.size());
        }
        else if constexpr (std::is_same_v<T, DeleteSlideOp>) {
            return "delete_slide  " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo) + "s";
        }
        else if constexpr (std::is_same_v<T, AddHoldWaypointOp>) {
            return "add_hold_wp   hold@" + fmtFloat(o.noteTime)
                 + "s  at=" + fmtFloat(o.atTime) + "s lane="
                 + std::to_string(o.lane) + " style=" + o.style;
        }
        else if constexpr (std::is_same_v<T, RemoveHoldWaypointOp>) {
            return "rm_hold_wp    hold@" + fmtFloat(o.noteTime)
                 + "s  at=" + fmtFloat(o.atTime) + "s";
        }
        else if constexpr (std::is_same_v<T, SetHoldTransitionOp>) {
            return "set_hold_tx   " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo)
                 + "s  style=" + o.style;
        }
        else if constexpr (std::is_same_v<T, AddDiskRotationOp>) {
            return "disk_rotate   t=" + fmtFloat(o.startTime) + "s dur="
                 + fmtFloat(o.duration) + "s  angle=" + fmtFloat(o.targetAngle, 3)
                 + "rad ease=" + o.easing;
        }
        else if constexpr (std::is_same_v<T, AddDiskMoveOp>) {
            return "disk_move     t=" + fmtFloat(o.startTime) + "s dur="
                 + fmtFloat(o.duration) + "s  to=(" + fmtFloat(o.targetX)
                 + "," + fmtFloat(o.targetY) + ") ease=" + o.easing;
        }
        else if constexpr (std::is_same_v<T, AddDiskScaleOp>) {
            return "disk_scale    t=" + fmtFloat(o.startTime) + "s dur="
                 + fmtFloat(o.duration) + "s  x" + fmtFloat(o.targetScale, 3)
                 + " ease=" + o.easing;
        }
        else if constexpr (std::is_same_v<T, DeleteDiskEventOp>) {
            return "disk_delete   kind=" + o.kind + " t="
                 + fmtFloat(o.startTime) + "s";
        }
        else if constexpr (std::is_same_v<T, SetPageSpeedOp>) {
            return "page_speed    page=" + std::to_string(o.pageIndex)
                 + " x" + fmtFloat(o.speed, 3);
        }
        else if constexpr (std::is_same_v<T, AddScanSpeedEventOp>) {
            return "scan_speed    t=" + fmtFloat(o.startTime) + "s dur="
                 + fmtFloat(o.duration) + "s  x" + fmtFloat(o.targetSpeed, 3)
                 + " ease=" + o.easing;
        }
        else if constexpr (std::is_same_v<T, DeleteScanSpeedEventOp>) {
            return "scan_speed_del " + fmtFloat(o.tFrom) + "-" + fmtFloat(o.tTo) + "s";
        }
        else {
            return "(unknown op)";
        }
    }, op);
}

// ── apply ───────────────────────────────────────────────────────────────────

namespace {

bool matchesFilter(const EditorNote& n, const std::string& filter) {
    if (filter.empty() || filter == "any") return true;
    if (filter == "tap")   return n.type == EditorNoteType::Tap;
    if (filter == "hold")  return n.type == EditorNoteType::Hold;
    if (filter == "flick") return n.type == EditorNoteType::Flick;
    return true;
}

int clampTrack(int t, int laneCount) {
    if (t < 0) t = 0;
    if (t >= laneCount) t = laneCount - 1;
    return t;
}

void sortByTime(std::vector<EditorNote>& notes) {
    std::sort(notes.begin(), notes.end(),
        [](const EditorNote& a, const EditorNote& b) {
            return a.time < b.time;
        });
}

} // namespace

ChartEditApplyStats applyChartEditOp(std::vector<EditorNote>& notes,
                                      int laneCount,
                                      const ChartEditOp& op) {
    ChartEditApplyStats st;
    if (laneCount < 1) laneCount = 1;

    std::visit([&](const auto& o) {
        using T = std::decay_t<decltype(o)>;

        if constexpr (std::is_same_v<T, DeleteRangeOp>) {
            auto before = notes.size();
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                [&](const EditorNote& n) {
                    return n.time >= o.tFrom && n.time <= o.tTo
                        && matchesFilter(n, o.typeFilter);
                }), notes.end());
            st.deleted = (int)(before - notes.size());
        }
        else if constexpr (std::is_same_v<T, InsertOp>) {
            EditorNote n{};
            n.type  = o.type;
            n.time  = o.time;
            n.track = clampTrack(o.track, laneCount);
            if (o.type == EditorNoteType::Hold)
                n.endTime = o.time + std::max(0.05f, o.duration);
            notes.push_back(n);
            sortByTime(notes);
            st.inserted = 1;
        }
        else if constexpr (std::is_same_v<T, MirrorLanesOp>) {
            for (auto& n : notes) {
                if (n.time < o.tFrom || n.time > o.tTo) continue;
                n.track = clampTrack((laneCount - 1) - n.track, laneCount);
                if (n.endTrack >= 0)
                    n.endTrack = clampTrack((laneCount - 1) - n.endTrack, laneCount);
                ++st.mutated;
            }
        }
        else if constexpr (std::is_same_v<T, ShiftLanesOp>) {
            for (auto& n : notes) {
                if (n.time < o.tFrom || n.time > o.tTo) continue;
                n.track = clampTrack(n.track + o.deltaLane, laneCount);
                if (n.endTrack >= 0)
                    n.endTrack = clampTrack(n.endTrack + o.deltaLane, laneCount);
                ++st.mutated;
            }
        }
        else if constexpr (std::is_same_v<T, ShiftTimeOp>) {
            for (auto& n : notes) {
                if (n.time < o.tFrom || n.time > o.tTo) continue;
                n.time += o.deltaSec;
                if (n.endTime > 0.f) n.endTime += o.deltaSec;
                ++st.mutated;
            }
            sortByTime(notes);
        }
        else if constexpr (std::is_same_v<T, ConvertTypeOp>) {
            for (auto& n : notes) {
                if (n.time < o.tFrom || n.time > o.tTo) continue;
                if (n.type != o.fromType) continue;
                n.type = o.toType;
                if (o.toType == EditorNoteType::Hold && n.endTime <= n.time)
                    n.endTime = n.time + std::max(0.05f, o.duration);
                if (o.toType != EditorNoteType::Hold)
                    n.endTime = 0.f;
                ++st.mutated;
            }
        }
        else if constexpr (std::is_same_v<T, AddArcOp>) {
            auto clampUnit = [](float v) {
                return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            };
            EditorNote n{};
            n.type       = EditorNoteType::Arc;
            n.time       = o.time;
            n.endTime    = o.time + std::max(0.05f, o.duration);
            n.arcStartX  = clampUnit(o.startX);
            n.arcEndX    = clampUnit(o.endX);
            n.arcStartY  = clampUnit(o.startY);
            n.arcEndY    = clampUnit(o.endY);
            n.arcEaseX   = parseArcEase(o.easeX);
            n.arcEaseY   = parseArcEase(o.easeY);
            n.arcColor   = (o.color == 1) ? 1 : 0;
            n.arcIsVoid  = o.isVoid;
            notes.push_back(n);
            sortByTime(notes);
            st.inserted = 1;
        }
        else if constexpr (std::is_same_v<T, DeleteArcOp>) {
            // Pick every arc whose start-time falls in the range, then
            // build a remap table so dependent ArcTap parent indices can
            // be rewritten. Orphan ArcTaps (parent == deleted idx) are
            // also removed so the chart never holds a dangling ref.
            std::vector<uint8_t> doomed(notes.size(), 0);
            for (size_t i = 0; i < notes.size(); ++i) {
                const auto& n = notes[i];
                if (n.type == EditorNoteType::Arc
                    && n.time >= o.tFrom && n.time <= o.tTo) {
                    doomed[i] = 1;
                }
            }
            for (size_t i = 0; i < notes.size(); ++i) {
                const auto& n = notes[i];
                if (n.type == EditorNoteType::ArcTap
                    && n.arcTapParent >= 0
                    && n.arcTapParent < (int)notes.size()
                    && doomed[n.arcTapParent]) {
                    doomed[i] = 1;
                }
            }
            // Rewrite ArcTap parent indices: for each survivor, count how
            // many doomed indices sit before its current parent.
            std::vector<int> indexMap(notes.size(), -1);
            int newIdx = 0;
            for (size_t i = 0; i < notes.size(); ++i)
                if (!doomed[i]) indexMap[i] = newIdx++;
            for (auto& n : notes) {
                if (n.type == EditorNoteType::ArcTap && n.arcTapParent >= 0
                    && n.arcTapParent < (int)indexMap.size()) {
                    n.arcTapParent = indexMap[n.arcTapParent];
                }
            }
            auto before = notes.size();
            size_t w = 0;
            for (size_t r = 0; r < notes.size(); ++r) {
                if (!doomed[r]) { notes[w++] = std::move(notes[r]); }
            }
            notes.resize(w);
            st.deleted = (int)(before - notes.size());
        }
        else if constexpr (std::is_same_v<T, ShiftArcHeightOp>) {
            auto clampUnit = [](float v) {
                return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            };
            for (auto& n : notes) {
                if (n.type != EditorNoteType::Arc) continue;
                if (n.time < o.tFrom || n.time > o.tTo) continue;
                n.arcStartY = clampUnit(n.arcStartY + o.deltaY);
                n.arcEndY   = clampUnit(n.arcEndY   + o.deltaY);
                ++st.mutated;
            }
        }
        else if constexpr (std::is_same_v<T, AddArcTapOp>) {
            // Find parent arc whose window brackets the tap time. Falls
            // back to the nearest arc if none strictly brackets. If no
            // arc exists at all, the op is a no-op (stat stays 0).
            int bestIdx = -1;
            float bestDist = 1e9f;
            for (size_t i = 0; i < notes.size(); ++i) {
                const auto& n = notes[i];
                if (n.type != EditorNoteType::Arc) continue;
                if (o.time >= n.time && o.time <= n.endTime) {
                    bestIdx = (int)i;
                    bestDist = 0.f;
                    break;
                }
                float d = std::min(std::fabs(o.time - n.time),
                                   std::fabs(o.time - n.endTime));
                if (d < bestDist) { bestDist = d; bestIdx = (int)i; }
            }
            if (bestIdx < 0) return;  // no arc to attach to
            EditorNote n{};
            n.type         = EditorNoteType::ArcTap;
            n.time         = o.time;
            n.arcTapParent = bestIdx;
            notes.push_back(n);
            sortByTime(notes);
            // sortByTime may have moved the parent; rebuild the tap's
            // parent link by matching the original parent's time.
            float parentTime = notes[bestIdx].time;
            // After sort, find the arc with that time that's still Arc.
            // If two arcs share the same time, the first one wins — same
            // policy the editor uses.
            for (size_t i = 0; i < notes.size(); ++i) {
                if (notes[i].type == EditorNoteType::Arc
                    && notes[i].time == parentTime) {
                    // Fix up the newly inserted ArcTap.
                    for (auto& m : notes) {
                        if (m.type == EditorNoteType::ArcTap
                            && m.time == o.time
                            && m.arcTapParent == bestIdx) {
                            m.arcTapParent = (int)i;
                        }
                    }
                    break;
                }
            }
            st.inserted = 1;
        }
        else if constexpr (std::is_same_v<T, DeleteArcTapOp>) {
            auto before = notes.size();
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                [&](const EditorNote& n) {
                    return n.type == EditorNoteType::ArcTap
                        && n.time >= o.tFrom && n.time <= o.tTo;
                }), notes.end());
            st.deleted = (int)(before - notes.size());
        }
        else if constexpr (std::is_same_v<T, AddSlideOp>) {
            auto clampUnit = [](float v) {
                return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            };
            EditorNote n{};
            n.type     = EditorNoteType::Slide;
            n.time     = o.time;
            n.endTime  = o.time + std::max(0.05f, o.duration);
            // Start position — used by the renderer when scanPath is empty.
            n.scanX = clampUnit(o.scanX);
            n.scanY = clampUnit(o.scanY);
            // Copy path, clamping each point. If the LLM omitted the
            // starting node, synthesize one from (scanX, scanY).
            n.scanPath.reserve(o.scanPath.size() + 1);
            if (o.scanPath.empty()) {
                n.scanPath.emplace_back(n.scanX, n.scanY);
            } else {
                for (const auto& p : o.scanPath) {
                    n.scanPath.emplace_back(clampUnit(p.first),
                                            clampUnit(p.second));
                }
            }
            // Sample points: clamp into [0, duration].
            float dur = n.endTime - n.time;
            n.samplePoints.reserve(o.samplePoints.size());
            for (float s : o.samplePoints) {
                if (s < 0.f) s = 0.f;
                if (s > dur) s = dur;
                n.samplePoints.push_back(s);
            }
            notes.push_back(n);
            sortByTime(notes);
            st.inserted = 1;
        }
        else if constexpr (std::is_same_v<T, DeleteSlideOp>) {
            auto before = notes.size();
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                [&](const EditorNote& n) {
                    return n.type == EditorNoteType::Slide
                        && n.time >= o.tFrom && n.time <= o.tTo;
                }), notes.end());
            st.deleted = (int)(before - notes.size());
        }
        else if constexpr (std::is_same_v<T, AddHoldWaypointOp>) {
            // Find the matching hold by nearest start time (tolerate a
            // 1 ms fuzz for float round-tripping).
            int bestIdx = -1;
            float bestDist = 1e9f;
            for (size_t i = 0; i < notes.size(); ++i) {
                if (notes[i].type != EditorNoteType::Hold) continue;
                float d = std::fabs(notes[i].time - o.noteTime);
                if (d < bestDist) { bestDist = d; bestIdx = (int)i; }
            }
            if (bestIdx < 0 || bestDist > 0.01f) return;  // no such hold

            EditorNote& h = notes[bestIdx];
            float tOff = o.atTime - h.time;
            if (tOff <= 0.f || tOff > (h.endTime - h.time)) return;  // out of body

            EditorHoldWaypoint wp{};
            wp.tOffset       = tOff;
            wp.lane          = o.lane;
            wp.transitionLen = 0.3f;  // matches the default the UI drag-record uses
            wp.style         = parseHoldStyle(o.style);

            // Insert sorted by tOffset so the gameplay evaluator can walk
            // the list linearly.
            auto pos = std::upper_bound(h.waypoints.begin(), h.waypoints.end(), wp,
                [](const EditorHoldWaypoint& a, const EditorHoldWaypoint& b) {
                    return a.tOffset < b.tOffset;
                });
            h.waypoints.insert(pos, wp);
            ++st.mutated;
        }
        else if constexpr (std::is_same_v<T, RemoveHoldWaypointOp>) {
            int bestIdx = -1;
            float bestDist = 1e9f;
            for (size_t i = 0; i < notes.size(); ++i) {
                if (notes[i].type != EditorNoteType::Hold) continue;
                float d = std::fabs(notes[i].time - o.noteTime);
                if (d < bestDist) { bestDist = d; bestIdx = (int)i; }
            }
            if (bestIdx < 0 || bestDist > 0.01f) return;

            EditorNote& h = notes[bestIdx];
            float targetOff = o.atTime - h.time;
            auto before = h.waypoints.size();
            h.waypoints.erase(std::remove_if(h.waypoints.begin(), h.waypoints.end(),
                [&](const EditorHoldWaypoint& w) {
                    return std::fabs(w.tOffset - targetOff) <= 0.01f;
                }), h.waypoints.end());
            if (h.waypoints.size() != before) ++st.mutated;
        }
        else if constexpr (std::is_same_v<T, SetHoldTransitionOp>) {
            EditorHoldTransition style = parseHoldStyle(o.style);
            for (auto& n : notes) {
                if (n.type != EditorNoteType::Hold) continue;
                if (n.time < o.tFrom || n.time > o.tTo) continue;
                n.transition = style;
                for (auto& w : n.waypoints) w.style = style;
                ++st.mutated;
            }
        }
        // Disk / scan-speed ops don't touch the notes vector; route through
        // applyChartEditOpExtended instead. Nothing to do here.
    }, op);

    return st;
}

// ── Extended apply (disk animation + scan-speed ops) ────────────────────────
// These ops live outside the notes vector. The SongEditor owns the
// per-difficulty keyframe vectors; we mutate them through its public
// accessors (diskRot(), diskMove(), diskScale(), scanSpeed(), scanPages()).

bool isExtendedOp(const ChartEditOp& op) {
    return std::visit([](const auto& o) -> bool {
        using T = std::decay_t<decltype(o)>;
        (void)o;
        return std::is_same_v<T, AddDiskRotationOp>
            || std::is_same_v<T, AddDiskMoveOp>
            || std::is_same_v<T, AddDiskScaleOp>
            || std::is_same_v<T, DeleteDiskEventOp>
            || std::is_same_v<T, SetPageSpeedOp>
            || std::is_same_v<T, AddScanSpeedEventOp>
            || std::is_same_v<T, DeleteScanSpeedEventOp>;
    }, op);
}

ChartEditApplyStats applyChartEditOpExtended(SongEditor& editor,
                                              int laneCount,
                                              const ChartEditOp& op) {
    (void)laneCount;
    ChartEditApplyStats st;

    std::visit([&](const auto& o) {
        using T = std::decay_t<decltype(o)>;

        if constexpr (std::is_same_v<T, AddDiskRotationOp>) {
            DiskRotationEvent e{};
            e.startTime   = o.startTime;
            e.duration    = std::max(0.f, o.duration);
            e.targetAngle = o.targetAngle;
            e.easing      = parseEasing(o.easing);
            auto& v = editor.diskRot();
            auto pos = std::upper_bound(v.begin(), v.end(), e,
                [](const DiskRotationEvent& a, const DiskRotationEvent& b) {
                    return a.startTime < b.startTime;
                });
            v.insert(pos, e);
            st.inserted = 1;
        }
        else if constexpr (std::is_same_v<T, AddDiskMoveOp>) {
            DiskMoveEvent e{};
            e.startTime = o.startTime;
            e.duration  = std::max(0.f, o.duration);
            e.target    = { o.targetX, o.targetY };
            e.easing    = parseEasing(o.easing);
            auto& v = editor.diskMove();
            auto pos = std::upper_bound(v.begin(), v.end(), e,
                [](const DiskMoveEvent& a, const DiskMoveEvent& b) {
                    return a.startTime < b.startTime;
                });
            v.insert(pos, e);
            st.inserted = 1;
        }
        else if constexpr (std::is_same_v<T, AddDiskScaleOp>) {
            DiskScaleEvent e{};
            e.startTime   = o.startTime;
            e.duration    = std::max(0.f, o.duration);
            e.targetScale = o.targetScale;
            e.easing      = parseEasing(o.easing);
            auto& v = editor.diskScale();
            auto pos = std::upper_bound(v.begin(), v.end(), e,
                [](const DiskScaleEvent& a, const DiskScaleEvent& b) {
                    return a.startTime < b.startTime;
                });
            v.insert(pos, e);
            st.inserted = 1;
        }
        else if constexpr (std::is_same_v<T, DeleteDiskEventOp>) {
            auto matchTime = [&](auto& vec) {
                auto before = vec.size();
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                    [&](const auto& e) {
                        return std::fabs(e.startTime - o.startTime) <= 0.01f;
                    }), vec.end());
                st.deleted += (int)(before - vec.size());
            };
            if      (o.kind == "rotation") matchTime(editor.diskRot());
            else if (o.kind == "move")     matchTime(editor.diskMove());
            else if (o.kind == "scale")    matchTime(editor.diskScale());
        }
        else if constexpr (std::is_same_v<T, SetPageSpeedOp>) {
            auto& v = editor.scanPages();
            bool replaced = false;
            for (auto& p : v) {
                if (p.pageIndex == o.pageIndex) {
                    p.speed = o.speed;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                ScanPageOverride po{};
                po.pageIndex = o.pageIndex;
                po.speed     = o.speed;
                auto pos = std::upper_bound(v.begin(), v.end(), po,
                    [](const ScanPageOverride& a, const ScanPageOverride& b) {
                        return a.pageIndex < b.pageIndex;
                    });
                v.insert(pos, po);
                st.inserted = 1;
            } else {
                st.mutated = 1;
            }
        }
        else if constexpr (std::is_same_v<T, AddScanSpeedEventOp>) {
            ScanSpeedEvent e{};
            e.startTime   = o.startTime;
            e.duration    = std::max(0.f, o.duration);
            e.targetSpeed = o.targetSpeed;
            e.easing      = parseEasing(o.easing);
            auto& v = editor.scanSpeed();
            auto pos = std::upper_bound(v.begin(), v.end(), e,
                [](const ScanSpeedEvent& a, const ScanSpeedEvent& b) {
                    return a.startTime < b.startTime;
                });
            v.insert(pos, e);
            st.inserted = 1;
        }
        else if constexpr (std::is_same_v<T, DeleteScanSpeedEventOp>) {
            auto& v = editor.scanSpeed();
            auto before = v.size();
            v.erase(std::remove_if(v.begin(), v.end(),
                [&](const ScanSpeedEvent& e) {
                    return e.startTime >= o.tFrom && e.startTime <= o.tTo;
                }), v.end());
            st.deleted = (int)(before - v.size());
        }
        // Note-vector ops route here as a no-op — the dispatch above in
        // applyChartEditOp is where they actually execute.
    }, op);

    return st;
}

// ── Mode gating ─────────────────────────────────────────────────────────────
// Invoked by SongEditor after parseChartEditOps; disallowed ops get moved
// to lastError. The parser itself has no mode context, so gating happens
// at the call site where `song->gameMode` is reachable.

bool isOpAllowedForMode(const ChartEditOp& op, const std::string& modeName) {
    return std::visit([&](const auto& o) -> bool {
        using T = std::decay_t<decltype(o)>;
        (void)o;
        // Arc family: 3D Arcaea only.
        if constexpr (std::is_same_v<T, AddArcOp>
                   || std::is_same_v<T, DeleteArcOp>
                   || std::is_same_v<T, ShiftArcHeightOp>
                   || std::is_same_v<T, AddArcTapOp>
                   || std::is_same_v<T, DeleteArcTapOp>) {
            return modeName == "arcaea";
        }
        // Slide family: ScanLine / Cytus only.
        if constexpr (std::is_same_v<T, AddSlideOp>
                   || std::is_same_v<T, DeleteSlideOp>) {
            return modeName == "cytus";
        }
        // Hold-waypoint family: anywhere Hold exists — bandori, arcaea,
        // lanota. Cytus uses page-based holds and doesn't expose the
        // same waypoint model.
        if constexpr (std::is_same_v<T, AddHoldWaypointOp>
                   || std::is_same_v<T, RemoveHoldWaypointOp>
                   || std::is_same_v<T, SetHoldTransitionOp>) {
            return modeName == "bandori"
                || modeName == "arcaea"
                || modeName == "lanota";
        }
        // Disk animation family: Circle / Lanota only.
        if constexpr (std::is_same_v<T, AddDiskRotationOp>
                   || std::is_same_v<T, AddDiskMoveOp>
                   || std::is_same_v<T, AddDiskScaleOp>
                   || std::is_same_v<T, DeleteDiskEventOp>) {
            return modeName == "lanota";
        }
        // Scan-speed family: ScanLine / Cytus only.
        if constexpr (std::is_same_v<T, SetPageSpeedOp>
                   || std::is_same_v<T, AddScanSpeedEventOp>
                   || std::is_same_v<T, DeleteScanSpeedEventOp>) {
            return modeName == "cytus";
        }
        // All shared ops are allowed in every mode.
        return true;
    }, op);
}
