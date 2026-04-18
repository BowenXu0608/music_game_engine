#include "CytusRenderer.h"
#include "renderer/Renderer.h"
#include "renderer/ParticleSystem.h"
#include "renderer/MaterialAssetLibrary.h"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>

static constexpr float NOTE_RADIUS    = 30.f;
static constexpr float NOTE_SIZE      = NOTE_RADIUS * 2.f;
static constexpr float SCAN_THICKNESS = 4.f;

namespace {
// Slot ids mirror MaterialSlots.cpp::kCytusSlots.
enum CytusSlot : uint16_t {
    SlotTapNote      = 0,
    SlotHoldBody     = 1,
    SlotHoldHead     = 2,
    SlotHoldTailCap  = 3,
    SlotFlickNote    = 4,
    SlotSlideHead    = 5,
    SlotSlideNode    = 6,
    SlotSlidePath    = 7,
    SlotScanLineCore = 8,
    SlotScanLineGlow = 9,
    SlotHitRing      = 10,
};
} // namespace

glm::vec4 CytusRenderer::slotTint(uint16_t slot, glm::vec4 fallbackRGBA) const {
    auto it = m_chartMaterials.find(slot);
    if (it == m_chartMaterials.end()) return fallbackRGBA;
    return it->second.tint;
}

// ── Scan-line schedule ──────────────────────────────────────────────────────
// Base period = 240/BPM (1 bar @ 4/4). With speed events, scanLineFrac()
// uses a precomputed phase table so the line can speed up / slow down.

float CytusRenderer::scanLinePeriod() const {
    float bpm = m_bpm > 0.f ? m_bpm : 120.f;
    return 240.0f / bpm;
}

float CytusRenderer::applyDiskEasing(float t, DiskEasing e) {
    constexpr float kPi = 3.14159265358979f;
    switch (e) {
        case DiskEasing::SineInOut:
            return -(std::cos(kPi * t) - 1.f) * 0.5f;
        case DiskEasing::QuadInOut:
            return t < 0.5f ? 2.f * t * t
                            : 1.f - (-2.f * t + 2.f) * (-2.f * t + 2.f) * 0.5f;
        case DiskEasing::CubicInOut:
            return t < 0.5f ? 4.f * t * t * t
                            : 1.f - (-2.f * t + 2.f) * (-2.f * t + 2.f) * (-2.f * t + 2.f) * 0.5f;
        case DiskEasing::Linear:
        default:
            return t;
    }
}

void CytusRenderer::buildPhaseTable() {
    m_phaseTable.clear();
    if (m_speedEvents.empty()) return;

    // Collect boundary times from speed events
    std::vector<double> boundaries;
    boundaries.push_back(0.0);
    for (auto& ev : m_speedEvents) {
        boundaries.push_back(ev.startTime);
        boundaries.push_back(ev.startTime + ev.duration);
    }
    // Extend past the last note so phase is defined for the whole song
    double maxT = 0.0;
    for (auto& n : m_notes)
        maxT = std::max(maxT, std::max(n.time, n.endTime) + 2.0);
    boundaries.push_back(maxT);
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
        [](double a, double b) { return std::abs(a - b) < 1e-9; }), boundaries.end());

    // Sample the instantaneous speed at time t using the segment-based
    // interpolation (same pattern as disk animation sampling).
    auto sampleSpeed = [&](double t) -> double {
        if (m_speedEvents.empty() || t < m_speedEvents.front().startTime) return 1.0;
        // Find the last event with startTime <= t
        size_t idx = 0;
        for (size_t i = 1; i < m_speedEvents.size(); ++i)
            if (m_speedEvents[i].startTime <= t) idx = i;
        const auto& cur = m_speedEvents[idx];
        double prev = (idx == 0) ? 1.0 : (double)m_speedEvents[idx - 1].targetSpeed;
        double segEnd = cur.startTime + cur.duration;
        if (cur.duration <= 1e-6 || t >= segEnd) return (double)cur.targetSpeed;
        float u = static_cast<float>((t - cur.startTime) / cur.duration);
        float e = applyDiskEasing(std::clamp(u, 0.f, 1.f), cur.easing);
        return prev + e * ((double)cur.targetSpeed - prev);
    };

    // Build table: numerically integrate speed to get accumulated phase.
    // Phase is measured in "sweep units" (1.0 = one full sweep = basePeriod).
    double accPhase = 0.0;
    m_phaseTable.push_back({0.0, 0.0, sampleSpeed(0.0)});

    for (size_t bi = 1; bi < boundaries.size(); ++bi) {
        double t0 = boundaries[bi - 1];
        double t1 = boundaries[bi];
        if (t1 <= t0) continue;

        // Simpson's rule with 16 subdivisions for accurate easing integration
        constexpr int N = 16;
        double dt = (t1 - t0) / N;
        double integral = 0.0;
        for (int k = 0; k <= N; ++k) {
            double tk = t0 + k * dt;
            double w = (k == 0 || k == N) ? 1.0 : (k % 2 == 1) ? 4.0 : 2.0;
            integral += w * sampleSpeed(tk);
        }
        integral *= dt / 3.0;
        accPhase += integral / m_basePeriod;

        m_phaseTable.push_back({t1, accPhase, sampleSpeed(t1)});
    }
}

