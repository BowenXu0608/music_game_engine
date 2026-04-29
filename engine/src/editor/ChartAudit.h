#pragma once
#include "ui/SongEditor.h"  // for EditorNote / EditorNoteType

#include <string>
#include <vector>

// Read-only chart auditor for the "Chart Audit" panel in SongEditor.
//
// Workflow:
//   1. computeAuditMetrics(notes, duration) scans the chart locally and
//      flags density spikes, jacks, crossovers, dead zones. Pure C++.
//   2. describeMetricsForPrompt(metrics) renders the findings as a compact
//      text block fed to the LLM as pre-digested facts so a small model
//      (qwen2.5:3b) only has to prioritize + narrate, not reason about times.
//   3. LLM responds with JSON {summary, issues:[...]}.
//   4. parseAuditReport(message) parses that reply for the UI.
//
// Mirrors the envelope-parsing style used by ChartEditOps for the copilot.

enum class AuditSeverity { Low, Medium, High };

struct AuditIssue {
    AuditSeverity severity  = AuditSeverity::Low;
    float         timeStart = 0.f;
    float         timeEnd   = 0.f;  // == timeStart for point issues
    std::string   category;   // density|jack|crossover|pacing|difficulty|other
    std::string   message;
};

struct AuditReport {
    bool                    success = false;
    std::string             summary;
    std::vector<AuditIssue> issues;
    std::string             errorMessage;
};

struct AuditMetrics {
    int   noteCount    = 0;
    int   tapCount     = 0;
    int   holdCount    = 0;
    int   flickCount   = 0;
    int   otherCount   = 0;  // slide / arc / arctap
    float songDuration = 0.f;
    float peakNps      = 0.f;  // max notes/s in any 2s sliding window
    float avgNps       = 0.f;  // noteCount / songDuration

    struct DensitySpike { float start; float end; int count; };
    std::vector<DensitySpike> densityHotspots;

    struct Jack { float time; int lane; int repeats; };
    std::vector<Jack> jacks;

    struct Crossover { float time; int laneJump; };
    std::vector<Crossover> crossovers;

    struct DeadZone { float start; float end; };
    std::vector<DeadZone> deadZones;

    // ── Marker-derived stats (filled when markers are passed in) ────────────
    int   markerCount = 0;

    // Marker density (independent of notes — useful when the chart only has
    // detected events authored). peak = max markers in any 2s sliding window;
    // avg = markerCount / songDuration.
    float peakMarkerRate2s = 0.f;
    float avgMarkerRate    = 0.f;

    // Time windows where the analyzer found a sustained burst of events. Same
    // shape as densityHotspots but driven by markers, so the audit can flag
    // an unworkable dense passage even before any notes are placed.
    std::vector<DensitySpike> markerDensityHotspots;

    // Long gaps between consecutive markers ("musical silence"). Same shape
    // as note-side deadZones; useful to confirm a sparse intro/outro.
    std::vector<DeadZone> markerDeadZones;

    // Notes placed where the analyzer detected nothing (possible misalignment
    // or off-beat note). Note-quality only — has nothing to say when there
    // are no notes yet, but stays visible once authoring starts.
    struct UnalignedNote { float time; int lane; float nearestMarkerDt; };
    std::vector<UnalignedNote> unalignedNotes;
};

// Scan the authored note list and populate AuditMetrics. songDuration may be
// 0 - falls back to the last note's endTime/time. markers (optional) is the
// AI-detected event list; when non-empty, marker-coverage stats are computed
// (orphan markers + notes that don't sit on any detected event).
AuditMetrics computeAuditMetrics(const std::vector<EditorNote>& notes,
                                  float songDuration,
                                  const std::vector<float>& markers = {});

// Format metrics as a short text block for the LLM prompt. Caps each
// category at a small fixed count so prompt size stays bounded.
std::string describeMetricsForPrompt(const AuditMetrics& m);

// Accept the raw assistant message; strips optional ```fences``` and any
// prose outside the first {..} pair; fills AuditReport. errorMessage is set
// on parse failure.
AuditReport parseAuditReport(const std::string& assistantMsg);
