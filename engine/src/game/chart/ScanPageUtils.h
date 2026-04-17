#pragma once

#include "ChartTypes.h"

#include <algorithm>
#include <vector>

// ── Scan-line page-table utilities ──────────────────────────────────────────
//
// A "page" = one sweep of the scan line (top→bottom OR bottom→top).
// Default page duration = 240/BPM seconds (one bar @ 4/4).
// Each page has an optional speed multiplier override (stored sparsely in
// ChartData::scanPageOverrides); page duration = (240/BPM) / speed.
// Direction alternates: page 0 sweeps bottom→top (goingUp=true), page 1
// sweeps top→bottom, and so on.
//
// These helpers are shared by ChartLoader (for on-load expansion of page
// overrides into runtime ScanSpeedEvents) and SongEditor (for the paginated
// scene view).

struct ScanPageInfo {
    int    index       = 0;
    double startTime   = 0.0;
    double duration    = 0.0;
    float  speed       = 1.0f;
    float  bpm         = 120.0f;
    bool   goingUp     = true;   // true = bottom→top at startTime
    bool   partialTail = false;  // shortened by mid-page BPM change
};

// Look up the speed override for `pageIndex` (linear scan — override vectors
// are small). Returns 1.0 if no override.
inline float scanPageSpeedFor(const std::vector<ScanPageOverride>& overrides,
                              int pageIndex)
{
    for (const auto& o : overrides)
        if (o.pageIndex == pageIndex) return o.speed > 0.f ? o.speed : 1.0f;
    return 1.0f;
}

// Build a page table covering [0, songEndTime]. Respects BPM changes by
// ending the current page at each timing-point boundary (partialTail=true).
// If `timingPoints` is empty, uses `fallbackBpm` for the whole song.
inline std::vector<ScanPageInfo> buildScanPageTable(
    const std::vector<TimingPoint>&      timingPoints,
    const std::vector<ScanPageOverride>& overrides,
    double                               songEndTime,
    float                                fallbackBpm = 120.0f)
{
    std::vector<ScanPageInfo> pages;
    if (songEndTime <= 0.0) return pages;

    // Flatten timing into [(segStart, bpm), ...] segments.
    struct Seg { double start; float bpm; };
    std::vector<Seg> segs;
    if (timingPoints.empty()) {
        segs.push_back({0.0, fallbackBpm > 0.f ? fallbackBpm : 120.0f});
    } else {
        // Sort timing points by time (input may not be sorted).
        std::vector<TimingPoint> sorted(timingPoints.begin(), timingPoints.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const TimingPoint& a, const TimingPoint& b) {
                      return a.time < b.time;
                  });
        for (const auto& tp : sorted) {
            float bpm = tp.bpm > 0.f ? tp.bpm : fallbackBpm;
            segs.push_back({tp.time, bpm > 0.f ? bpm : 120.0f});
        }
        if (segs.front().start > 0.0) {
            // Chart starts before the first timing point: use first BPM
            // back-filled to t=0.
            segs.insert(segs.begin(), {0.0, segs.front().bpm});
        }
    }

    double cursor = 0.0;
    int    pageIdx = 0;
    for (size_t si = 0; si < segs.size(); ++si) {
        double segEnd = (si + 1 < segs.size()) ? segs[si + 1].start : songEndTime;
        float  bpm    = segs[si].bpm;
        double basePeriod = (bpm > 0.f) ? (240.0 / bpm) : 2.0;

        while (cursor < segEnd) {
            float  speed  = scanPageSpeedFor(overrides, pageIdx);
            double pageDt = basePeriod / std::max(0.01f, speed);
            double pageEnd = cursor + pageDt;

            ScanPageInfo p{};
            p.index     = pageIdx;
            p.startTime = cursor;
            p.bpm       = bpm;
            p.speed     = speed;
            p.goingUp   = ((pageIdx % 2) == 0);

            if (pageEnd > segEnd + 1e-6) {
                // Mid-page BPM change: truncate at segment boundary.
                double truncated = segEnd - cursor;
                if (truncated < 0.001) {
                    // Negligible tail; skip and advance cursor.
                    cursor = segEnd;
                    break;
                }
                p.duration    = truncated;
                p.partialTail = true;
                pages.push_back(p);
                cursor = segEnd;
                ++pageIdx;
                break;
            }

            p.duration = pageDt;
            pages.push_back(p);
            cursor = pageEnd;
            ++pageIdx;
        }
    }
    return pages;
}

// Expand sparse per-page overrides into ScanSpeedEvents. Emits one zero-
// duration step event at each overridden page's startTime, plus a return-
// to-1.0 event at the following page's startTime if the next page has no
// override. CytusRenderer's buildPhaseTable handles zero-duration events as
// instantaneous step changes (sampleSpeed returns targetSpeed when duration
// <= 1e-6).
inline std::vector<ScanSpeedEvent> expandScanPagesToSpeedEvents(
    const std::vector<ScanPageInfo>&     pageTable,
    const std::vector<ScanPageOverride>& overrides)
{
    std::vector<ScanSpeedEvent> events;
    if (overrides.empty() || pageTable.empty()) return events;

    for (const auto& p : pageTable) {
        bool hasOverride = false;
        for (const auto& o : overrides)
            if (o.pageIndex == p.index) { hasOverride = true; break; }

        // Emit one event per page boundary where the speed differs from
        // the previous page's speed (avoids redundant events).
        float prevSpeed = (p.index == 0) ? 1.0f : [&]() -> float {
            for (const auto& q : pageTable)
                if (q.index == p.index - 1) return q.speed;
            return 1.0f;
        }();
        if (p.index == 0 && !hasOverride) continue;  // default 1.0 at t=0
        if (std::abs(p.speed - prevSpeed) < 1e-4) continue;

        ScanSpeedEvent ev{};
        ev.startTime   = p.startTime;
        ev.duration    = 0.0;
        ev.targetSpeed = p.speed;
        ev.easing      = DiskEasing::Linear;
        events.push_back(ev);
    }
    return events;
}
