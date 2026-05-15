#pragma once
#include "ui/SongEditor.h"   // for EditorNote, MarkerFeature
#include "game/chart/ChartTypes.h"  // DiskRotation/Move/Scale, ScanSpeedEvent, ScanPageOverride
#include "renderer/MaterialAsset.h" // MaterialAsset (Copilot material undo)

#include <vector>
#include <unordered_map>
#include <cstdint>

// Single-level undo payload for the Editor Copilot.
//
// Captures everything the op vocabulary can mutate today for the active
// difficulty: notes, markers, per-marker analyzer features, disk animation
// keyframes, scan-speed / scan-page overrides, and (material op) the active
// difficulty's slot-override map plus the pre-edit copies of the mode's
// default material assets.
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

    // SetPbrMaterialOp undo. `matCaptured` gates restore so non-material
    // sessions don't touch the library. `matAssets` holds pre-edit copies of
    // every default asset for the mode's slots (bounded, ≤ slot count);
    // restore re-upserts them. `matOverrides` is the active difficulty's
    // slot→asset map before the edit.
    bool                                                   matCaptured = false;
    std::unordered_map<uint16_t, ChartData::MaterialData>  matOverrides;
    std::vector<MaterialAsset>                             matAssets;
};
