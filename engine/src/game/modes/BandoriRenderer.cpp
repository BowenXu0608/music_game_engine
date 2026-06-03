#include "BandoriRenderer.h"
#include "renderer/Renderer.h"
#include "renderer/Material.h"
#include "renderer/MaterialAssetLibrary.h"
#include "renderer/ParticleEffectLibrary.h"
#include "renderer/ParticleSlots.h"
#include "ui/ProjectHub.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <vector>

namespace {
constexpr float kActiveHoldGlowIntensity = 1.0f;

// Slot ids mirror MaterialSlots.cpp::kBandoriSlots.
enum BandoriSlot : uint16_t {
    SlotTapNote        = 0,
    SlotHoldBody       = 1,
    SlotHoldBodyActive = 2,
    SlotHoldHead       = 3,
    SlotHoldHeadActive = 4,
    SlotFlickNote      = 5,
    SlotDragNote       = 6,
    SlotSlideNote      = 7,
    SlotSampleMarker   = 8,
    SlotLaneDivider    = 9,
    SlotHitZoneLine    = 10,
    SlotHitZoneGlow    = 11,
    SlotTrackSurface   = 12,
};

} // namespace

Material BandoriRenderer::slotOrFallback(uint16_t slot, const Material& fallback) const {
    auto it = m_chartMaterials.find(slot);
    if (it == m_chartMaterials.end()) return fallback;
    Material m = it->second;
    if (m.cls == MaterialClass::Pbr)
        return m;   // PBR keeps its resolved baseColor/normal views
    // Legacy effect path: texture/sampler come from the fallback.
    m.texture = fallback.texture;
    m.sampler = fallback.sampler;
    return m;
}

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

    // Import per-slot material overrides from the chart. Each entry either
    // references a project-level MaterialAsset (via md.assetName) or carries
    // inline legacy fields — resolveMaterial() picks whichever is populated.
    m_chartMaterials.clear();
    for (const auto& md : chart.materials) {
        Material m = resolveMaterial(md, m_materialLibrary);
        renderer.resolvePbrTextures(m);
        m_chartMaterials[md.slot] = m;
    }

    // Apply camera config (eye/target are baked baselines, not from config —
    // see onResize; cameraDistance/FovDeg are relative knobs over that).
    if (config) {
        m_camDistance = config->cameraDistance;
        m_camFovDeg   = config->cameraFovDeg;
        m_playfieldWidthPct = config->playfieldWidthPct;
        m_playfieldHeightPct = config->playfieldHeightPct;
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

    // Index hold notes by id so the sustained aura can evaluate each hold's
    // cross-lane curve at the judgment line, and resolve the bound effects.
    m_holdNoteIdx.clear();
    for (size_t i = 0; i < m_notes.size(); ++i) {
        if (m_notes[i].type == NoteType::Hold)
            m_holdNoteIdx[m_notes[i].id] = i;
    }
    resolveParticleEffects(config);

    onResize(renderer.width(), renderer.height());
}

void BandoriRenderer::resolveParticleEffects(const GameModeConfig* config) {
    m_particleEffects.clear();
    if (!m_particleLibrary || !m_renderer) return;

    const std::string mode = "bandori";
    for (const auto& slot : bandoriParticleSlots()) {
        std::string name = defaultParticleEffectName(mode, slot.slug);
        if (config) {
            auto b = config->particleEffects.find(slot.slug);
            if (b != config->particleEffects.end() && !b->second.empty())
                name = b->second;
        }
        const ParticleEffectAsset* a = m_particleLibrary->get(name);
        if (!a)   // bound asset missing — fall back to the seeded default
            a = m_particleLibrary->get(defaultParticleEffectName(mode, slot.slug));
        if (!a) continue;

        uint16_t pipeKey = 0;
        if (a->kind == ParticleEffectKind::Custom && !a->customShaderPath.empty()) {
            std::string abs =
                (m_particleLibrary->projectDir() / a->customShaderPath).string();
            pipeKey = m_renderer->particles().registerCustomPipeline(abs);
        }
        m_particleEffects[slot.slug] = particleEmitFromAsset(*a, pipeKey);
    }
}

