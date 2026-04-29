#include "ChartAudit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using nlohmann::json;

namespace {

AuditSeverity parseSeverity(const std::string& s) {
    if (s == "high")                    return AuditSeverity::High;
    if (s == "medium" || s == "med")    return AuditSeverity::Medium;
    return AuditSeverity::Low;
}

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

} // namespace

AuditMetrics computeAuditMetrics(const std::vector<EditorNote>& notes,
                                  float songDuration,
                                  const std::vector<float>& markers) {
    AuditMetrics m;
    for (const auto& n : notes) {
        switch (n.type) {
            case EditorNoteType::Tap:   ++m.tapCount;   break;
            case EditorNoteType::Hold:  ++m.holdCount;  break;
            case EditorNoteType::Flick: ++m.flickCount; break;
            default: ++m.otherCount; break;
        }
    }
    m.noteCount = (int)notes.size();

    std::vector<const EditorNote*> sorted;
    sorted.reserve(notes.size());
    for (const auto& n : notes) sorted.push_back(&n);
    std::sort(sorted.begin(), sorted.end(),
        [](const EditorNote* a, const EditorNote* b) { return a->time < b->time; });

    float dur = songDuration;
    if (dur <= 0.f) {
        for (const auto* np : sorted) {
            float e = np->endTime > np->time ? np->endTime : np->time;
            if (e > dur) dur = e;
        }
        // If there are no notes yet (e.g. just after Analyze Beats), fall
        // back to the last marker so density / NPS denominators stay sane.
        for (float mt : markers) if (mt > dur) dur = mt;
    }
    m.songDuration = dur;
    m.avgNps = (dur > 0.f) ? (float)m.noteCount / dur : 0.f;

    // Peak NPS in a 2s sliding window (onset-driven; ignores hold tail).
    {
        const float windowSec = 2.f;
        int best = 0;
        size_t head = 0;
        for (size_t tail = 0; tail < sorted.size(); ++tail) {
            while (head < sorted.size() &&
                   sorted[head]->time < sorted[tail]->time - windowSec)
                ++head;
            int count = (int)(tail - head + 1);
            if (count > best) best = count;
        }
        m.peakNps = best / windowSec;
    }

    // Density hotspots: scan twice with different (winSec, threshold) pairs
    // and union the results, so we catch BOTH sustained density AND short
    // bursts. The previous single 4 s @ ≥24 (~6 NPS sustained) pass missed
    // brief 10–12 NPS spikes that were clearly authoring problems; adding a
    // 1 s @ ≥8 burst pass surfaces those, and lowering the long pass to
    // 4 s @ ≥16 (~4 NPS) makes mid-density hard charts trigger as well.
    auto scanWindow = [&](float winSec, int threshold,
                          std::vector<AuditMetrics::DensitySpike>& out) {
        size_t head = 0;
        float  lastEnd  = -1e9f;
        int    runBest  = 0;
        float  runStart = 0.f;
        float  runEnd   = 0.f;
        for (size_t tail = 0; tail < sorted.size(); ++tail) {
            while (head < sorted.size() &&
                   sorted[head]->time < sorted[tail]->time - winSec)
                ++head;
            int count = (int)(tail - head + 1);
            if (count < threshold) continue;
            float ws = sorted[head]->time;
            float we = sorted[tail]->time;
            if (ws - lastEnd > winSec) {
                if (runBest > 0) out.push_back({runStart, runEnd, runBest});
                runBest  = count;
                runStart = ws;
                runEnd   = we;
            } else {
                if (count > runBest) runBest = count;
                runEnd = we;
            }
            lastEnd = we;
        }
        if (runBest > 0) out.push_back({runStart, runEnd, runBest});
    };

    {
        std::vector<AuditMetrics::DensitySpike> sustained;
        std::vector<AuditMetrics::DensitySpike> bursts;
        scanWindow(4.f, 16, sustained);   // ~4 NPS sustained over 4 s
        scanWindow(1.f,  8, bursts);      // ~8 NPS spike inside any 1 s

        // Merge: sustained hotspots win when they overlap a burst (we'd
        // rather report the wider, more useful range). Otherwise both
        // make it into the final list.
        auto overlaps = [](const AuditMetrics::DensitySpike& a,
                           const AuditMetrics::DensitySpike& b) {
            return !(a.end < b.start || b.end < a.start);
        };
        m.densityHotspots = sustained;
        for (const auto& bs : bursts) {
            bool absorbed = false;
            for (const auto& sus : sustained) {
                if (overlaps(bs, sus)) { absorbed = true; break; }
            }
            if (!absorbed) m.densityHotspots.push_back(bs);
        }
        std::sort(m.densityHotspots.begin(), m.densityHotspots.end(),
                  [](const AuditMetrics::DensitySpike& a,
                     const AuditMetrics::DensitySpike& b) {
                      return a.start < b.start;
                  });
    }

    // Jacks: >=3 consecutive same-lane notes, each within 500ms of the prior.
    {
        const float gap         = 0.5f;
        const int   minRepeats  = 3;
        for (size_t i = 0; i + 1 < sorted.size(); ) {
            int lane = sorted[i]->track;
            size_t j = i + 1;
            int repeats = 1;
            while (j < sorted.size() &&
                   sorted[j]->track == lane &&
                   sorted[j]->time - sorted[j-1]->time < gap) {
                ++repeats;
                ++j;
            }
            if (repeats >= minRepeats)
                m.jacks.push_back({sorted[i]->time, lane, repeats});
            i = (j > i + 1) ? j : i + 1;
        }
    }

    // Crossovers: adjacent notes with |dLane| >= 3 within 150ms.
    {
        const float gap = 0.15f;
        for (size_t i = 1; i < sorted.size(); ++i) {
            float dt = sorted[i]->time - sorted[i-1]->time;
            if (dt > gap) continue;
            int jump = std::abs(sorted[i]->track - sorted[i-1]->track);
            if (jump >= 3)
                m.crossovers.push_back({sorted[i]->time, jump});
        }
    }

    // Dead zones: gap > 8s between consecutive onsets.
    {
        const float minGap = 8.f;
        for (size_t i = 1; i < sorted.size(); ++i) {
            float dt = sorted[i]->time - sorted[i-1]->time;
            if (dt > minGap)
                m.deadZones.push_back({sorted[i-1]->time, sorted[i]->time});
        }
    }

    // ── Marker coverage ─────────────────────────────────────────────────────
    // Markers come from the AI beat analyzer and represent musical events the
    // chart "should" have a note near. Two complementary checks:
    //   * orphan markers — events the chart skipped (gap in authoring)
    //   * unaligned notes — notes placed where no marker exists (possible
    //     timing drift or off-beat decoration)
    // Both use a ±matchWindow tolerance to count as "covered" — slightly looser
    // than perfect-judgment so legitimate ±100ms author taste isn't flagged.
    //
    // Marker-only stats (count, avg/peak rate, marker density hotspots,
    // marker dead zones) are populated only when there are NO notes yet —
    // those are meant for the "Analyze Beats just finished, no chart yet"
    // workflow. Once authoring begins, the audit shifts to note-side
    // metrics and (if both exist) coverage-based comparison.
    if (!markers.empty()) {
        m.markerCount = (int)markers.size();
        const float matchWindow = 0.12f; // 120 ms
        const bool  notesPresent = !notes.empty();

        // Sorted copies for two-pointer matching.
        std::vector<float> sortedMarkers = markers;
        std::sort(sortedMarkers.begin(), sortedMarkers.end());

        // ── Marker-only stats (skipped once notes exist) ───────────────────
        // When the user is still in the "Analyze Beats just finished, no
        // chart yet" workflow, the audit speaks about the marker stream:
        // density, peak rate, dead zones. As soon as authoring starts the
        // marker-only stats stop being useful — the chart's own density
        // tells the more meaningful story. Coverage / orphan / unaligned
        // are still computed below since those are note-quality metrics.
        if (!notesPresent) {
            m.avgMarkerRate = (m.songDuration > 0.f)
                ? (float)m.markerCount / m.songDuration : 0.f;
            // Peak markers per 2 s window — same definition as peakNps but
            // on the analyzer's output instead of authored notes.
            {
                const float windowSec = 2.f;
                int best = 0;
                size_t head = 0;
                for (size_t tail = 0; tail < sortedMarkers.size(); ++tail) {
                    while (head < sortedMarkers.size() &&
                           sortedMarkers[head] < sortedMarkers[tail] - windowSec)
                        ++head;
                    int count = (int)(tail - head + 1);
                    if (count > best) best = count;
                }
                m.peakMarkerRate2s = best / windowSec;
            }
            // Marker density hotspots: 4 s / ≥16 events, matching the lower
            // note-side threshold (≈4 events per second sustained).
            {
                const float winSec    = 4.f;
                const int   threshold = 16;
                size_t head = 0;
                float  lastEnd  = -1e9f;
                int    runBest  = 0;
                float  runStart = 0.f;
                float  runEnd   = 0.f;
                for (size_t tail = 0; tail < sortedMarkers.size(); ++tail) {
                    while (head < sortedMarkers.size() &&
                           sortedMarkers[head] < sortedMarkers[tail] - winSec)
                        ++head;
                    int count = (int)(tail - head + 1);
                    if (count < threshold) continue;
                    float ws = sortedMarkers[head];
                    float we = sortedMarkers[tail];
                    if (ws - lastEnd > winSec) {
                        if (runBest > 0)
                            m.markerDensityHotspots.push_back({runStart, runEnd, runBest});
                        runBest  = count;
                        runStart = ws;
                        runEnd   = we;
                    } else {
                        if (count > runBest) runBest = count;
                        runEnd = we;
                    }
                    lastEnd = we;
                }
                if (runBest > 0)
                    m.markerDensityHotspots.push_back({runStart, runEnd, runBest});
            }
        } else {
            // Notes exist — keep the marker-only fields zeroed so any UI /
            // prompt code that gates on "markerCount > 0" naturally hides
            // these sections.
            m.markerCount      = 0;
            m.avgMarkerRate    = 0.f;
            m.peakMarkerRate2s = 0.f;
        }
        // Marker dead zones: gap > 8 s between consecutive markers. Same
        // gating rule as the marker-only density block — only meaningful
        // before any notes are placed.
        if (!notesPresent) {
            const float minGap = 8.f;
            for (size_t i = 1; i < sortedMarkers.size(); ++i) {
                float dt = sortedMarkers[i] - sortedMarkers[i - 1];
                if (dt > minGap)
                    m.markerDeadZones.push_back({sortedMarkers[i-1], sortedMarkers[i]});
            }
        }

        // Off-beat note detection: for each note, find the nearest marker.
        // If the gap exceeds matchWindow, the note is unaligned — the
        // analyzer didn't see a musical event there. This is a pure
        // note-quality observation (only meaningful when both sides exist;
        // when there are no notes the loop is empty by definition).
        for (const auto* np : sorted) {
            auto it = std::lower_bound(sortedMarkers.begin(), sortedMarkers.end(), np->time);
            float dt = std::numeric_limits<float>::infinity();
            if (it != sortedMarkers.end()) dt = std::fabs(*it - np->time);
            if (it != sortedMarkers.begin()) {
                float dtPrev = std::fabs(*(it - 1) - np->time);
                if (dtPrev < dt) dt = dtPrev;
            }
            if (dt > matchWindow) {
                m.unalignedNotes.push_back({np->time, np->track, dt});
            }
        }
    }

    return m;
}