double CytusRenderer::interpolatePhase(double t) const {
    if (m_phaseTable.empty()) return (t < 0 ? 0 : t) / m_basePeriod;
    if (t <= m_phaseTable.front().time) return m_phaseTable.front().phase;
    if (t >= m_phaseTable.back().time) {
        // Extrapolate beyond table at the final speed
        const auto& last = m_phaseTable.back();
        double dt = t - last.time;
        return last.phase + last.speed * dt / m_basePeriod;
    }
    // Binary search
    auto it = std::upper_bound(m_phaseTable.begin(), m_phaseTable.end(), t,
        [](double tt, const PhaseEntry& e) { return tt < e.time; });
    const auto& cur = *std::prev(it);
    const auto& nxt = *it;
    double segDt = nxt.time - cur.time;
    if (segDt < 1e-9) return cur.phase;
    double frac = (t - cur.time) / segDt;
    return cur.phase + frac * (nxt.phase - cur.phase);
}

float CytusRenderer::scanLineFrac(double t) const {
    double phase;
    if (m_phaseTable.empty()) {
        // Constant-speed fallback
        const double T = m_basePeriod;
        if (T <= 1e-6) return 1.f;
        const double tt = t < 0.0 ? 0.0 : t;
        const double p = std::fmod(tt, 2.0 * T);
        if (p < T) return static_cast<float>(1.0 - p / T);
        return static_cast<float>((p - T) / T);
    }
    phase = interpolatePhase(t < 0.0 ? 0.0 : t);
    // Triangle wave: phase in [0,2) maps to frac bouncing 1→0→1
    double cyclePhase = std::fmod(phase, 2.0);
    if (cyclePhase < 0.0) cyclePhase += 2.0;
    if (cyclePhase < 1.0) return static_cast<float>(1.0 - cyclePhase);
    return static_cast<float>(cyclePhase - 1.0);
}

bool CytusRenderer::scanLineGoingUp(double t) const {
    double phase;
    if (m_phaseTable.empty()) {
        const double T = m_basePeriod;
        if (T <= 1e-6) return true;
        const double tt = t < 0.0 ? 0.0 : t;
        phase = std::fmod(tt, 2.0 * T) / T;
    } else {
        phase = interpolatePhase(t < 0.0 ? 0.0 : t);
    }
    double cyclePhase = std::fmod(phase, 2.0);
    if (cyclePhase < 0.0) cyclePhase += 2.0;
    return cyclePhase < 1.0;
}

