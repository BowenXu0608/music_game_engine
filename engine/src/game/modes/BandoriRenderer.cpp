#include "BandoriRenderer.h"
#include "renderer/Renderer.h"
#include "ui/ProjectHub.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <vector>

// Project world pos → screen coords (y=0 bottom, y=h top) using the perspective VP.
// With Vulkan-corrected perspective (proj[1][1] *= -1):
//   NDC Y = +1  →  screen bottom (y = 0)
//   NDC Y = -1  →  screen top    (y = h)
glm::vec2 BandoriRenderer::w2s(glm::vec3 pos, const glm::mat4& vp, float sw, float sh) {
    glm::vec4 clip = vp * glm::vec4(pos, 1.f);
    if (clip.w <= 0.f) return {-9999.f, -9999.f};
    float ndcX =  clip.x / clip.w;
    float ndcY =  clip.y / clip.w;
    return {
        (ndcX * 0.5f + 0.5f) * sw,
        (0.5f - ndcY * 0.5f) * sh
    };
}

float BandoriRenderer::pxSize(float worldSz, float clipW, float proj11y, float sh) {
    if (clipW <= 0.f) return 0.f;
    return worldSz * proj11y * sh * 0.5f / clipW;
}

void BandoriRenderer::onInit(Renderer& renderer, const ChartData& chart,
                             const GameModeConfig* config) {
    m_renderer = &renderer;
    m_notes = chart.notes;

    // Apply camera config
    if (config) {
        m_camEye    = {config->cameraEye[0], config->cameraEye[1], config->cameraEye[2]};
        m_camTarget = {config->cameraTarget[0], config->cameraTarget[1], config->cameraTarget[2]};
        m_camFov    = config->cameraFov;
        m_laneCount = config->trackCount;
    }

    // Also check lane count from chart data (in case notes use higher lanes)
    for (auto& n : m_notes) {
        int lane = -1;
        if (auto* tap = std::get_if<TapData>(&n.data))        lane = static_cast<int>(std::lround(tap->laneX));
        else if (auto* hold = std::get_if<HoldData>(&n.data)) lane = static_cast<int>(std::lround(hold->laneX));
        else if (auto* flick = std::get_if<FlickData>(&n.data)) lane = static_cast<int>(std::lround(flick->laneX));
        if (lane >= m_laneCount) m_laneCount = lane + 1;
    }

    m_judgmentDisplays.resize(m_laneCount);

    onResize(renderer.width(), renderer.height());
}

void BandoriRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;

    float aspect = h > 0 ? static_cast<float>(w) / h : 1.f;

    // Camera from config (user-adjustable in editor)
    Camera persp = Camera::makePerspective(m_camFov, aspect, 0.1f, 300.f);
    persp.lookAt(m_camEye, m_camTarget);
    m_perspVP  = persp.viewProjection();
    m_proj11y  = std::abs(persp.projection()[1][1]);

    // Calculate lane spacing so highway width matches ~90% of screen at the hit zone
    // Project two test points at z=0 to find screen-space width per world unit
    glm::vec2 leftTest  = w2s({-1.f, 0.f, HIT_ZONE_Z}, m_perspVP, (float)w, (float)h);
    glm::vec2 rightTest = w2s({ 1.f, 0.f, HIT_ZONE_Z}, m_perspVP, (float)w, (float)h);
    float pxPerWorldUnit = (rightTest.x - leftTest.x) * 0.5f; // px per 1 world unit
    if (pxPerWorldUnit > 0.f) {
        float desiredPx = (float)w * 0.30f; // highway fills 30% of screen width (centered)
        float totalWorldW = desiredPx / pxPerWorldUnit;
        m_laneSpacing = totalWorldW / m_laneCount;
        m_noteWorldW  = m_laneSpacing; // notes fill the full lane width
    }

    m_camera = Camera::makeOrtho(0.f, static_cast<float>(w),
                                  static_cast<float>(h), 0.f);
}

void BandoriRenderer::onUpdate(float dt, double songTime) {
    m_songTime = songTime;

    for (auto& display : m_judgmentDisplays) {
        display.update(dt);
    }
}

void BandoriRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    float sw = static_cast<float>(m_width);
    float sh = static_cast<float>(m_height);

    // Lane dividers converge to vanishing point — one edge line per boundary
    for (int i = 0; i <= m_laneCount; ++i) {
        float wx   = (i - m_laneCount * 0.5f) * m_laneSpacing;
        glm::vec2 nearPt = w2s({wx, 0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        glm::vec2 farPt  = w2s({wx, 0.f, APPROACH_Z}, m_perspVP, sw, sh);
        renderer.lines().drawLine(nearPt, farPt, 1.5f, {1.f, 1.f, 1.f, 0.2f});
    }

    // Hit zone line across all lanes (bright, thick)
    {
        float leftX  = -(m_laneCount * 0.5f) * m_laneSpacing;
        float rightX =  (m_laneCount * 0.5f) * m_laneSpacing;
        glm::vec2 l = w2s({leftX,  0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        glm::vec2 r = w2s({rightX, 0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        renderer.lines().drawLine(l, r, 4.f, {1.f, 0.9f, 0.2f, 1.f});  // bright yellow
        renderer.lines().drawLine(l, r, 8.f, {1.f, 0.9f, 0.2f, 0.3f}); // glow
    }

    // ── Hold bodies (draw before heads so the head marker sits on top) ──────
    // For each Hold note, tessellate a ribbon along the interpolated lane path
    // from start lane → end lane, using the HoldTransition style. Also drops
    // sample-point markers along the ribbon.
    auto laneToWorldX = [&](float lane) {
        return (lane - (m_laneCount - 1) * 0.5f) * m_laneSpacing;
    };
    for (auto& note : m_notes) {
        if (note.type != NoteType::Hold) continue;
        if (m_hitNotes.count(note.id)) continue;
        auto* hold = std::get_if<HoldData>(&note.data);
        if (!hold || hold->duration <= 0.f) continue;

        // If the head time has passed by more than the Bad window and the
        // player never started holding, the head was missed or judged Bad —
        // the entire hold should disappear, not just stop being interactive.
        const bool holdActive = m_activeHoldIds.count(note.id) > 0;
        constexpr double kBadWindow = 0.15;
        if (!holdActive && m_songTime > (double)note.time + kBadWindow) {
            m_hitNotes.insert(note.id);
            continue;
        }

        const float dur      = hold->duration;
        const float baseHalf = m_noteWorldW * 0.5f;

        // Rhomboid half-width — for the multi-waypoint path, bulges during
        // each rhomboid segment's transition window. Falls back to the legacy
        // single-transition spread when waypoints are empty.
        auto halfWAt = [&](float tOff) -> float {
            if (!hold->waypoints.empty()) {
                int seg = holdActiveSegment(*hold, tOff);
                if (seg <= 0) return baseHalf;
                const auto& a = hold->waypoints[seg - 1];
                const auto& b = hold->waypoints[seg];
                if (b.style != HoldTransition::Rhomboid) return baseHalf;
                float tLen = std::max(0.f, b.transitionLen);
                if (tLen <= 0.f) return baseHalf;
                float u = (tOff - (b.tOffset - tLen)) / tLen;
                float tri = 1.f - std::abs(2.f * u - 1.f);
                float spread = std::abs((float)b.lane - (float)a.lane) * m_laneSpacing;
                return baseHalf + tri * spread * 0.5f;
            }
            if (hold->transition != HoldTransition::Rhomboid
                || hold->effectiveEndLane() == hold->laneX)
                return baseHalf;
            float tLen = std::clamp(hold->transitionLen, 0.f, dur);
            if (tLen <= 0.f) return baseHalf;
            float tBegin = holdTransitionBegin(*hold);
            float tEnd   = tBegin + tLen;
            if (tOff <= tBegin || tOff >= tEnd) return baseHalf;
            float u = (tOff - tBegin) / tLen;
            float tri = 1.f - std::abs(2.f * u - 1.f);
            float spread = std::abs(hold->effectiveEndLane() - hold->laneX) * m_laneSpacing;
            return baseHalf + tri * spread * 0.5f;
        };

        // While the hold is actively being held, push RGB above 1.0 so the
        // bloom post-process picks it up as a glow. Otherwise render normal.
        const glm::vec4 holdBodyColor = holdActive
            ? glm::vec4{0.6f, 2.4f, 3.0f, 0.95f}   // bright cyan → blooms
            : glm::vec4{0.2f, 0.8f, 1.f, 0.85f};

        // Tessellate the *visible* portion of the hold uniformly. Previously
        // we sampled tOff in [0, dur] and culled out-of-window segments,
        // which made long holds spawn in chunks at the far plane: each
        // sample only popped in once its wz crossed the cull line, so the
        // ribbon visibly extended itself N times. Instead, derive the tOff
        // window directly from the visible Z range and tessellate inside it
        // so the visible ribbon is always continuous from the very moment
        // any part of the hold enters the highway.
        //
        //   wz = -(absT - songTime) * SCROLL_SPEED
        //   absT = note.time + tOff
        //   ⇒ tOff = (-wz / SCROLL_SPEED) - (note.time - songTime)
        // While actively holding, hide everything past the judgement line
        // (wz > 0). Before the hold is started, allow a small +12 of past
        // overshoot so the head doesn't pop out the moment it crosses the
        // line — gives the player a frame or two to react.
        const float zNear = holdActive ? 0.f : 12.f;
        const float zFar  = APPROACH_Z - 2.f;
        const float dt    = static_cast<float>(note.time - m_songTime);
        const float tOffAtZNear = (-zNear / SCROLL_SPEED) - dt;
        const float tOffAtZFar  = (-zFar  / SCROLL_SPEED) - dt;
        const float tOffLo = std::max(0.f,  std::min(tOffAtZNear, tOffAtZFar));
        const float tOffHi = std::min(dur,  std::max(tOffAtZNear, tOffAtZFar));
        if (tOffHi <= tOffLo + 1e-4f) continue;

        // EVERY sample point sits on a fixed chart-time grid, so as the
        // visible window scrolls, samples only enter/leave at the bounds —
        // no sample's tOff ever drifts. Combined with chart-time-anchored
        // corner samples (transition boundaries + interior subdivisions),
        // this means the on-screen polyline shape for any segment of the
        // hold is identical from frame to frame, eliminating the
        // morph/lagging stutter at corners.
        constexpr float kGridStep      = 0.04f;   // baseline sample period (sec)
        constexpr int   kInteriorSamples = 8;     // extra samples per corner

        std::vector<float> tSamples;
        tSamples.reserve(64);

        // 1. Fixed chart-time grid covering [0, dur].
        for (float t = 0.f; t < dur; t += kGridStep) tSamples.push_back(t);
        tSamples.push_back(dur);

        // 2. Anchor samples at every transition window boundary plus dense
        //    interior subdivisions. These are also chart-time-fixed.
        auto addCornerSamples = [&](float tBeg, float tEnd) {
            if (tEnd <= tBeg + 1e-4f) return;
            tSamples.push_back(tBeg);
            tSamples.push_back(tEnd);
            for (int j = 1; j < kInteriorSamples; ++j)
                tSamples.push_back(tBeg + (tEnd - tBeg) * (float)j / (float)kInteriorSamples);
        };

        if (!hold->waypoints.empty()) {
            for (size_t wi = 1; wi < hold->waypoints.size(); ++wi) {
                const auto& b = hold->waypoints[wi];
                if (b.transitionLen <= 0.f) continue;
                float tEnd = b.tOffset;
                float tBeg = tEnd - b.transitionLen;
                addCornerSamples(tBeg, tEnd);
            }
        } else if (hold->transition != HoldTransition::Straight
                   && hold->transitionLen > 0.f
                   && hold->effectiveEndLane() != hold->laneX) {
            float tBeg = holdTransitionBegin(*hold);
            float tEnd = tBeg + std::clamp(hold->transitionLen, 0.f, dur);
            addCornerSamples(tBeg, tEnd);
        }

        // 3. Add the visible-window endpoints themselves as explicit samples
        //    so the rendered ribbon's first/last vertex sits *exactly* on
        //    the visible boundary instead of at whichever grid sample
        //    happens to be inside it. Without this, the boundary slice
        //    snaps between configurations as the window scrolls past grid
        //    points, which reads as a flicker at the judgement line during
        //    an active hold.
        tSamples.push_back(tOffLo);
        tSamples.push_back(tOffHi);

        // 4. Sort & dedupe so the polyline marches forward in chart time.
        std::sort(tSamples.begin(), tSamples.end());
        tSamples.erase(std::unique(tSamples.begin(), tSamples.end(),
                                   [](float a, float b) { return std::abs(a - b) < 1e-4f; }),
                       tSamples.end());

        bool havePrev = false;
        glm::vec3 prevL{}, prevR{};
        for (float tOff : tSamples) {
            if (tOff < tOffLo - 1e-4f || tOff > tOffHi + 1e-4f) {
                havePrev = false; continue;
            }
            double absT = note.time + tOff;

            float lane = evalHoldLaneAt(*hold, tOff);
            float wx   = laneToWorldX(lane);
            float wz   = -static_cast<float>(absT - m_songTime) * SCROLL_SPEED;

            float hw = halfWAt(tOff);
            glm::vec3 L{wx - hw, 0.f, wz};
            glm::vec3 R{wx + hw, 0.f, wz};
            if (havePrev) {
                glm::vec2 sPL = w2s(prevL, m_perspVP, sw, sh);
                glm::vec2 sPR = w2s(prevR, m_perspVP, sw, sh);
                glm::vec2 sR  = w2s(R,     m_perspVP, sw, sh);
                glm::vec2 sL  = w2s(L,     m_perspVP, sw, sh);
                renderer.quads().drawQuadCorners(
                    sPL, sPR, sR, sL,
                    holdBodyColor, {0.f, 0.f, 1.f, 1.f},
                    renderer.whiteView(), renderer.whiteSampler(),
                    renderer.context(), renderer.descriptors());
            }
            prevL = L; prevR = R; havePrev = true;
        }

        // Sample-point markers (small bright yellow squares on the ribbon).
        // Kept visible through the whole hold window so the player can see
        // which checkpoints are still ahead and which have already passed.
        for (const auto& sp : hold->samplePoints) {
            float tOff = sp.tOffset;
            if (tOff < 0.f || tOff > dur) continue;
            double absT = note.time + tOff;

            float lane = evalHoldLaneAt(*hold, tOff);
            float wx   = laneToWorldX(lane);
            float wz   = -static_cast<float>(absT - m_songTime) * SCROLL_SPEED;
            if (wz > 12.f || wz < APPROACH_Z - 1.f) continue;

            float r = m_noteWorldW * 0.25f;
            glm::vec3 wNL{wx - r, 0.f, wz + r};
            glm::vec3 wNR{wx + r, 0.f, wz + r};
            glm::vec3 wFR{wx + r, 0.f, wz - r};
            glm::vec3 wFL{wx - r, 0.f, wz - r};
            renderer.quads().drawQuadCorners(
                w2s(wNL, m_perspVP, sw, sh),
                w2s(wNR, m_perspVP, sw, sh),
                w2s(wFR, m_perspVP, sw, sh),
                w2s(wFL, m_perspVP, sw, sh),
                {1.f, 0.95f, 0.3f, 0.95f}, {0.f, 0.f, 1.f, 1.f},
                renderer.whiteView(), renderer.whiteSampler(),
                renderer.context(), renderer.descriptors());
        }
    }

    // Notes (heads)
    for (auto& note : m_notes) {
        float laneX = 0.f;
        if      (auto* tap   = std::get_if<TapData>  (&note.data)) laneX = tap->laneX;
        else if (auto* hold  = std::get_if<HoldData> (&note.data)) laneX = hold->laneX;
        else if (auto* flick = std::get_if<FlickData>(&note.data)) laneX = flick->laneX;
        else continue;

        float timeDiff = static_cast<float>(note.time - m_songTime);
        float noteZ    = -timeDiff * SCROLL_SPEED;
        // Only render notes on the visible highway section. Holds keep their
        // head quad visible longer so the reference stays on screen while
        // the player is still tracking the body.
        const float upperClip = (note.type == NoteType::Hold) ? 12.f : 2.f;
        if (noteZ > upperClip || noteZ < APPROACH_Z - 2.f) continue;

        float worldX = (laneX - (m_laneCount - 1) * 0.5f) * m_laneSpacing;

        // Skip rendering notes that were already hit (they get particle effect instead)
        if (m_hitNotes.count(note.id)) continue;

        // Project 4 ground-plane corners so the note follows the same perspective
        // foreshortening as the lanes — far edge is narrower than near edge.
        float hw = m_noteWorldW * 0.5f;                 // half-width across lane
        float hd = m_noteWorldW * 0.4f * 0.5f;          // half-depth along scroll axis
        glm::vec3 wNL{worldX - hw, 0.f, noteZ + hd};    // near-left  (closer to camera)
        glm::vec3 wNR{worldX + hw, 0.f, noteZ + hd};    // near-right
        glm::vec3 wFR{worldX + hw, 0.f, noteZ - hd};    // far-right
        glm::vec3 wFL{worldX - hw, 0.f, noteZ - hd};    // far-left

        glm::vec4 cNL = m_perspVP * glm::vec4(wNL, 1.f);
        glm::vec4 cNR = m_perspVP * glm::vec4(wNR, 1.f);
        glm::vec4 cFR = m_perspVP * glm::vec4(wFR, 1.f);
        glm::vec4 cFL = m_perspVP * glm::vec4(wFL, 1.f);
        if (cNL.w <= 0.f || cNR.w <= 0.f || cFR.w <= 0.f || cFL.w <= 0.f) continue;

        glm::vec2 sNL = w2s(wNL, m_perspVP, sw, sh);
        glm::vec2 sNR = w2s(wNR, m_perspVP, sw, sh);
        glm::vec2 sFR = w2s(wFR, m_perspVP, sw, sh);
        glm::vec2 sFL = w2s(wFL, m_perspVP, sw, sh);

        // Cull tiny far notes
        float nearW = std::abs(sNR.x - sNL.x);
        if (nearW < 2.f) continue;

        glm::vec4 color = {1.f, 0.8f, 0.2f, 1.f};          // Tap: yellow
        if (note.type == NoteType::Hold)  color = m_activeHoldIds.count(note.id)
                                              ? glm::vec4{0.6f, 2.4f, 3.0f, 1.f}   // active → bloom
                                              : glm::vec4{0.2f, 0.8f, 1.f, 1.f};   // Hold: cyan
        if (note.type == NoteType::Flick) color = {1.f, 0.3f, 0.3f, 1.f};   // Flick: red
        if (note.type == NoteType::Drag)  color = {0.6f, 1.f, 0.4f, 0.85f}; // Drag: green
        if (note.type == NoteType::Slide) color = {0.8f, 0.4f, 1.f, 1.f};   // Slide: purple

        // Order: NL, NR, FR, FL — matches drawQuad's BL,BR,TR,TL winding so the
        // existing index pattern (0,1,2, 2,3,0) tessellates correctly.
        renderer.quads().drawQuadCorners(
            sNL, sNR, sFR, sFL,
            color, {0.f, 0.f, 1.f, 1.f},
            renderer.whiteView(), renderer.whiteSampler(),
            renderer.context(), renderer.descriptors());
    }

    // Judgment displays removed — using particle effects only
}

void BandoriRenderer::showJudgment(int lane, Judgment judgment) {
    if (lane < 0 || lane >= static_cast<int>(m_judgmentDisplays.size())) return;

    m_judgmentDisplays[lane].spawn(judgment, {0.f, 0.f});

    // Mark the closest note in this lane as hit — EXCEPT Hold notes, which
    // must stay visible until the player releases the key/finger. The hold
    // body (and head) get culled naturally once the whole shape scrolls past
    // the hit zone clip window; marking them here would make them vanish the
    // instant the head is struck, which also kills every downstream sample
    // tick's rendered marker.
    float bestDist = 999.f;
    uint32_t bestId = 0;
    bool found = false;
    for (auto& note : m_notes) {
        if (note.type == NoteType::Hold) continue;
        if (m_hitNotes.count(note.id)) continue;
        int noteLane = -1;
        if (auto* tap = std::get_if<TapData>(&note.data))        noteLane = static_cast<int>(std::lround(tap->laneX));
        else if (auto* flick = std::get_if<FlickData>(&note.data)) noteLane = static_cast<int>(std::lround(flick->laneX));
        if (noteLane != lane) continue;
        float d = std::abs((float)(note.time - m_songTime));
        if (d > 0.15f) continue;
        if (d < bestDist) { bestDist = d; bestId = note.id; found = true; }
    }
    if (found) m_hitNotes.insert(bestId);

    // Particle effect — Miss gets nothing, others get colored burst
    if (judgment != Judgment::Miss && m_renderer) {
        float sw = (float)m_width, sh = (float)m_height;
        float laneX = (lane - (m_laneCount - 1) * 0.5f) * m_laneSpacing;
        glm::vec2 hitPos = w2s({laneX, 0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);

        glm::vec4 pColor;
        int pCount = 12;
        switch (judgment) {
            case Judgment::Perfect: pColor = {0.2f, 1.f, 0.3f, 1.f}; pCount = 20; break;
            case Judgment::Good:    pColor = {0.3f, 0.6f, 1.f, 1.f}; pCount = 14; break;
            case Judgment::Bad:     pColor = {1.f, 0.25f, 0.2f, 1.f}; pCount = 8; break;
            default: break;
        }
        m_renderer->particles().emitBurst(hitPos, pColor, pCount, 200.f, 8.f, 0.5f);
    }
}

void BandoriRenderer::onShutdown(Renderer& renderer) {
    m_renderer = nullptr;
    m_notes.clear();
    m_hitNotes.clear();
    for (auto& d : m_judgmentDisplays) d = JudgmentDisplay{};
}
