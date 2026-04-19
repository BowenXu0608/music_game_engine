#pragma once
#include "ui/SongEditor.h"              // EditorNote / EditorNoteType / Difficulty
#include "ui/MusicSelectionEditor.h"    // Difficulty (enum class)
#include "engine/AudioAnalyzer.h"       // MarkerFeature
#include "game/chart/ChartTypes.h"      // ChartData, NoteEvent
// ProjectHub.h brings GameModeConfig via the indirect include chain below;
// pull it in directly to keep this header self-contained.
#include "ui/ProjectHub.h"

#include <string>
#include <vector>

// Style Transfer module.
//
// Computes a "style fingerprint" from a reference ChartData (type ratios,
// lane histogram, NPS stats, transition stats) and applies it to the
// in-editor note list so the current chart shifts toward the reference's
// feel while preserving note times.
//
// The AI narration step is driven from SongEditor; this module only
// produces the structured data + text the LLM is primed with.

struct StyleFingerprint {
    int   noteCount  = 0;
    int   trackCount = 0;
    float durationSec = 0.f;
    float tapPct   = 0.f, holdPct = 0.f, flickPct = 0.f;  // Tap/Hold/Flick only, sums to 1
    float avgNps   = 0.f, peakNps2s = 0.f;
    std::vector<float> laneHist;        // size == trackCount, sums to 1
    float meanDLane = 0.f;              // avg |lane_i - lane_{i-1}| across eligible notes
    float sameLaneRepeatRate = 0.f;     // fraction of consecutive pairs on same lane
};

struct StyleCandidate {
    std::string label;          // "<set> / <song> [Difficulty] (Nt)"
    std::string absPath;        // project-absolute .json chart path
    int         trackCount = 0; // reference song's native trackCount
};

struct StyleTransferOptions {
    bool  supportsHold = true;
    float holdMinSec   = 0.2f;
    bool  antiJack     = true;
};

struct StyleTransferStats {
    int retyped = 0;
    int relaned = 0;
    int skipped = 0;  // non-rebalanceable notes (Slide/Arc/ArcTap or cross-lane holds)
};

// Compute a fingerprint from a loaded ChartData. Only Tap/Hold/Flick notes
// count toward type ratios + lane histogram; other types are ignored.
StyleFingerprint computeFingerprint(const ChartData& chart, int trackCount);

// Same but from the editor's own EditorNote list (used for before/after).
StyleFingerprint computeFingerprintFromEditor(const std::vector<EditorNote>& notes,
                                               int trackCount, float durationSec);

// Human-readable text block for UI display + LLM prompt.
std::string describeFingerprint(const StyleFingerprint& fp);

// Enumerate reference-chart candidates from the in-memory MusicSelection
// (so newly-added songs show up without a prior save). Filters by
// (type + dimension + trackCount) equality and excludes the currently
// edited slot. Relative chart paths are resolved against projectPath.
std::vector<StyleCandidate> enumerateStyleCandidates(
    const std::string& projectPath,
    const std::vector<MusicSetInfo>& sets,
    const GameModeConfig& currentMode,
    const std::string& currentSongName,
    Difficulty currentDifficulty);

// Rebalance `notes` (in-place) toward `target`. Eligible notes = Tap/Hold/
// Flick with single-lane geometry; everything else is skipped.
//
// `markerTimes` + `features` are parallel-indexed (same as SongEditor's
// markers()/features()). When markers are empty the rebalance still runs,
// it just uses deterministic defaults instead of audio-driven scoring.
StyleTransferStats applyStyleTransfer(std::vector<EditorNote>& notes,
                                       const std::vector<MarkerFeature>& features,
                                       const std::vector<float>& markerTimes,
                                       int trackCount,
                                       const StyleFingerprint& target,
                                       const StyleTransferOptions& opts);