std::string describeMetricsForPrompt(const AuditMetrics& m) {
    std::string s;
    char buf[256];

    std::snprintf(buf, sizeof(buf), "Chart metrics:\n  duration = %.1fs\n",
                  m.songDuration);
    s += buf;

    std::snprintf(buf, sizeof(buf),
        "  notes = %d (tap=%d, hold=%d, flick=%d, other=%d)\n",
        m.noteCount, m.tapCount, m.holdCount, m.flickCount, m.otherCount);
    s += buf;

    std::snprintf(buf, sizeof(buf),
        "  avg_nps = %.2f, peak_nps_2s = %.2f\n",
        m.avgNps, m.peakNps);
    s += buf;

    auto cap = [](size_t n, size_t limit) { return n > limit ? limit : n; };

    std::snprintf(buf, sizeof(buf), "Density hotspots (%zu):\n",
                  m.densityHotspots.size());
    s += buf;
    for (size_t i = 0; i < cap(m.densityHotspots.size(), 12); ++i) {
        const auto& h = m.densityHotspots[i];
        std::snprintf(buf, sizeof(buf),
            "  - %.2f-%.2fs count=%d\n", h.start, h.end, h.count);
        s += buf;
    }
    if (m.densityHotspots.size() > 12) {
        std::snprintf(buf, sizeof(buf), "  ...(%zu more)\n",
                      m.densityHotspots.size() - 12);
        s += buf;
    }

    std::snprintf(buf, sizeof(buf),
        "Jacks (same-lane repeats, %zu):\n", m.jacks.size());
    s += buf;
    for (size_t i = 0; i < cap(m.jacks.size(), 12); ++i) {
        const auto& j = m.jacks[i];
        std::snprintf(buf, sizeof(buf),
            "  - t=%.2fs lane=%d repeats=%d\n", j.time, j.lane, j.repeats);
        s += buf;
    }
    if (m.jacks.size() > 12) {
        std::snprintf(buf, sizeof(buf), "  ...(%zu more)\n",
                      m.jacks.size() - 12);
        s += buf;
    }

    std::snprintf(buf, sizeof(buf),
        "Crossovers (big lane jumps under 150ms, %zu):\n",
        m.crossovers.size());
    s += buf;
    for (size_t i = 0; i < cap(m.crossovers.size(), 12); ++i) {
        const auto& c = m.crossovers[i];
        std::snprintf(buf, sizeof(buf),
            "  - t=%.2fs jump=%d\n", c.time, c.laneJump);
        s += buf;
    }
    if (m.crossovers.size() > 12) {
        std::snprintf(buf, sizeof(buf), "  ...(%zu more)\n",
                      m.crossovers.size() - 12);
        s += buf;
    }

    std::snprintf(buf, sizeof(buf),
        "Dead zones (>8s silence, %zu):\n", m.deadZones.size());
    s += buf;
    for (size_t i = 0; i < cap(m.deadZones.size(), 6); ++i) {
        const auto& d = m.deadZones[i];
        std::snprintf(buf, sizeof(buf),
            "  - %.2f-%.2fs\n", d.start, d.end);
        s += buf;
    }
    if (m.deadZones.size() > 6) {
        std::snprintf(buf, sizeof(buf), "  ...(%zu more)\n",
                      m.deadZones.size() - 6);
        s += buf;
    }

    // Marker-derived section (omitted when no markers were supplied).
    if (m.markerCount > 0) {
        std::snprintf(buf, sizeof(buf),
            "Markers: %d   avg %.2f/s   peak %.2f/s (2s window)\n",
            m.markerCount, m.avgMarkerRate, m.peakMarkerRate2s);
        s += buf;

        std::snprintf(buf, sizeof(buf),
            "Marker density hotspots (%zu):\n",
            m.markerDensityHotspots.size());
        s += buf;
        for (size_t i = 0; i < cap(m.markerDensityHotspots.size(), 12); ++i) {
            const auto& h = m.markerDensityHotspots[i];
            std::snprintf(buf, sizeof(buf),
                "  - %.2f-%.2fs count=%d\n", h.start, h.end, h.count);
            s += buf;
        }
        if (m.markerDensityHotspots.size() > 12) {
            std::snprintf(buf, sizeof(buf), "  ...(%zu more)\n",
                          m.markerDensityHotspots.size() - 12);
            s += buf;
        }

        std::snprintf(buf, sizeof(buf),
            "Marker dead zones (>8s, %zu):\n", m.markerDeadZones.size());
        s += buf;
        for (size_t i = 0; i < cap(m.markerDeadZones.size(), 6); ++i) {
            const auto& d = m.markerDeadZones[i];
            std::snprintf(buf, sizeof(buf),
                "  - %.2f-%.2fs\n", d.start, d.end);
            s += buf;
        }
        if (m.markerDeadZones.size() > 6) {
            std::snprintf(buf, sizeof(buf), "  ...(%zu more)\n",
                          m.markerDeadZones.size() - 6);
            s += buf;
        }
    }

    // Unaligned-notes section — shown whenever there are notes that drifted
    // off any analyzer marker, regardless of whether the marker-only stats
    // block above ran. This is a *note*-quality observation so it stays
    // visible once authoring has begun.
    if (!m.unalignedNotes.empty()) {
        std::snprintf(buf, sizeof(buf),
            "Unaligned notes (no marker within 120ms, %zu):\n",
            m.unalignedNotes.size());
        s += buf;
        for (size_t i = 0; i < cap(m.unalignedNotes.size(), 12); ++i) {
            const auto& u = m.unalignedNotes[i];
            std::snprintf(buf, sizeof(buf),
                "  - t=%.2fs lane=%d nearest_marker_dt=%.0fms\n",
                u.time, u.lane, u.nearestMarkerDt * 1000.f);
            s += buf;
        }
        if (m.unalignedNotes.size() > 12) {
            std::snprintf(buf, sizeof(buf), "  ...(%zu more)\n",
                          m.unalignedNotes.size() - 12);
            s += buf;
        }
    }
    return s;
}

AuditReport parseAuditReport(const std::string& assistantMsg) {
    AuditReport out;
    std::string trimmed = extractJsonEnvelope(assistantMsg);
    json doc;
    try {
        doc = json::parse(trimmed);
    } catch (const std::exception& e) {
        out.errorMessage = std::string("Couldn't parse audit JSON: ") + e.what();
        return out;
    }
    if (doc.contains("summary") && doc["summary"].is_string())
        out.summary = doc["summary"].get<std::string>();
    if (doc.contains("issues") && doc["issues"].is_array()) {
        for (const auto& it : doc["issues"]) {
            if (!it.is_object()) continue;
            AuditIssue issue;
            issue.severity  = parseSeverity(it.value("severity", "low"));
            issue.timeStart = (float)it.value("time", 0.0);
            issue.timeEnd   = (float)it.value("end_time", (double)issue.timeStart);
            issue.category  = it.value("category", "other");
            issue.message   = it.value("message",  "");
            if (issue.message.empty()) continue;
            out.issues.push_back(std::move(issue));
        }
    }
    out.success = true;
    return out;
}