void CytusRenderer::onInit(Renderer& renderer, const ChartData& chart,
                           const GameModeConfig* /*config*/) {
    m_renderer = &renderer;

    // Import per-slot material overrides — asset or legacy inline, picked by
    // resolveMaterial() based on which form the entry populates.
    m_chartMaterials.clear();
    for (const auto& md : chart.materials) {
        m_chartMaterials[md.slot] = resolveMaterial(md, m_materialLibrary);
    }

    // Dominant BPM from chart timing (editor always writes at least one
    // point). Fallback 120.
    m_bpm = chart.timingPoints.empty() ? 120.f
                                       : chart.timingPoints.front().bpm;
    if (m_bpm <= 0.f) m_bpm = 120.f;
    m_basePeriod = 240.0 / m_bpm;

    // Load and sort speed events, then build phase table
    m_speedEvents = chart.scanSpeedEvents;
    std::sort(m_speedEvents.begin(), m_speedEvents.end(),
              [](const ScanSpeedEvent& a, const ScanSpeedEvent& b) {
                  return a.startTime < b.startTime; });

    for (const auto& n : chart.notes) {
        ScanNote sn{};
        sn.id   = n.id;
        sn.time = n.time;

        if (auto* tap = std::get_if<TapData>(&n.data)) {
            sn.sx   = tap->scanX;
            sn.sy   = tap->scanY;
            sn.lane = (int)tap->laneX;
            if (n.type == NoteType::Slide) {
                sn.isSlide      = true;
                sn.path         = tap->scanPath;
                sn.slideEndTime = n.time + tap->duration;
                sn.endTime      = sn.slideEndTime;
                sn.samplePoints = tap->samplePoints;
            } else {
                sn.isTap = true;
            }
        } else if (auto* hold = std::get_if<HoldData>(&n.data)) {
            sn.sx         = hold->scanX;
            sn.sy         = hold->scanY;
            sn.endY       = hold->scanEndY >= 0.f ? hold->scanEndY : hold->scanY;
            sn.lane       = (int)hold->laneX;
            sn.isHold     = true;
            sn.endTime    = n.time + hold->duration;
            sn.holdSweeps = hold->scanHoldSweeps;
        } else if (auto* flick = std::get_if<FlickData>(&n.data)) {
            sn.sx      = flick->scanX;
            sn.sy      = flick->scanY;
            sn.lane    = (int)flick->laneX;
            sn.isFlick = true;
        } else {
            continue;
        }

        m_notes.push_back(std::move(sn));
    }

    // Build phase table after notes are loaded (buildPhaseTable reads m_notes
    // to determine song length) but before goingUpAtTime is assigned (so the
    // phase-aware scanLineGoingUp is available).
    buildPhaseTable();
    for (auto& sn : m_notes)
        sn.goingUpAtTime = scanLineGoingUp(sn.time);

    onResize(renderer.width(), renderer.height());
}

void CytusRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;
    m_camera = Camera::makeOrtho(0.f, (float)w, (float)h, 0.f);
}

void CytusRenderer::onUpdate(float dt, double songTime) {
    m_songTime = songTime;
    for (auto& note : m_notes)
        if (note.isHit) note.hitTimer += dt;
}

void CytusRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    const float w = (float)m_width;
    const float h = (float)m_height;

    const float scanY = scanLineFrac(m_songTime) * h;

    // Scan line (glow + core). Both are LineBatch consumers — tint-only.
    glm::vec4 scanGlowTint = slotTint(SlotScanLineGlow, {1.f, 1.f, 1.f, 0.07f});
    glm::vec4 scanCoreTint = slotTint(SlotScanLineCore, {1.f, 1.f, 1.f, 0.9f});
    renderer.lines().drawLine({0.f, scanY}, {w, scanY}, 24.f, scanGlowTint);
    renderer.lines().drawLine({0.f, scanY}, {w, scanY}, SCAN_THICKNESS, scanCoreTint);

    // Cache per-frame resolved tints for the remaining slots. Alpha is the
    // slot's own alpha; per-note `alpha` is multiplied in at each call site.
    const glm::vec4 tapTint       = slotTint(SlotTapNote,     {1.f, 1.f, 1.f, 1.f});
    const glm::vec4 holdBodyTint  = slotTint(SlotHoldBody,    {0.3f, 0.7f, 1.f, 0.45f});
    const glm::vec4 holdHeadTint  = slotTint(SlotHoldHead,    {0.3f, 0.7f, 1.f, 1.f});
    const glm::vec4 holdTailTint  = slotTint(SlotHoldTailCap, {0.3f, 0.7f, 1.f, 0.8f});
    const glm::vec4 flickTint     = slotTint(SlotFlickNote,   {1.f, 0.75f, 0.35f, 1.f});
    const glm::vec4 slideHeadTint = slotTint(SlotSlideHead,   {0.85f, 0.5f, 1.f, 1.f});
    const glm::vec4 slideNodeTint = slotTint(SlotSlideNode,   {1.f, 1.f, 1.f, 1.f});
    const glm::vec4 slidePathTint = slotTint(SlotSlidePath,   {0.85f, 0.5f, 1.f, 0.55f});
    const glm::vec4 hitRingTint   = slotTint(SlotHitRing,     {1.f, 1.f, 1.f, 0.85f});

    auto withAlpha = [](const glm::vec4& base, float alphaMul) {
        return glm::vec4{base.r, base.g, base.b, base.a * alphaMul};
    };

    const auto whiteTex = std::tuple{renderer.whiteView(), renderer.whiteSampler()};
    auto drawQuadAt = [&](glm::vec2 c, glm::vec2 sz, glm::vec4 col) {
        renderer.quads().drawQuad(c, sz, 0.f, col, {0.f, 0.f, 1.f, 1.f},
                                  std::get<0>(whiteTex), std::get<1>(whiteTex),
                                  renderer.context(), renderer.descriptors());
    };

    // Cytus-style page visibility. Each sweep is a page; notes are shown
    // only during their own page and scale/fade in as the scan line
    // approaches them. No cross-turn overlap.
    const double T_sweep = (double)scanLinePeriod();
    const int    curPage = T_sweep > 1e-4 ? (int)std::floor(m_songTime / T_sweep) : 0;
    constexpr double scanTailPad = 0.3;

    for (auto& note : m_notes) {
        // ── Hit effect: expanding ring ─────────────────────────────────
        if (note.isHit) {
            static constexpr float HIT_EFFECT_DUR = 0.35f;
            if (note.hitTimer >= HIT_EFFECT_DUR) continue;
            float t2       = note.hitTimer / HIT_EFFECT_DUR;
            float expand   = 1.f + t2 * 2.5f;
            float alpha    = 1.f - t2;
            float outerSz  = NOTE_SIZE * expand + 10.f;
            float innerSz  = NOTE_SIZE * expand - 10.f;
            glm::vec2 c(scanToScreenX(note.sx), scanToScreenY(note.sy));
            drawQuadAt(c, {outerSz, outerSz}, withAlpha(hitRingTint, alpha));
            if (innerSz > 0.f)
                drawQuadAt(c, {innerSz, innerSz}, {0.f, 0.f, 0.f, alpha});
            continue;
        }

        // Drop legacy lane-authored notes that have no scan position —
        // otherwise they all stack at (0,0). A chart author would never
        // place a note at exactly the top-left corner intentionally.
        if (note.sx < 0.001f && note.sy < 0.001f &&
            note.endY <= 0.f && note.path.empty())
            continue;

        const int notePage = T_sweep > 1e-4 ? (int)std::floor(note.time / T_sweep) : 0;
        // Multi-sweep holds span multiple pages — check if current page
        // falls within the note's page range.
        if (note.isHold && note.holdSweeps > 0) {
            const int endPage = T_sweep > 1e-4 ? (int)std::floor(note.endTime / T_sweep) : 0;
            if (curPage < notePage || curPage > endPage) continue;
        } else {
            if (notePage != curPage) continue;
        }

        // For multi-sweep holds, use the END page boundaries so the hold
        // stays visible throughout its duration, not just the head page.
        const double visPageStart = (double)notePage * T_sweep;
        const int    endPageIdx   = (note.isHold && note.holdSweeps > 0 && T_sweep > 1e-4)
                                    ? (int)std::floor(note.endTime / T_sweep) : notePage;
        const double visPageEnd   = (double)(endPageIdx + 1) * T_sweep;
        const double tailTime     = (note.isHold || note.isSlide)
                                    ? std::max(note.endTime, note.time) : note.time;

        float scale, alpha;
        if (m_songTime <= note.time) {
            double denom = std::max(0.0001, note.time - visPageStart);
            double u = std::clamp((m_songTime - visPageStart) / denom, 0.0, 1.0);
            scale = (float)(0.30 + 0.70 * u);
            alpha = (float)(0.25 + 0.75 * u);
        } else if (m_songTime <= tailTime) {
            scale = 1.f;
            alpha = 1.f;
        } else {
            double tailDt = m_songTime - tailTime;
            if (tailDt > scanTailPad || m_songTime > visPageEnd) continue;
            scale = 1.f;
            alpha = (float)std::clamp(1.0 - tailDt / scanTailPad, 0.0, 1.0);
        }
        float sz = NOTE_SIZE * std::max(scale, 0.35f);

        const glm::vec2 head(scanToScreenX(note.sx), scanToScreenY(note.sy));

        if (note.isHold) {
            const float holdW = NOTE_RADIUS * 0.5f;
            if (note.holdSweeps == 0) {
                // Single-sweep hold: simple rectangle
                const glm::vec2 end(scanToScreenX(note.sx), scanToScreenY(note.endY));
                const float bodyH = std::abs(end.y - head.y);
                const glm::vec2 mid(head.x, (head.y + end.y) * 0.5f);
                drawQuadAt(mid, {holdW, bodyH}, withAlpha(holdBodyTint, alpha));
                drawQuadAt(end, {NOTE_RADIUS, NOTE_RADIUS}, withAlpha(holdTailTint, alpha));
            } else {
                // Multi-sweep hold: draw body segments through each sweep
                bool sweepUp = note.goingUpAtTime;
                float segStartY = head.y;
                for (int s = 0; s <= note.holdSweeps; ++s) {
                    float segEndY;
                    if (s < note.holdSweeps) {
                        // Full sweep to turn boundary
                        segEndY = scanToScreenY(sweepUp ? 0.f : 1.f);
                    } else {
                        // Final sweep to tail
                        segEndY = scanToScreenY(note.endY);
                    }
                    float bodyH = std::abs(segEndY - segStartY);
                    float midY  = (segStartY + segEndY) * 0.5f;
                    drawQuadAt({head.x, midY}, {holdW, bodyH},
                               withAlpha(holdBodyTint, alpha));
                    segStartY = segEndY;
                    sweepUp = !sweepUp;
                }
                // Tail cap
                glm::vec2 end(scanToScreenX(note.sx), scanToScreenY(note.endY));
                drawQuadAt(end, {NOTE_RADIUS, NOTE_RADIUS}, withAlpha(holdTailTint, alpha));
            }
            // Head
            drawQuadAt(head, {sz, sz}, withAlpha(holdHeadTint, alpha));
            continue;
        }

        if (note.isSlide) {
            // Straight-line segments between control points (Cytus-style).
            if (note.path.size() >= 2) {
                glm::vec2 prev(scanToScreenX(note.path[0].first),
                               scanToScreenY(note.path[0].second));
                for (size_t i = 1; i < note.path.size(); ++i) {
                    glm::vec2 cur(scanToScreenX(note.path[i].first),
                                  scanToScreenY(note.path[i].second));
                    renderer.lines().drawLine(prev, cur, NOTE_RADIUS * 0.4f,
                                              withAlpha(slidePathTint, alpha));
                    prev = cur;
                }
            }
            // Head marker
            drawQuadAt(head, {sz, sz}, withAlpha(slideHeadTint, alpha));
            // Node markers at each control point after the head
            for (size_t i = 1; i < note.path.size(); ++i) {
                glm::vec2 pp(scanToScreenX(note.path[i].first),
                             scanToScreenY(note.path[i].second));
                drawQuadAt(pp, {NOTE_RADIUS * 0.6f, NOTE_RADIUS * 0.6f},
                           withAlpha(slideNodeTint, alpha));
            }
            continue;
        }

        if (note.isFlick) {
            // Arrow body uses the Flick tint; inner accent is a derived white.
            drawQuadAt(head, {sz * 0.9f, sz * 1.2f}, withAlpha(flickTint, alpha));
            drawQuadAt(head, {sz * 0.4f, sz * 0.4f}, {1.f, 1.f, 1.f, alpha});
            continue;
        }

        // Plain Tap: outer dark ring + inner fill (ring stays hardcoded).
        drawQuadAt(head, {sz + 8.f, sz + 8.f}, {0.f, 0.f, 0.f, alpha * 0.6f});
        drawQuadAt(head, {sz, sz}, withAlpha(tapTint, alpha));
    }
}