glm::vec2 BandoriRenderer::laneHitPos(float lane) const {
    float sw = (float)m_width, sh = (float)m_height;
    float laneX = (lane - (m_laneCount - 1) * 0.5f) * m_laneSpacing;
    return w2s({laneX, 0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
}

void BandoriRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;

    float aspect = h > 0 ? static_cast<float>(w) / h : 1.f;

    // Fixed camera. Its position, pitch and FOV never change, so the lane-
    // convergence angle is constant. The two author knobs map to orthogonal,
    // angle-preserving effects (no scaling of the field):
    //   Camera Distance  -> m_approachZ: how far the highway is drawn toward the
    //     vanishing point, i.e. how much track is visible ahead.
    //   Playfield Height -> anchorNdcY: vertical screen position of the judgment
    //     line (a pure vertical shift; more room above as it lowers).
    // cameraFovDeg overrides the baseline FOV when > 0.
    // NOTE: this math is mirrored in SongEditor::renderSceneView (dual-site).
    const glm::vec3 camEye{0.f, 5.f, 8.f};
    const glm::vec3 camTarget{0.f, 0.f, -24.f};
    const float     baseFov = 55.f;

    // Camera Distance -> visible track length.
    float dN     = (std::clamp(m_camDistance, 0.5f, 2.0f) - 0.5f) / 1.5f;
    m_approachZ  = -glm::mix(20.f, 110.f, dN);
    // Playfield Height -> judgment-line screen position (flipped NDC: +1=bottom).
    float hN          = (std::clamp(m_playfieldHeightPct, 0.3f, 1.f) - 0.3f) / 0.7f;
    float anchorNdcY  = glm::mix(0.2f, 0.85f, hN);

    float     camFov = m_camFovDeg > 0.f ? m_camFovDeg : baseFov;
    Camera persp = Camera::makePerspective(camFov, aspect, 0.1f, 300.f);
    persp.lookAt(camEye, camTarget);

    // Pin the judgment line (z=0) to anchorNdcY — a vertical clip-space shift
    // only, so the camera, FOV and lane angle are all untouched.
    {
        glm::mat4 proj = persp.projection();
        glm::vec4 hitClip = (proj * persp.view()) * glm::vec4(0.f, 0.f, 0.f, 1.f);
        if (std::abs(hitClip.w) > 1e-5f) {
            float delta = anchorNdcY - hitClip.y / hitClip.w;
            for (int k = 0; k < 4; ++k) proj[k][1] += delta * proj[k][3];
        }
        persp.setProj(proj);
    }
    m_perspVP  = persp.viewProjection();
    m_proj11y  = std::abs(persp.projection()[1][1]);

    // Playfield Width -> bottom width of the highway at the hit line.
    {
        glm::vec2 lT = w2s({-1.f, 0.f, HIT_ZONE_Z}, m_perspVP, (float)w, (float)h);
        glm::vec2 rT = w2s({ 1.f, 0.f, HIT_ZONE_Z}, m_perspVP, (float)w, (float)h);
        float pxPerWorldUnit = (rT.x - lT.x) * 0.5f;
        if (pxPerWorldUnit > 0.f) {
            float widthPct  = std::clamp(m_playfieldWidthPct, 0.2f, 1.f);
            float desiredPx = (float)w * widthPct;
            m_laneSpacing   = desiredPx / pxPerWorldUnit / m_laneCount;
            m_noteWorldW    = m_laneSpacing;
        }
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

    // Track surface — filled trapezoid behind the dividers, spanning from the
    // leftmost lane edge to the rightmost, near plane to vanishing point.
    {
        float leftX  = -(m_laneCount * 0.5f) * m_laneSpacing;
        float rightX =  (m_laneCount * 0.5f) * m_laneSpacing;
        glm::vec2 sNL = w2s({leftX,  0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        glm::vec2 sNR = w2s({rightX, 0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        glm::vec2 sFR = w2s({rightX, 0.f, m_approachZ}, m_perspVP, sw, sh);
        glm::vec2 sFL = w2s({leftX,  0.f, m_approachZ}, m_perspVP, sw, sh);
        Material trackDefault;
        trackDefault.kind    = MaterialKind::Unlit;
        trackDefault.tint    = {0.08f, 0.10f, 0.18f, 0.8f};
        trackDefault.texture = renderer.whiteView();
        trackDefault.sampler = renderer.whiteSampler();
        Material trackMat = slotOrFallback(SlotTrackSurface, trackDefault);
        renderer.quads().drawQuadCorners(
            sNL, sNR, sFR, sFL,
            trackMat, {0.f, 0.f, 1.f, 1.f},
            renderer.context(), renderer.descriptors());
    }

    // Lane dividers converge to vanishing point — one edge line per boundary
    Material laneDefault;
    laneDefault.tint = {1.f, 1.f, 1.f, 0.2f};
    glm::vec4 laneTint = slotOrFallback(SlotLaneDivider, laneDefault).tint;
    for (int i = 0; i <= m_laneCount; ++i) {
        float wx   = (i - m_laneCount * 0.5f) * m_laneSpacing;
        glm::vec2 nearPt = w2s({wx, 0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        glm::vec2 farPt  = w2s({wx, 0.f, m_approachZ}, m_perspVP, sw, sh);
        renderer.lines().drawLine(nearPt, farPt, 1.5f, laneTint);
    }

    // Hit zone line across all lanes (bright, thick)
    {
        float leftX  = -(m_laneCount * 0.5f) * m_laneSpacing;
        float rightX =  (m_laneCount * 0.5f) * m_laneSpacing;
        glm::vec2 l = w2s({leftX,  0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        glm::vec2 r = w2s({rightX, 0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        Material hzLineDefault;  hzLineDefault.tint = {1.f, 0.9f, 0.2f, 1.f};
        Material hzGlowDefault;  hzGlowDefault.tint = {1.f, 0.9f, 0.2f, 0.3f};
        glm::vec4 hzLineTint = slotOrFallback(SlotHitZoneLine, hzLineDefault).tint;
        glm::vec4 hzGlowTint = slotOrFallback(SlotHitZoneGlow, hzGlowDefault).tint;
        renderer.lines().drawLine(l, r, 4.f, hzLineTint);  // bright yellow
        renderer.lines().drawLine(l, r, 8.f, hzGlowTint);  // glow
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

        // While the hold is actively being held, use the Glow material so the
        // shader adds an emissive boost the bloom post-process picks up.
        // Otherwise render plain Unlit. Chart overrides (if present) win.
        Material holdBodyDefault;
        holdBodyDefault.texture = renderer.whiteView();
        holdBodyDefault.sampler = renderer.whiteSampler();
        if (holdActive) {
            holdBodyDefault.kind     = MaterialKind::Glow;
            holdBodyDefault.tint     = {0.3f, 0.8f, 1.f, 0.95f};
            holdBodyDefault.params.x = kActiveHoldGlowIntensity;
        } else {
            holdBodyDefault.kind = MaterialKind::Unlit;
            holdBodyDefault.tint = {0.2f, 0.8f, 1.f, 0.85f};
        }
        Material holdBodyMat = slotOrFallback(
            holdActive ? SlotHoldBodyActive : SlotHoldBody, holdBodyDefault);

        // Tessellate the *visible* portion of the hold uniformly. Previously
        // we sampled tOff in [0, dur] and culled out-of-window segments,
        // which made long holds spawn in chunks at the far plane: each
        // sample only popped in once its wz crossed the cull line, so the
        // ribbon visibly extended itself N times. Instead, derive the tOff
        // window directly from the visible Z range and tessellate inside it
        // so the visible ribbon is always continuous from the very moment
        // any part of the hold enters the highway.
        //
        //   wz = -(absT - songTime) * (SCROLL_SPEED * m_noteSpeedMul)
        //   absT = note.time + tOff
        //   ⇒ tOff = (-wz / (SCROLL_SPEED * m_noteSpeedMul)) - (note.time - songTime)
        // While actively holding, hide everything past the judgement line
        // (wz > 0). Before the hold is started, allow a small +12 of past
        // overshoot so the head doesn't pop out the moment it crosses the
        // line — gives the player a frame or two to react.
        const float zNear = holdActive ? 0.f : 12.f;
        const float zFar  = m_approachZ - 2.f;
        const float dt    = static_cast<float>(note.time - m_songTime);
        const float tOffAtZNear = (-zNear / (SCROLL_SPEED * m_noteSpeedMul)) - dt;
        const float tOffAtZFar  = (-zFar  / (SCROLL_SPEED * m_noteSpeedMul)) - dt;
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
            float wz   = -static_cast<float>(absT - m_songTime) * (SCROLL_SPEED * m_noteSpeedMul);

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
                    holdBodyMat, {0.f, 0.f, 1.f, 1.f},
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
            float wz   = -static_cast<float>(absT - m_songTime) * (SCROLL_SPEED * m_noteSpeedMul);
            if (wz > 12.f || wz < m_approachZ - 1.f) continue;

            float r = m_noteWorldW * 0.25f;
            glm::vec3 wNL{wx - r, 0.f, wz + r};
            glm::vec3 wNR{wx + r, 0.f, wz + r};
            glm::vec3 wFR{wx + r, 0.f, wz - r};
            glm::vec3 wFL{wx - r, 0.f, wz - r};
            Material sampleDefault;
            sampleDefault.kind    = MaterialKind::Unlit;
            sampleDefault.tint    = {1.f, 0.95f, 0.3f, 0.95f};
            sampleDefault.texture = renderer.whiteView();
            sampleDefault.sampler = renderer.whiteSampler();
            Material sampleMat = slotOrFallback(SlotSampleMarker, sampleDefault);
            renderer.quads().drawQuadCorners(
                w2s(wNL, m_perspVP, sw, sh),
                w2s(wNR, m_perspVP, sw, sh),
                w2s(wFR, m_perspVP, sw, sh),
                w2s(wFL, m_perspVP, sw, sh),
                sampleMat, {0.f, 0.f, 1.f, 1.f},
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
        float noteZ    = -timeDiff * (SCROLL_SPEED * m_noteSpeedMul);
        // Only render notes on the visible highway section. Holds keep their
        // head quad visible longer so the reference stays on screen while
        // the player is still tracking the body.
        const float upperClip = (note.type == NoteType::Hold) ? 12.f : 2.f;
        if (noteZ > upperClip || noteZ < m_approachZ - 2.f) continue;

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

        Material headDefault;
        headDefault.kind    = MaterialKind::Unlit;
        headDefault.texture = renderer.whiteView();
        headDefault.sampler = renderer.whiteSampler();
        headDefault.tint    = {1.f, 0.8f, 0.2f, 1.f};          // Tap: yellow
        uint16_t headSlot = SlotTapNote;
        if (note.type == NoteType::Hold) {
            if (m_activeHoldIds.count(note.id)) {
                headDefault.kind     = MaterialKind::Glow;
                headDefault.tint     = {0.3f, 0.8f, 1.f, 1.f};
                headDefault.params.x = kActiveHoldGlowIntensity;
                headSlot = SlotHoldHeadActive;
            } else {
                headDefault.tint = {0.2f, 0.8f, 1.f, 1.f};     // Hold: cyan
                headSlot = SlotHoldHead;
            }
        }
        if (note.type == NoteType::Flick) { headDefault.tint = {1.f, 0.3f, 0.3f, 1.f};   headSlot = SlotFlickNote; }
        if (note.type == NoteType::Drag)  { headDefault.tint = {0.6f, 1.f, 0.4f, 0.85f}; headSlot = SlotDragNote;  }
        if (note.type == NoteType::Slide) { headDefault.tint = {0.8f, 0.4f, 1.f, 1.f};   headSlot = SlotSlideNote; }
        Material headMat = slotOrFallback(headSlot, headDefault);

        // Order: NL, NR, FR, FL — matches drawQuad's BL,BR,TR,TL winding so the
        // existing index pattern (0,1,2, 2,3,0) tessellates correctly.
        renderer.quads().drawQuadCorners(
            sNL, sNR, sFR, sFL,
            headMat, {0.f, 0.f, 1.f, 1.f},
            renderer.context(), renderer.descriptors());
    }

    // Judgment displays removed — using particle effects only
}

void BandoriRenderer::showJudgment(int lane, Judgment judgment, float timingDelta) {
    if (lane < 0 || lane >= static_cast<int>(m_judgmentDisplays.size())) return;

    // Anchor the floating text just above the actual judgment line, at this
    // lane's center, sized to the lane. laneHitPos uses the w2s convention
    // (y=0 = bottom), so window-space Y = 1 - y/height. Lane width comes from
    // projecting the two lane edges at the hit line.
    float fw = (m_width  > 0) ? (float)m_width  : 1.f;
    float fh = (m_height > 0) ? (float)m_height : 1.f;
    glm::vec2 pc = laneHitPos(static_cast<float>(lane));
    glm::vec2 pl = laneHitPos(lane - 0.5f);
    glm::vec2 pr = laneHitPos(lane + 0.5f);
    float normX   = std::clamp(pc.x / fw, 0.f, 1.f);
    float hitY01  = std::clamp(1.f - pc.y / fh, 0.f, 1.f);
    float laneWpx = std::abs(pr.x - pl.x);
    int   sign    = (timingDelta > 1e-4f) ? 1 : (timingDelta < -1e-4f ? -1 : 0);
    m_judgmentDisplays[lane].spawn(judgment, normX, hitY01, laneWpx, sign);

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
    // Particle emission now lives in showHitEffect() so each gameplay event
    // can fire its own author-bound effect; showJudgment only drives the text.
}

void BandoriRenderer::showHitEffect(HitEventKind kind, int lane,
                                    NoteType /*type*/, Judgment judgment) {
    if (judgment == Judgment::Miss || !m_renderer) return;

    const char* slug = nullptr;
    switch (kind) {
        case HitEventKind::ClickHit: slug = "click_hit"; break;
        case HitEventKind::FlickHit: slug = "flick_hit"; break;
        case HitEventKind::HoldHead: slug = "hold_head"; break;
        case HitEventKind::HoldTick: slug = "hold_tick"; break;
        case HitEventKind::HoldEnd:  slug = "hold_end";  break;
    }
    if (!slug) return;
    auto it = m_particleEffects.find(slug);
    if (it == m_particleEffects.end()) return;

    glm::vec2 pos = laneHitPos(lane);
    if (pos.x < -9000.f) return;   // behind camera
    m_renderer->particles().emitEffect(it->second, pos);
}

void BandoriRenderer::emitHoldAura(float dt) {
    if (!m_renderer) return;
    auto it = m_particleEffects.find("hold_aura");
    if (it == m_particleEffects.end()) return;

    for (uint32_t id : m_activeHoldIds) {
        auto lit = m_holdNoteIdx.find(id);
        if (lit == m_holdNoteIdx.end()) continue;
        const NoteEvent& note = m_notes[lit->second];
        const auto* hold = std::get_if<HoldData>(&note.data);
        if (!hold) continue;

        // Lane of the hold segment currently crossing the judgment line:
        // tOff = now - holdStart, evaluated on the cross-lane curve. This makes
        // the aura follow the note as it changes lanes (fractional during a
        // transition), instead of sticking to the start lane.
        float tOff = static_cast<float>(m_songTime - note.time);
        tOff = std::clamp(tOff, 0.f, hold->duration);
        float lane = evalHoldLaneAt(*hold, tOff);

        glm::vec2 pos = laneHitPos(lane);
        if (pos.x < -9000.f) continue;
        m_renderer->particles().emitSustained(id, it->second, pos, dt);
    }
}

void BandoriRenderer::onShutdown(Renderer& renderer) {
    m_renderer = nullptr;
    m_notes.clear();
    m_hitNotes.clear();
    for (auto& d : m_judgmentDisplays) d = JudgmentDisplay{};
}
