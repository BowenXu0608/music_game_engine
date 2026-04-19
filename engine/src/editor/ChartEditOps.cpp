#include "ChartEditOps.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <sstream>

using nlohmann::json;

namespace {

EditorNoteType parseType(const std::string& s) {
    if (s == "tap" || s == "click") return EditorNoteType::Tap;
    if (s == "hold")                return EditorNoteType::Hold;
    if (s == "flick")               return EditorNoteType::Flick;
    // Slide / Arc / ArcTap deliberately rejected — those aren't in scope for
    // the copilot's op vocabulary. Fall back to Tap so a bad type string from
    // the model doesn't silently break the edit.
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

// Strip optional ```json ... ``` or ``` ... ``` fences, and any leading /
// trailing prose outside the first {...} pair, so we can parse what's left.
std::string extractJsonEnvelope(const std::string& msg) {
    // Find the first '{' and the matching closing '}'. This is permissive —
    // models sometimes prepend or append explanation text.
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
        ChartEditOp op;
        std::string kind = item.value("op", "");
        auto tFrom = (float)item.value("from", 0.0);
        auto tTo   = (float)item.value("to",   0.0);
        if (tTo < tFrom) std::swap(tFrom, tTo);
        op.tFrom = tFrom;
        op.tTo   = tTo;

        if      (kind == "delete_range") {
            op.kind = ChartEditOpKind::DeleteRange;
            op.typeFilter = item.value("type_filter", "any");
        }
        else if (kind == "insert") {
            op.kind = ChartEditOpKind::Insert;
            op.time  = (float)item.value("time", 0.0);
            op.track = (int)  item.value("track", 0);
            op.type  = parseType(item.value("type", "tap"));
            op.duration = (float)item.value("duration", 0.3);
        }
        else if (kind == "mirror_lanes") {
            op.kind = ChartEditOpKind::MirrorLanes;
        }
        else if (kind == "shift_lanes") {
            op.kind = ChartEditOpKind::ShiftLanes;
            op.deltaLane = (int)item.value("delta", 0);
        }
        else if (kind == "shift_time") {
            op.kind = ChartEditOpKind::ShiftTime;
            op.deltaSec = (float)item.value("delta", 0.0);
        }
        else if (kind == "convert_type") {
            op.kind = ChartEditOpKind::ConvertType;
            op.fromType = parseType(item.value("from_type", "tap"));
            op.type     = parseType(item.value("to_type",   "tap"));
            op.duration = (float)item.value("duration", 0.3);
        }
        else {
            // Unknown op — skip silently; the AI may emit something future.
            continue;
        }
        out.ops.push_back(op);
    }
    out.success = true;
    return out;
}

std::string describeChartEditOp(const ChartEditOp& op) {
    char buf[256];
    switch (op.kind) {
        case ChartEditOpKind::DeleteRange:
            std::snprintf(buf, sizeof(buf),
                "delete_range  %.2f-%.2fs  filter=%s",
                op.tFrom, op.tTo,
                op.typeFilter.empty() ? "any" : op.typeFilter.c_str());
            return buf;
        case ChartEditOpKind::Insert:
            std::snprintf(buf, sizeof(buf),
                "insert        t=%.2fs track=%d type=%s%s",
                op.time, op.track, typeName(op.type),
                op.type == EditorNoteType::Hold ? "" : "");
            return buf;
        case ChartEditOpKind::MirrorLanes:
            std::snprintf(buf, sizeof(buf),
                "mirror_lanes  %.2f-%.2fs", op.tFrom, op.tTo);
            return buf;
        case ChartEditOpKind::ShiftLanes:
            std::snprintf(buf, sizeof(buf),
                "shift_lanes   %.2f-%.2fs  delta=%+d",
                op.tFrom, op.tTo, op.deltaLane);
            return buf;
        case ChartEditOpKind::ShiftTime:
            std::snprintf(buf, sizeof(buf),
                "shift_time    %.2f-%.2fs  delta=%+.3fs",
                op.tFrom, op.tTo, op.deltaSec);
            return buf;
        case ChartEditOpKind::ConvertType:
            std::snprintf(buf, sizeof(buf),
                "convert_type  %.2f-%.2fs  %s->%s",
                op.tFrom, op.tTo,
                typeName(op.fromType), typeName(op.type));
            return buf;
        default: return "(unknown op)";
    }
}

ChartEditApplyStats applyChartEditOp(std::vector<EditorNote>& notes,
                                      int laneCount,
                                      const ChartEditOp& op) {
    ChartEditApplyStats st;
    if (laneCount < 1) laneCount = 1;
    auto inRange = [&](float t) { return t >= op.tFrom && t <= op.tTo; };
    auto matchesFilter = [&](const EditorNote& n) {
        if (op.typeFilter.empty() || op.typeFilter == "any") return true;
        if (op.typeFilter == "tap")   return n.type == EditorNoteType::Tap;
        if (op.typeFilter == "hold")  return n.type == EditorNoteType::Hold;
        if (op.typeFilter == "flick") return n.type == EditorNoteType::Flick;
        return true;
    };
    auto clampTrack = [&](int t) {
        if (t < 0) t = 0;
        if (t >= laneCount) t = laneCount - 1;
        return t;
    };

    switch (op.kind) {
        case ChartEditOpKind::DeleteRange: {
            auto before = notes.size();
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                [&](const EditorNote& n) {
                    return inRange(n.time) && matchesFilter(n);
                }), notes.end());
            st.deleted = (int)(before - notes.size());
            break;
        }
        case ChartEditOpKind::Insert: {
            EditorNote n{};
            n.type  = op.type;
            n.time  = op.time;
            n.track = clampTrack(op.track);
            if (op.type == EditorNoteType::Hold)
                n.endTime = op.time + std::max(0.05f, op.duration);
            notes.push_back(n);
            std::sort(notes.begin(), notes.end(),
                [](const EditorNote& a, const EditorNote& b) {
                    return a.time < b.time;
                });
            st.inserted = 1;
            break;
        }
        case ChartEditOpKind::MirrorLanes: {
            for (auto& n : notes) {
                if (!inRange(n.time)) continue;
                n.track = clampTrack((laneCount - 1) - n.track);
                if (n.endTrack >= 0)
                    n.endTrack = clampTrack((laneCount - 1) - n.endTrack);
                ++st.mutated;
            }
            break;
        }
        case ChartEditOpKind::ShiftLanes: {
            for (auto& n : notes) {
                if (!inRange(n.time)) continue;
                n.track = clampTrack(n.track + op.deltaLane);
                if (n.endTrack >= 0)
                    n.endTrack = clampTrack(n.endTrack + op.deltaLane);
                ++st.mutated;
            }
            break;
        }
        case ChartEditOpKind::ShiftTime: {
            for (auto& n : notes) {
                if (!inRange(n.time)) continue;
                n.time += op.deltaSec;
                if (n.endTime > 0.f) n.endTime += op.deltaSec;
                ++st.mutated;
            }
            std::sort(notes.begin(), notes.end(),
                [](const EditorNote& a, const EditorNote& b) {
                    return a.time < b.time;
                });
            break;
        }
        case ChartEditOpKind::ConvertType: {
            for (auto& n : notes) {
                if (!inRange(n.time)) continue;
                if (n.type != op.fromType) continue;
                n.type = op.type;
                if (op.type == EditorNoteType::Hold && n.endTime <= n.time)
                    n.endTime = n.time + std::max(0.05f, op.duration);
                if (op.type != EditorNoteType::Hold)
                    n.endTime = 0.f;
                ++st.mutated;
            }
            break;
        }
        default: break;
    }
    return st;
}
