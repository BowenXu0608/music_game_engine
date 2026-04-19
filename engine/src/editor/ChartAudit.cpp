#include "ChartAudit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
                                  float songDuration) {
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

    // Density hotspots: 4s windows with >= 24 notes (~6 nps sustained),
    // merged into contiguous runs to avoid flooding the report with
    // overlapping windows.
    {
        const float winSec    = 4.f;
        const int   threshold = 24;
        size_t head = 0;
        float  lastEnd   = -1e9f;
        int    runBest   = 0;
        float  runStart  = 0.f;
        float  runEnd    = 0.f;
        for (size_t tail = 0; tail < sorted.size(); ++tail) {
            while (head < sorted.size() &&
                   sorted[head]->time < sorted[tail]->time - winSec)
                ++head;
            int count = (int)(tail - head + 1);
            if (count < threshold) continue;
            float ws = sorted[head]->time;
            float we = sorted[tail]->time;
            if (ws - lastEnd > winSec) {
                if (runBest > 0)
                    m.densityHotspots.push_back({runStart, runEnd, runBest});
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
            m.densityHotspots.push_back({runStart, runEnd, runBest});
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
