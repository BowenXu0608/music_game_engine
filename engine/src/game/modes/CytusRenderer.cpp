#include "CytusRenderer.h"
#include "renderer/Renderer.h"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>

static constexpr float NOTE_RADIUS    = 30.f;
static constexpr float NOTE_SIZE      = NOTE_RADIUS * 2.f;
static constexpr float SCAN_THICKNESS = 4.f;

// Bit-stable scan-line schedule. Mirrors the editor's helpers in
// SongEditor.cpp so authored positions and runtime sweep line agree.

float CytusRenderer::scanLinePeriod() const {
    float bpm = m_bpm > 0.f ? m_bpm : 120.f;
    return 240.0f / bpm;
}

float CytusRenderer::scanLineFrac(double t) const {
    const double T = (double)scanLinePeriod();
    if (T <= 1e-6) return 1.f;
    const double tt    = t < 0.0 ? 0.0 : t;
    const double phase = std::fmod(tt, 2.0 * T);
    if (phase < T) return (float)(1.0 - phase / T);
    return (float)((phase - T) / T);
}

bool CytusRenderer::scanLineGoingUp(double t) const {
    const double T = (double)scanLinePeriod();
    if (T <= 1e-6) return true;
    const double tt    = t < 0.0 ? 0.0 : t;
    const double phase = std::fmod(tt, 2.0 * T);
    return phase < T;
}