// ── Spatial picker ───────────────────────────────────────────────────────
// Runs over every un-hit note and returns the one whose head sits closest
// to `screenPx`, gated on:
//   1) temporal window: |note.time - songTime| <= 0.18s. This is slightly
//      wider than JudgmentSystem's Bad threshold so the picker can still
//      consume a late tap and let JudgmentSystem classify it as Bad/Miss.
//   2) pixel tolerance: distance to note head <= pixelTol.
// For Slides, tapping anywhere along the recorded path also counts as a
// pick on the head (enables the player to put their finger down partway
// along a slide, which the drag logic will then track).

std::optional<CytusRenderer::PickResult>
CytusRenderer::pickNoteAt(glm::vec2 screenPx, double songTime, float pixelTol) const
{
    const float tolSq = pixelTol * pixelTol;
    float    bestDistSq = tolSq;
    const ScanNote* best = nullptr;

    for (const auto& n : m_notes) {
        if (n.isHit) continue;
        if (std::abs(n.time - songTime) > 0.18) continue;

        // Head-distance check
        float hx = scanToScreenX(n.sx);
        float hy = scanToScreenY(n.sy);
        float dx = screenPx.x - hx;
        float dy = screenPx.y - hy;
        float dsq = dx * dx + dy * dy;

        // For slides, also allow picks anywhere along the path.
        if (n.isSlide && !n.path.empty()) {
            for (const auto& p : n.path) {
                float px = scanToScreenX(p.first);
                float py = scanToScreenY(p.second);
                float pdx = screenPx.x - px;
                float pdy = screenPx.y - py;
                float pdsq = pdx * pdx + pdy * pdy;
                if (pdsq < dsq) dsq = pdsq;
            }
        }

        if (dsq < bestDistSq) {
            bestDistSq = dsq;
            best       = &n;
        }
    }

    if (!best) return std::nullopt;
    PickResult r{};
    r.noteId = best->id;
    r.type   = best->isHold  ? NoteType::Hold
             : best->isFlick ? NoteType::Flick
             : best->isSlide ? NoteType::Slide
             :                 NoteType::Tap;
    return r;
}

