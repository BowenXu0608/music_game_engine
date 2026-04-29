// Phase 1b gate: verify that the unified chart loader recovers every field
// the Editor Copilot will need to emit in Phase 4-6 (arcs, slides, hold
// waypoints, disk animation, scan-speed events). Hand-crafted JSON fixtures
// are the ground truth; we assert the ChartLoader parse result matches.
//
// Phase 4 extension: also exercise the new arc / arctap ops end-to-end —
// parseChartEditOps → applyChartEditOp on an EditorNote vector — so the
// cascading-delete, parent-link fixup, and easing translation all get
// covered by a regression test.
//
// Exits 0 on success, 1 on any failed assertion so CI can wire it up.

#include "editor/ChartEditOps.h"
#include "game/chart/ChartLoader.h"
#include "game/chart/ChartTypes.h"
#include "ui/SongEditor.h"  // for EditorNote / EditorNoteType

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

namespace fs = std::filesystem;

static int g_failures = 0;

static void fail(const char* what, const std::string& detail = "") {
    std::printf("FAIL: %s%s%s\n", what,
                detail.empty() ? "" : " — ", detail.c_str());
    ++g_failures;
}

static bool nearly(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

#define CHECK(expr, msg) do {                                               \
    if (!(expr)) fail(msg, #expr);                                          \
} while (0)

#define CHECK_EQ(a, b, msg) do {                                            \
    if (!((a) == (b))) fail(msg);                                           \
} while (0)

#define CHECK_NEAR(a, b, msg) do {                                          \
    float _a = (float)(a); float _b = (float)(b);                           \
    if (!nearly(_a, _b)) {                                                  \
        char buf[256];                                                      \
        std::snprintf(buf, sizeof(buf), "%s (got=%f want=%f)", msg, _a, _b);\
        fail(buf);                                                          \
    }                                                                       \
} while (0)

// Fixture — every critical field for Phase 4-6 ops is represented here.
// Two successive arc segments share a color (Arc waypoint decomposition).
// Slide carries scanPath + samplePoints. Hold carries a multi-waypoint
// path exercising all four transition styles. Disk animation exercises
// all four easing values. Scan speed events AND per-page overrides are
// both present so we can check either pathway.
static const char* kFixtureJson = R"JSON({
  "version": "1.0",
  "title": "Roundtrip Fixture",
  "artist": "Phase 1b Gate",
  "offset": 0,
  "timing": { "bpm": 120.0, "timeSignature": "4/4" },
  "notes": [
    {"time": 1.0, "type": "tap",   "lane": 3},
    {"time": 2.0, "type": "hold",  "lane": 0, "duration": 2.5,
     "waypoints": [
       {"t": 0.0, "lane": 0, "len": 0.0, "style": "straight"},
       {"t": 0.8, "lane": 2, "len": 0.3, "style": "angle90"},
       {"t": 1.6, "lane": 4, "len": 0.3, "style": "curve"},
       {"t": 2.5, "lane": 6, "len": 0.3, "style": "rhomboid"}
     ],
     "samples": [0.25, 0.5, 0.75, 1.0, 1.25]
    },
    {"time": 5.0, "type": "flick", "lane": 5},
    {"time": 6.0, "type": "slide", "lane": 0, "duration": 0.8,
     "samples": [0.1, 0.4, 0.7],
     "scan": {"x": 0.1, "y": 0.2,
              "path": [[0.1, 0.2], [0.35, 0.4], [0.7, 0.6], [0.9, 0.8]]}
    },
    {"time": 8.0, "type": "arc",
     "startX": 0.1, "startY": 0.0, "endX": 0.5, "endY": 0.5,
     "duration": 1.0, "easeX": 2.0, "easeY": -2.0,
     "color": 0, "void": false},
    {"time": 9.0, "type": "arc",
     "startX": 0.5, "startY": 0.5, "endX": 0.9, "endY": 0.3,
     "duration": 1.0, "easeX": 3.0, "easeY": -3.0,
     "color": 0, "void": false},
    {"time": 8.5, "type": "arctap", "lane": 0, "arcX": 0.3, "arcY": 0.25}
  ],
  "diskAnimation": {
    "rotations": [
      {"startTime": 0.0, "duration": 2.0, "target": 3.14159, "easing": "linear"},
      {"startTime": 4.0, "duration": 1.0, "target": 6.28318, "easing": "quadInOut"}
    ],
    "moves": [
      {"startTime": 1.0, "duration": 1.5, "target": [0.25, -0.1], "easing": "sineInOut"}
    ],
    "scales": [
      {"startTime": 2.0, "duration": 2.0, "target": 1.5, "easing": "cubicInOut"}
    ]
  },
  "scanPages": [
    {"index": 0, "speed": 1.0},
    {"index": 3, "speed": 2.0},
    {"index": 5, "speed": 0.5}
  ],
  "markers": [0.5, 1.0, 1.5, 2.0]
})JSON";