void CytusRenderer::onInit(Renderer& renderer, const ChartData& chart,
                           const GameModeConfig* /*config*/) {
    // Dominant BPM from chart timing (editor always writes at least one
    // point). Fallback 120.
    m_bpm = chart.timingPoints.empty() ? 120.f
                                       : chart.timingPoints.front().bpm;
    if (m_bpm <= 0.f) m_bpm = 120.f;

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
            sn.sx      = hold->scanX;
            sn.sy      = hold->scanY;
            sn.endY    = hold->scanEndY >= 0.f ? hold->scanEndY : hold->scanY;
            sn.lane    = (int)hold->laneX;
            sn.isHold  = true;
            sn.endTime = n.time + hold->duration;
        } else if (auto* flick = std::get_if<FlickData>(&n.data)) {
            sn.sx      = flick->scanX;
            sn.sy      = flick->scanY;
            sn.lane    = (int)flick->laneX;
            sn.isFlick = true;
        } else {
            continue;
        }

        sn.goingUpAtTime = scanLineGoingUp(sn.time);
        m_notes.push_back(std::move(sn));
    }

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

    // Scan line (glow + core)
    renderer.lines().drawLine({0.f, scanY}, {w, scanY},
                               24.f, {1.f, 1.f, 1.f, 0.07f});
    renderer.lines().drawLine({0.f, scanY}, {w, scanY},
                               SCAN_THICKNESS, {1.f, 1.f, 1.f, 0.9f});

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
            drawQuadAt(c, {outerSz, outerSz}, {1.f, 1.f, 1.f, alpha * 0.85f});
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
        if (notePage != curPage) continue;  // different sweep

        const double pageStart = (double)notePage * T_sweep;
        const double pageEnd   = pageStart + T_sweep;
        const double tailTime  = (note.isHold || note.isSlide)
                                 ? std::max(note.endTime, note.time) : note.time;

        float scale, alpha;
        if (m_songTime <= note.time) {
            double denom = std::max(0.0001, note.time - pageStart);
            double u = std::clamp((m_songTime - pageStart) / denom, 0.0, 1.0);
            scale = (float)(0.30 + 0.70 * u);
            alpha = (float)(0.25 + 0.75 * u);
        } else if (m_songTime <= tailTime) {
            scale = 1.f;
            alpha = 1.f;
        } else {
            double tailDt = m_songTime - tailTime;
            if (tailDt > scanTailPad || m_songTime > pageEnd) continue;
            scale = 1.f;
            alpha = (float)std::clamp(1.0 - tailDt / scanTailPad, 0.0, 1.0);
        }
        float sz = NOTE_SIZE * std::max(scale, 0.35f);

        const glm::vec2 head(scanToScreenX(note.sx), scanToScreenY(note.sy));

        if (note.isHold) {
            const glm::vec2 end(scanToScreenX(note.sx), scanToScreenY(note.endY));
            // Body = a thin rectangle between head and tail
            const float bodyH = std::abs(end.y - head.y);
            const glm::vec2 mid(head.x, (head.y + end.y) * 0.5f);
            drawQuadAt(mid, {NOTE_RADIUS * 0.5f, bodyH},
                       {0.3f, 0.7f, 1.f, alpha * 0.45f});
            // Head
            drawQuadAt(head, {sz, sz}, {0.3f, 0.7f, 1.f, alpha});
            // Tail cap
            drawQuadAt(end, {NOTE_RADIUS, NOTE_RADIUS},
                       {0.3f, 0.7f, 1.f, alpha * 0.8f});
            continue;
        }

        if (note.isSlide) {
            // Catmull-Rom tessellated body — matches the editor's smooth
            // preview so runtime and authoring look identical.
            if (note.path.size() >= 2) {
                constexpr int SUBDIV = 12;
                auto cr = [](float p0, float p1, float p2, float p3, float t) {
                    float t2 = t * t, t3 = t2 * t;
                    return 0.5f * ((2.f * p1) +
                                   (-p0 + p2) * t +
                                   (2.f*p0 - 5.f*p1 + 4.f*p2 - p3) * t2 +
                                   (-p0 + 3.f*p1 - 3.f*p2 + p3) * t3);
                };
                glm::vec2 prev(scanToScreenX(note.path.front().first),
                               scanToScreenY(note.path.front().second));
                for (size_t i = 0; i + 1 < note.path.size(); ++i) {
                    const auto& p1 = note.path[i];
                    const auto& p2 = note.path[i + 1];
                    const auto& p0 = (i == 0) ? p1 : note.path[i - 1];
                    const auto& p3 = (i + 2 < note.path.size()) ? note.path[i + 2] : p2;
                    for (int s = 1; s <= SUBDIV; ++s) {
                        float t = (float)s / (float)SUBDIV;
                        float nx = cr(p0.first,  p1.first,  p2.first,  p3.first,  t);
                        float ny = cr(p0.second, p1.second, p2.second, p3.second, t);
                        glm::vec2 cur(scanToScreenX(nx), scanToScreenY(ny));
                        renderer.lines().drawLine(prev, cur, NOTE_RADIUS * 0.4f,
                                                  {0.85f, 0.5f, 1.f, alpha * 0.55f});
                        prev = cur;
                    }
                }
            }
            // Head + sample markers
            drawQuadAt(head, {sz, sz}, {0.85f, 0.5f, 1.f, alpha});
            if (note.path.size() >= 2 && note.slideEndTime > note.time) {
                const float total = (float)(note.slideEndTime - note.time);
                for (float sp : note.samplePoints) {
                    float u = std::clamp(sp / std::max(0.0001f, total), 0.f, 1.f);
                    size_t idx = (size_t)(u * (note.path.size() - 1));
                    if (idx >= note.path.size()) idx = note.path.size() - 1;
                    glm::vec2 pp(scanToScreenX(note.path[idx].first),
                                 scanToScreenY(note.path[idx].second));
                    drawQuadAt(pp, {NOTE_RADIUS * 0.6f, NOTE_RADIUS * 0.6f},
                               {1.f, 1.f, 1.f, alpha});
                }
            }
            continue;
        }

        if (note.isFlick) {
            // Draw an arrow-like quad; direction = sweep direction at note.time
            drawQuadAt(head, {sz * 0.9f, sz * 1.2f},
                       {1.f, 0.75f, 0.35f, alpha});
            drawQuadAt(head, {sz * 0.4f, sz * 0.4f},
                       {1.f, 1.f, 1.f, alpha});
            continue;
        }

        // Plain Tap: outer dark ring + inner fill
        drawQuadAt(head, {sz + 8.f, sz + 8.f}, {0.f, 0.f, 0.f, alpha * 0.6f});
        drawQuadAt(head, {sz, sz}, {1.f, 1.f, 1.f, alpha});
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
}

void CytusRenderer::onShutdown(Renderer& /*renderer*/) {
    m_notes.clear();
}
