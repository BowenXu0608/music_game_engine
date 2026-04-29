#pragma once
#include "ui/SongEditor.h"   // for EditorNote, MarkerFeature
#include "game/chart/ChartTypes.h"  // DiskRotation/Move/Scale, ScanSpeedEvent, ScanPageOverride

#include <vector>

// Single-level undo payload for the Editor Copilot.
//
// Captures everything the op vocabulary can mutate today for the active
// difficulty: notes, markers, per-marker analyzer features, plus (Phase 7)
// disk animation keyframes and scan-speed / scan-page overrides. Materials
// live outside this payload because there are no material ops yet.
//
// Fields not relevant to the current mode are simply empty after capture;
// restore writes them back anyway — harmless for idle modes.
struct ChartSnapshot {
    std::vector<EditorNote>        notes;
    std::vector<float>             markers;
    std::vector<MarkerFeature>     features;

    // Phase 7 additions (disk + scan-speed ops).
    std::vector<DiskRotationEvent> diskRot;
    std::vector<DiskMoveEvent>     diskMove;
    std::vector<DiskScaleEvent>    diskScale;
    std::vector<ScanSpeedEvent>    scanSpeed;
    std::vector<ScanPageOverride>  scanPages;
};