int main() {
    // Write fixture to a temp file; loadUnified dispatches on .json extension
    // plus the "version" field.
    fs::path tmp = fs::temp_directory_path() / "mge_chart_roundtrip.json";
    {
        std::ofstream f(tmp);
        if (!f) { std::printf("FAIL: cannot write fixture to %s\n", tmp.string().c_str()); return 1; }
        f << kFixtureJson;
    }

    ChartData chart;
    try {
        chart = ChartLoader::load(tmp.string());
    } catch (const std::exception& e) {
        std::printf("FAIL: loader threw: %s\n", e.what());
        return 1;
    }

    // ── Header
    CHECK_EQ(chart.title,  std::string("Roundtrip Fixture"), "title");
    CHECK_EQ(chart.artist, std::string("Phase 1b Gate"),     "artist");

    // ── Notes — 7 events (arc split counts as 2, arctap as its own note)
    CHECK_EQ(chart.notes.size(), (size_t)7, "note count");
    if (chart.notes.size() != 7) {
        std::printf("  (cannot continue asserting note fields; got %zu notes)\n", chart.notes.size());
    } else {
        // Loader preserves JSON source order, so the order is:
        //  tap@1, hold@2, flick@5, slide@6, arc@8, arc@9, arctap@8.5
        const auto& tapEv  = chart.notes[0];
        const auto& holdEv = chart.notes[1];
        const auto& flickEv= chart.notes[2];
        const auto& slideEv= chart.notes[3];
        const auto& arc1Ev = chart.notes[4];
        const auto& arc2Ev = chart.notes[5];
        const auto& atapEv = chart.notes[6];

        CHECK(tapEv.type  == NoteType::Tap,    "notes[0] is tap");
        CHECK(holdEv.type == NoteType::Hold,   "notes[1] is hold");
        CHECK(flickEv.type== NoteType::Flick,  "notes[2] is flick");
        CHECK(slideEv.type== NoteType::Slide,  "notes[3] is slide");
        CHECK(arc1Ev.type == NoteType::Arc,    "notes[4] is arc");
        CHECK(atapEv.type == NoteType::ArcTap, "notes[5] is arctap");
        CHECK(arc2Ev.type == NoteType::Arc,    "notes[6] is arc");

        // Hold — multi-waypoints with all 4 transition styles
        if (auto* hd = std::get_if<HoldData>(&holdEv.data)) {
            CHECK_NEAR(hd->laneX,    0.f, "hold.laneX");
            CHECK_NEAR(hd->duration, 2.5f, "hold.duration");
            CHECK_EQ(hd->waypoints.size(), (size_t)4, "hold waypoint count");
            if (hd->waypoints.size() == 4) {
                CHECK_EQ((int)hd->waypoints[0].style, (int)HoldTransition::Straight, "wp[0] style");
                CHECK_EQ((int)hd->waypoints[1].style, (int)HoldTransition::Angle90,  "wp[1] style");
                CHECK_EQ((int)hd->waypoints[2].style, (int)HoldTransition::Curve,    "wp[2] style");
                CHECK_EQ((int)hd->waypoints[3].style, (int)HoldTransition::Rhomboid, "wp[3] style");
                CHECK_NEAR(hd->waypoints[0].tOffset, 0.0f, "wp[0] tOffset");
                CHECK_NEAR(hd->waypoints[2].tOffset, 1.6f, "wp[2] tOffset");
                CHECK_EQ(hd->waypoints[1].lane, 2, "wp[1] lane");
                CHECK_EQ(hd->waypoints[3].lane, 6, "wp[3] lane");
                CHECK_NEAR(hd->waypoints[1].transitionLen, 0.3f, "wp[1] transitionLen");
            }
            CHECK_EQ(hd->samplePoints.size(), (size_t)5, "hold sample count");
        } else { fail("hold variant"); }

        // Slide — scanPath + samplePoints
        if (auto* td = std::get_if<TapData>(&slideEv.data)) {
            CHECK_EQ(td->scanPath.size(), (size_t)4, "slide scanPath size");
            if (td->scanPath.size() == 4) {
                CHECK_NEAR(td->scanPath[0].first,  0.10f, "slide path[0].x");
                CHECK_NEAR(td->scanPath[0].second, 0.20f, "slide path[0].y");
                CHECK_NEAR(td->scanPath[3].first,  0.90f, "slide path[3].x");
                CHECK_NEAR(td->scanPath[3].second, 0.80f, "slide path[3].y");
            }
            CHECK_EQ(td->samplePoints.size(), (size_t)3, "slide sample count");
            if (td->samplePoints.size() == 3) {
                CHECK_NEAR(td->samplePoints[1], 0.4f, "slide sample[1]");
            }
            CHECK_NEAR(td->duration, 0.8f, "slide duration");
        } else { fail("slide variant"); }

        // Arc 1 — positive easing on X, negative on Y
        if (auto* ad = std::get_if<ArcData>(&arc1Ev.data)) {
            CHECK_NEAR(ad->startPos.x, 0.1f, "arc1 startX");
            CHECK_NEAR(ad->startPos.y, 0.0f, "arc1 startY");
            CHECK_NEAR(ad->endPos.x,   0.5f, "arc1 endX");
            CHECK_NEAR(ad->endPos.y,   0.5f, "arc1 endY");
            CHECK_NEAR(ad->duration,   1.0f, "arc1 duration");
            CHECK_NEAR(ad->curveXEase, 2.0f, "arc1 easeX (si)");
            CHECK_NEAR(ad->curveYEase,-2.0f, "arc1 easeY (so)");
            CHECK_EQ(ad->color, 0, "arc1 color");
            CHECK(ad->isVoid == false, "arc1 void");
        } else { fail("arc1 variant"); }

        // Arc 2 — deeper easing codes (sisi / siso)
        if (auto* ad = std::get_if<ArcData>(&arc2Ev.data)) {
            CHECK_NEAR(ad->curveXEase, 3.0f, "arc2 easeX (sisi)");
            CHECK_NEAR(ad->curveYEase,-3.0f, "arc2 easeY (siso)");
        } else { fail("arc2 variant"); }

        // ArcTap — stored as TapData with laneX = arcX, scanY = arcY
        if (auto* td = std::get_if<TapData>(&atapEv.data)) {
            CHECK_NEAR(td->laneX, 0.3f,  "arctap x");
            CHECK_NEAR(td->scanY, 0.25f, "arctap y");
        } else { fail("arctap variant"); }
    }

    // ── Disk animation — all 4 easing values represented
    CHECK_EQ(chart.diskAnimation.rotations.size(), (size_t)2, "disk rot count");
    CHECK_EQ(chart.diskAnimation.moves.size(),     (size_t)1, "disk move count");
    CHECK_EQ(chart.diskAnimation.scales.size(),    (size_t)1, "disk scale count");
    if (chart.diskAnimation.rotations.size() == 2) {
        CHECK_EQ((int)chart.diskAnimation.rotations[0].easing, (int)DiskEasing::Linear,    "disk rot[0] easing");
        CHECK_EQ((int)chart.diskAnimation.rotations[1].easing, (int)DiskEasing::QuadInOut, "disk rot[1] easing");
        CHECK_NEAR(chart.diskAnimation.rotations[1].targetAngle, 6.28318f, "disk rot[1] target");
    }
    if (!chart.diskAnimation.moves.empty()) {
        CHECK_EQ((int)chart.diskAnimation.moves[0].easing, (int)DiskEasing::SineInOut, "disk move easing");
        CHECK_NEAR(chart.diskAnimation.moves[0].target.x,  0.25f,  "disk move target.x");
        CHECK_NEAR(chart.diskAnimation.moves[0].target.y, -0.10f,  "disk move target.y");
    }
    if (!chart.diskAnimation.scales.empty()) {
        CHECK_EQ((int)chart.diskAnimation.scales[0].easing, (int)DiskEasing::CubicInOut, "disk scale easing");
        CHECK_NEAR(chart.diskAnimation.scales[0].targetScale, 1.5f, "disk scale target");
    }

    // ── Scan pages — the sparse source-of-truth format
    CHECK_EQ(chart.scanPageOverrides.size(), (size_t)3, "scan page override count");
    if (chart.scanPageOverrides.size() == 3) {
        CHECK_EQ(chart.scanPageOverrides[0].pageIndex, 0, "page override[0].index");
        CHECK_EQ(chart.scanPageOverrides[1].pageIndex, 3, "page override[1].index");
        CHECK_EQ(chart.scanPageOverrides[2].pageIndex, 5, "page override[2].index");
        CHECK_NEAR(chart.scanPageOverrides[1].speed, 2.0f, "page override[1].speed");
        CHECK_NEAR(chart.scanPageOverrides[2].speed, 0.5f, "page override[2].speed");
    }

    std::remove(tmp.string().c_str());

    // ── Phase 4: arc / arctap ops regression ─────────────────────────────────
    // Parse a bundle of arc ops, apply to an empty EditorNote vector, and
    // assert the post-apply state matches expectations.

    const char* kOpsJson = R"JSON({
      "explanation": "test bundle",
      "ops": [
        {"op":"add_arc", "time":1.0, "duration":2.0,
         "startX":0.1, "startY":0.0, "endX":0.7, "endY":0.9,
         "easeX":"si", "easeY":"so", "color":0, "void":false},
        {"op":"add_arc", "time":5.0, "duration":1.0,
         "startX":0.5, "startY":0.5, "endX":0.5, "endY":0.5,
         "easeX":"sisi", "easeY":"siso", "color":1, "void":true},
        {"op":"add_arctap", "time":2.0},
        {"op":"add_arctap", "time":5.3},
        {"op":"shift_arc_height", "from":0.0, "to":4.0, "delta":0.1}
      ]
    })JSON";

    auto parseRes = parseChartEditOps(kOpsJson);
    CHECK(parseRes.success, "ops parse");
    CHECK_EQ(parseRes.ops.size(), (size_t)5, "op count");

    std::vector<EditorNote> liveNotes;
    for (const auto& op : parseRes.ops)
        applyChartEditOp(liveNotes, /*laneCount=*/7, op);

    // Expect 4 notes after apply: 2 arcs + 2 arctaps.
    CHECK_EQ(liveNotes.size(), (size_t)4, "post-apply note count");
    if (liveNotes.size() == 4) {
        // Sort order after apply (sortByTime): arc@1, arctap@2, arc@5, arctap@5.3
        CHECK(liveNotes[0].type == EditorNoteType::Arc,    "liveNotes[0] arc");
        CHECK(liveNotes[1].type == EditorNoteType::ArcTap, "liveNotes[1] arctap");
        CHECK(liveNotes[2].type == EditorNoteType::Arc,    "liveNotes[2] arc");
        CHECK(liveNotes[3].type == EditorNoteType::ArcTap, "liveNotes[3] arctap");

        // First arc: easing si/so = 2/-2, color=0, plus +0.1 height shift
        CHECK_NEAR(liveNotes[0].arcEaseX,  2.0f, "arc0 easeX (si)");
        CHECK_NEAR(liveNotes[0].arcEaseY, -2.0f, "arc0 easeY (so)");
        CHECK_NEAR(liveNotes[0].arcStartY, 0.1f, "arc0 startY shifted");
        CHECK_NEAR(liveNotes[0].arcEndY,   1.0f, "arc0 endY shifted (clamped)");
        CHECK_EQ(liveNotes[0].arcColor, 0, "arc0 color");
        CHECK(liveNotes[0].arcIsVoid == false, "arc0 void");

        // Second arc: sisi/siso = 3/-3, void, no shift (outside range)
        CHECK_NEAR(liveNotes[2].arcEaseX,  3.0f, "arc1 easeX (sisi)");
        CHECK_NEAR(liveNotes[2].arcEaseY, -3.0f, "arc1 easeY (siso)");
        CHECK(liveNotes[2].arcIsVoid, "arc1 void");
        CHECK_NEAR(liveNotes[2].arcStartY, 0.5f, "arc1 startY untouched");

        // ArcTap parent links must point at the arcs (indices 0 and 2)
        CHECK_EQ(liveNotes[1].arcTapParent, 0, "arctap0 parent = arc0");
        CHECK_EQ(liveNotes[3].arcTapParent, 2, "arctap1 parent = arc1");
    }

    // Cascading delete: delete_arc [0, 2.5] should remove arc0 AND its
    // dependent arctap. Arc1 + its arctap survive.
    auto parseDel = parseChartEditOps(
        R"({"explanation":"","ops":[{"op":"delete_arc","from":0.0,"to":2.5}]})");
    CHECK(parseDel.success, "delete_arc parse");
    for (const auto& op : parseDel.ops)
        applyChartEditOp(liveNotes, 7, op);

    CHECK_EQ(liveNotes.size(), (size_t)2, "after cascade delete");
    if (liveNotes.size() == 2) {
        CHECK(liveNotes[0].type == EditorNoteType::Arc,    "survivor[0] arc");
        CHECK(liveNotes[1].type == EditorNoteType::ArcTap, "survivor[1] arctap");
        // Parent index must have been rewritten from 2 to 0 (only 2 notes left)
        CHECK_EQ(liveNotes[1].arcTapParent, 0, "arctap parent rewritten");
    }

    // Mode gating: arc ops must be rejected outside arcaea.
    AddArcOp probe;
    CHECK(isOpAllowedForMode(probe, "arcaea"),  "add_arc allowed in arcaea");
    CHECK(!isOpAllowedForMode(probe, "bandori"),"add_arc blocked in bandori");
    CHECK(!isOpAllowedForMode(probe, "lanota"), "add_arc blocked in lanota");
    CHECK(!isOpAllowedForMode(probe, "cytus"),  "add_arc blocked in cytus");

    // ── Phase 5: slide ops regression ────────────────────────────────────────
    const char* kSlideOpsJson = R"JSON({
      "explanation": "slide test",
      "ops": [
        {"op":"add_slide", "time":12.0, "duration":0.8,
         "scanX":0.1, "scanY":0.2,
         "path":[[0.1, 0.2], [0.4, 0.5], [0.9, 0.9]],
         "samples":[0.2, 0.5]},
        {"op":"add_slide", "time":20.0, "duration":1.0,
         "scanX":0.5, "scanY":0.1,
         "path":[[0.5, 0.1], [0.5, 0.9]],
         "samples":[0.3, 0.6, 0.9]}
      ]
    })JSON";
    auto slideParse = parseChartEditOps(kSlideOpsJson);
    CHECK(slideParse.success, "slide parse");
    CHECK_EQ(slideParse.ops.size(), (size_t)2, "slide op count");

    std::vector<EditorNote> slideNotes;
    for (const auto& op : slideParse.ops)
        applyChartEditOp(slideNotes, /*laneCount=*/7, op);

    CHECK_EQ(slideNotes.size(), (size_t)2, "slide post-apply count");
    if (slideNotes.size() == 2) {
        CHECK(slideNotes[0].type == EditorNoteType::Slide, "slide[0] type");
        CHECK_EQ(slideNotes[0].scanPath.size(), (size_t)3, "slide[0] path size");
        CHECK_NEAR(slideNotes[0].scanPath[2].first,  0.9f, "slide[0] path[2].x");
        CHECK_NEAR(slideNotes[0].scanPath[2].second, 0.9f, "slide[0] path[2].y");
        CHECK_EQ(slideNotes[0].samplePoints.size(), (size_t)2, "slide[0] sample count");
        CHECK_NEAR(slideNotes[0].endTime - slideNotes[0].time, 0.8f, "slide[0] duration");

        CHECK_EQ(slideNotes[1].scanPath.size(), (size_t)2, "slide[1] path size");
        CHECK_NEAR(slideNotes[1].scanPath[1].second, 0.9f, "slide[1] path[1].y");
    }

    // Delete one slide
    auto slideDel = parseChartEditOps(
        R"({"explanation":"","ops":[{"op":"delete_slide","from":18.0,"to":22.0}]})");
    for (const auto& op : slideDel.ops)
        applyChartEditOp(slideNotes, 7, op);
    CHECK_EQ(slideNotes.size(), (size_t)1, "post-delete_slide count");

    // Mode gating for slide ops
    AddSlideOp slideProbe;
    CHECK(isOpAllowedForMode(slideProbe, "cytus"),    "add_slide allowed in cytus");
    CHECK(!isOpAllowedForMode(slideProbe, "bandori"), "add_slide blocked in bandori");
    CHECK(!isOpAllowedForMode(slideProbe, "arcaea"),  "add_slide blocked in arcaea");
    CHECK(!isOpAllowedForMode(slideProbe, "lanota"),  "add_slide blocked in lanota");

    // ── Phase 6: hold-waypoint ops regression ────────────────────────────────
    // Seed a lone Hold, then exercise add/remove/setTransition.
    std::vector<EditorNote> holdNotes;
    {
        EditorNote h{};
        h.type     = EditorNoteType::Hold;
        h.time     = 4.0f;
        h.endTime  = 6.0f;
        h.track    = 1;
        h.transition = EditorHoldTransition::Straight;
        holdNotes.push_back(h);
    }

    const char* kHoldOpsJson = R"JSON({
      "explanation": "hold wp test",
      "ops": [
        {"op":"add_hold_waypoint", "note_time":4.0, "at_time":4.8,
         "lane":3, "style":"curve"},
        {"op":"add_hold_waypoint", "note_time":4.0, "at_time":5.6,
         "lane":1, "style":"rhomboid"}
      ]
    })JSON";
    auto holdParse = parseChartEditOps(kHoldOpsJson);
    CHECK(holdParse.success, "hold wp parse");
    CHECK_EQ(holdParse.ops.size(), (size_t)2, "hold wp op count");

    for (const auto& op : holdParse.ops)
        applyChartEditOp(holdNotes, /*laneCount=*/7, op);

    CHECK_EQ(holdNotes.size(), (size_t)1, "holds still 1");
    CHECK_EQ(holdNotes[0].waypoints.size(), (size_t)2, "2 waypoints");
    if (holdNotes[0].waypoints.size() == 2) {
        CHECK_NEAR(holdNotes[0].waypoints[0].tOffset, 0.8f, "wp[0] tOffset");
        CHECK_NEAR(holdNotes[0].waypoints[1].tOffset, 1.6f, "wp[1] tOffset");
        CHECK_EQ(holdNotes[0].waypoints[0].lane, 3, "wp[0] lane");
        CHECK_EQ((int)holdNotes[0].waypoints[0].style,
                 (int)EditorHoldTransition::Curve, "wp[0] style");
        CHECK_EQ((int)holdNotes[0].waypoints[1].style,
                 (int)EditorHoldTransition::Rhomboid, "wp[1] style");
    }

    // Remove the first waypoint
    auto holdRm = parseChartEditOps(
        R"({"explanation":"","ops":[{"op":"remove_hold_waypoint","note_time":4.0,"at_time":4.8}]})");
    for (const auto& op : holdRm.ops)
        applyChartEditOp(holdNotes, 7, op);
    CHECK_EQ(holdNotes[0].waypoints.size(), (size_t)1, "1 waypoint after remove");

    // Bulk set_hold_transition — should rewrite both the legacy field and
    // the surviving waypoint's style
    auto setTx = parseChartEditOps(
        R"({"explanation":"","ops":[{"op":"set_hold_transition","from":3.0,"to":5.0,"style":"angle90"}]})");
    for (const auto& op : setTx.ops)
        applyChartEditOp(holdNotes, 7, op);
    CHECK_EQ((int)holdNotes[0].transition, (int)EditorHoldTransition::Angle90,
             "hold transition set");
    if (!holdNotes[0].waypoints.empty()) {
        CHECK_EQ((int)holdNotes[0].waypoints[0].style,
                 (int)EditorHoldTransition::Angle90, "wp style rewritten");
    }

    // Mode gating for hold ops
    AddHoldWaypointOp hwpProbe;
    CHECK(isOpAllowedForMode(hwpProbe, "bandori"), "hold wp allowed in bandori");
    CHECK(isOpAllowedForMode(hwpProbe, "arcaea"),  "hold wp allowed in arcaea");
    CHECK(isOpAllowedForMode(hwpProbe, "lanota"),  "hold wp allowed in lanota");
    CHECK(!isOpAllowedForMode(hwpProbe, "cytus"),  "hold wp blocked in cytus");

    // ── Phase 7: disk + scan-speed ops regression ────────────────────────────
    // `isExtendedOp` routes these through applyChartEditOpExtended, so the
    // note-vector apply path must be a no-op for them.
    AddDiskRotationOp diskProbe;
    AddDiskMoveOp diskMoveProbe;
    SetPageSpeedOp pageProbe;
    CHECK(isExtendedOp(diskProbe),     "add_disk_rotation is extended");
    CHECK(isExtendedOp(diskMoveProbe), "add_disk_move is extended");
    CHECK(isExtendedOp(pageProbe),     "set_page_speed is extended");

    DeleteRangeOp dProbe;
    CHECK(!isExtendedOp(dProbe),       "delete_range is not extended");

    // Mode gating for the extended families
    CHECK(isOpAllowedForMode(diskProbe, "lanota"),   "disk rot allowed in lanota");
    CHECK(!isOpAllowedForMode(diskProbe, "bandori"), "disk rot blocked in bandori");
    CHECK(!isOpAllowedForMode(diskProbe, "arcaea"),  "disk rot blocked in arcaea");
    CHECK(!isOpAllowedForMode(diskProbe, "cytus"),   "disk rot blocked in cytus");
    CHECK(isOpAllowedForMode(pageProbe, "cytus"),    "page speed allowed in cytus");
    CHECK(!isOpAllowedForMode(pageProbe, "lanota"),  "page speed blocked in lanota");

    // Parse a bundle of disk/scan ops and confirm the JSON dispatch builds
    // each variant. We can't exercise applyChartEditOpExtended here without
    // a fully-constructed SongEditor (needs ImGui context, etc.); that's
    // covered by the editor smoke test. Parsing is the unit-level gate.
    const char* kExtOpsJson = R"JSON({
      "explanation": "ext test",
      "ops": [
        {"op":"add_disk_rotation", "start_time":16.0, "duration":2.0,
         "target":1.5708, "easing":"sineInOut"},
        {"op":"add_disk_move", "start_time":16.0, "duration":1.0,
         "target":[0.25, -0.10], "easing":"linear"},
        {"op":"add_disk_scale", "start_time":18.0, "duration":1.0,
         "target":1.5, "easing":"cubicInOut"},
        {"op":"delete_disk_event", "kind":"rotation", "start_time":16.0},
        {"op":"set_page_speed", "page_index":4, "speed":1.5},
        {"op":"add_scan_speed_event", "start_time":40.0, "duration":2.0,
         "target":1.5, "easing":"quadInOut"},
        {"op":"delete_scan_speed_event", "from":40.0, "to":44.0}
      ]
    })JSON";
    auto extParse = parseChartEditOps(kExtOpsJson);
    CHECK(extParse.success, "ext ops parse");
    CHECK_EQ(extParse.ops.size(), (size_t)7, "ext op count");
    // Each op should route as extended
    for (const auto& op : extParse.ops) {
        CHECK(isExtendedOp(op), "all ext ops tagged extended");
    }

    if (g_failures) {
        std::printf("\n%d assertion(s) failed\n", g_failures);
        return 1;
    }
    std::printf("chart_roundtrip_test: PASS (%zu notes, disk+scan recovered; "
                "arc ops + cascade delete + mode gate OK)\n",
                chart.notes.size());
    return 0;
}
