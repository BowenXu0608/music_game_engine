#pragma once
#include "ui/SongEditor.h"  // for EditorNote, MarkerFeature

#include <vector>

// Single-level undo payload for the Editor Copilot.
//
// Captures exactly what the op vocabulary can mutate today: notes, markers,
// and per-marker analyzer features for the active difficulty. Materials,
// scan-line pages, and disk animation stay outside this snapshot because the
// Phase 1 ops don't touch them.
struct ChartSnapshot {
    std::vector<EditorNote>    notes;
    std::vector<float>         markers;
    std::vector<MarkerFeature> features;
};