void CytusRenderer::markNoteHit(uint32_t noteId) {
    for (auto& n : m_notes) {
        if (n.id == noteId) {
            n.isHit    = true;
            n.hitTimer = 0.f;
            return;
        }
    }
}

void CytusRenderer::showJudgment(int lane, Judgment judgment) {
    if (judgment == Judgment::Miss) return;
    float bestDist = 999.f;
    ScanNote* best = nullptr;
    for (auto& note : m_notes) {
        if (note.isHit) continue;
        if (note.lane != lane) continue;
        float d = std::abs((float)(note.time - m_songTime));
        if (d > 0.15f) continue;
        if (d < bestDist) { bestDist = d; best = &note; }
    }
    if (best) best->isHit = true;

    // Particle burst at the note's on-screen position.
    if (m_renderer && best) {
        glm::vec4 pColor;
        int pCount = 12;
        switch (judgment) {
            case Judgment::Perfect: pColor = {0.2f, 1.f,  0.3f, 1.f}; pCount = 20; break;
            case Judgment::Good:    pColor = {0.3f, 0.6f, 1.f,  1.f}; pCount = 14; break;
            case Judgment::Bad:     pColor = {1.f,  0.6f, 0.2f, 1.f}; pCount = 10; break;
            default: return;
        }
        m_renderer->particles().emitBurst({best->sx, best->sy}, pColor, pCount,
                                          200.f, 8.f, 0.5f);
    }
}

// Linear interpolation along a piecewise-linear path at parameter u ∈ [0,1].
static std::pair<float,float> linearPathEval(
    const std::vector<std::pair<float,float>>& pts, float u) {
    if (pts.empty()) return {0.f, 0.f};
    if (pts.size() == 1 || u <= 0.f) return pts.front();
    if (u >= 1.f) return pts.back();
    float scaled = u * static_cast<float>(pts.size() - 1);
    size_t i = static_cast<size_t>(scaled);
    if (i >= pts.size() - 1) i = pts.size() - 2;
    float t = scaled - static_cast<float>(i);
    return { pts[i].first  + t * (pts[i+1].first  - pts[i].first),
             pts[i].second + t * (pts[i+1].second - pts[i].second) };
}

bool CytusRenderer::slideExpectedPos(uint32_t noteId, double songTime,
                                     glm::vec2& outScreen) const {
    for (auto& note : m_notes) {
        if (note.id != noteId || !note.isSlide) continue;
        if (note.path.size() < 2 || note.slideEndTime <= note.time) return false;
        float total = static_cast<float>(note.slideEndTime - note.time);
        float elapsed = static_cast<float>(songTime - note.time);
        float u = std::clamp(elapsed / std::max(0.0001f, total), 0.f, 1.f);
        auto [px, py] = linearPathEval(note.path, u);
        outScreen = {scanToScreenX(px), scanToScreenY(py)};
        return true;
    }
    return false;
}

std::vector<CytusRenderer::SlideTick> CytusRenderer::consumeSlideTicks(double songTime) {
    std::vector<SlideTick> ticks;
    for (auto& note : m_notes) {
        if (!note.isSlide || note.path.size() < 2) continue;
        if (note.samplePoints.empty()) continue;
        float total = static_cast<float>(note.slideEndTime - note.time);
        while (note.nextSampleIdx < note.samplePoints.size()) {
            float spOff = note.samplePoints[note.nextSampleIdx];
            double absTime = note.time + spOff;
            if (absTime > songTime) break;
            float u = std::clamp(spOff / std::max(0.0001f, total), 0.f, 1.f);
            auto [px, py] = linearPathEval(note.path, u);
            ticks.push_back({note.id, scanToScreenX(px), scanToScreenY(py)});
            note.nextSampleIdx++;
        }
    }
    return ticks;
}

void CytusRenderer::onShutdown(Renderer& /*renderer*/) {
    m_notes.clear();
}
