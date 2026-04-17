#include "LanotaRenderer.h"
#include "renderer/Renderer.h"
#include "ui/ProjectHub.h"   // GameModeConfig definition
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>
#include <unordered_map>

static constexpr float TWO_PI = 6.28318530717958f;
static constexpr float PI     = 3.14159265358979f;

// Project world pos → screen coords (y=0 bottom, y=h top).
// Vulkan-corrected perspective: NDC Y=+1 → y=0 (bottom), NDC Y=-1 → y=h (top).
glm::vec2 LanotaRenderer::w2s(glm::vec3 pos, const glm::mat4& vp, float sw, float sh) {
    glm::vec4 clip = vp * glm::vec4(pos, 1.f);
    if (clip.w <= 0.f) return {-9999.f, -9999.f};
    return {
        (clip.x / clip.w * 0.5f + 0.5f) * sw,
        (0.5f - clip.y / clip.w * 0.5f) * sh
    };
}

void LanotaRenderer::onInit(Renderer& renderer, const ChartData& chart,
                            const GameModeConfig* config) {
    m_renderer = &renderer;

    // Seed disk layout from the per-song config (falls back to defaults).
    if (config) {
        if (config->diskInnerRadius  > 0.f) INNER_RADIUS = config->diskInnerRadius;
        if (config->diskBaseRadius   > 0.f) BASE_RADIUS  = config->diskBaseRadius;
        if (config->diskRingSpacing  > 0.f) RING_SPACING = config->diskRingSpacing;
        if (config->diskInitialScale > 0.f) {
            m_diskInitialScale = config->diskInitialScale;
            m_diskScale        = config->diskInitialScale;
        }
    }

    onResize(renderer.width(), renderer.height());

    // Track count for the lane→angle fallback (used when the chart has no
    // LanotaRingData notes — e.g. a chart authored in Drop-Notes mode that the
    // user is now playing in Circle mode).  Also stored as m_trackCount so the
    // keyboard hit path (showJudgment) can reverse-map lane → angle.
    m_trackCount = (config && config->trackCount > 0) ? config->trackCount : 7;
    int trackCount = m_trackCount;
    std::cout << "[LanotaRenderer] onInit trackCount=" << trackCount
              << " chart.notes=" << chart.notes.size() << "\n";

    m_holdBodies.clear();
    for (const auto& note : chart.notes) {
        if (note.type != NoteType::Hold) continue;
        if (auto* hd = std::get_if<HoldData>(&note.data))
            m_holdBodies.push_back({note.id, note.time, *hd});
    }

    std::unordered_map<int, std::vector<NoteEvent>> ringNotes;
    for (auto& note : chart.notes)
        if (auto* rd = std::get_if<LanotaRingData>(&note.data))
            ringNotes[rd->ringIndex].push_back(note);

    // Fallback: chart has no native ring notes — synthesize one ring from
    // lane-based notes (TapData / HoldData / FlickData).  Each lane index is
    // mapped to an evenly-spaced angle around the disk.  Without this, picking
    // Circle mode for a Drop-Notes chart would render an empty screen.
    if (ringNotes.empty()) {
        std::vector<NoteEvent>& dst = ringNotes[0];
        for (auto& note : chart.notes) {
            int   lane = -1;
            int   span = 1;
            if      (auto* tap   = std::get_if<TapData>  (&note.data)) { lane = (int)tap->laneX;  span = tap->laneSpan;  }
            else if (auto* hold  = std::get_if<HoldData> (&note.data)) { lane = (int)hold->laneX; span = hold->laneSpan; }
            else if (auto* flick = std::get_if<FlickData>(&note.data)) { lane = (int)flick->laneX; }
            if (lane < 0) continue;

            // Lane 0 sits at the top of the disk (12 o'clock) and lane numbers
            // increase clockwise. World +Y projects to screen top, so clockwise
            // in screen space corresponds to *decreasing* angle — hence the
            // `π/2 − lane·θ` form (not `lane·θ − π/2`).
            float angle = PI * 0.5f - (static_cast<float>(lane) / trackCount) * TWO_PI;
            NoteEvent converted = note;
            converted.data = LanotaRingData{angle, 0, span};
            dst.push_back(converted);
        }
    }

    for (auto& [idx, notes] : ringNotes) {
        Ring ring{};
        ring.baseRadius    = BASE_RADIUS + idx * RING_SPACING;
        ring.radius        = ring.baseRadius;
        ring.rotationSpeed = 0.4f + idx * 0.15f;
        ring.currentAngle  = 0.f;
        ring.notes         = notes;
        m_rings.push_back(std::move(ring));
    }

    reloadDiskAnimation(chart.diskAnimation);
}

void LanotaRenderer::reloadDiskAnimation(const DiskAnimation& anim) {
    m_rotationEvents = anim.rotations;
    m_moveEvents     = anim.moves;
    m_scaleEvents    = anim.scales;
    auto byStart = [](const auto& a, const auto& b) { return a.startTime < b.startTime; };
    std::sort(m_rotationEvents.begin(), m_rotationEvents.end(), byStart);
    std::sort(m_moveEvents    .begin(), m_moveEvents    .end(), byStart);
    std::sort(m_scaleEvents   .begin(), m_scaleEvents   .end(), byStart);
}

void LanotaRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;
    rebuildPerspVP();
    m_camera = Camera::makeOrtho(0.f, static_cast<float>(w),
                                  static_cast<float>(h), 0.f);
}

// -----------------------------------------------------------------------------
// Perspective VP — rebuilt whenever the window resizes or m_diskCenter changes
// -----------------------------------------------------------------------------
// The camera eye and target are both offset by m_diskCenter, so the disk always
// appears at the screen centre projected from that world XY position.
// Everything that passes through m_perspVP (rings + notes via w2s) moves together.
void LanotaRenderer::rebuildPerspVP() {
    float aspect = m_height > 0 ? static_cast<float>(m_width) / m_height : 1.f;
    Camera persp = Camera::makePerspective(FOV_Y_DEG, aspect, 0.1f, 200.f);
    persp.lookAt(
        {m_diskCenter.x, m_diskCenter.y, 4.f},   // eye
        {m_diskCenter.x, m_diskCenter.y, 0.f});   // target (disk hit-plane)
    m_perspVP = persp.viewProjection();
    m_proj11y = std::abs(persp.projection()[1][1]);
}

// -----------------------------------------------------------------------------
// Easing functions — all map t∈[0,1] → [0,1]
// -----------------------------------------------------------------------------
float LanotaRenderer::applyEasing(float t, EasingType easing) {
    switch (easing) {
    case EasingType::SineInOut:
        // Smooth S-curve via cosine: slow start, fast middle, slow end
        return -(std::cos(PI * t) - 1.f) * 0.5f;

    case EasingType::QuadInOut:
        // Quadratic: t² accelerate / decelerate
        return t < 0.5f
            ? 2.f * t * t
            : 1.f - (-2.f * t + 2.f) * (-2.f * t + 2.f) * 0.5f;

    case EasingType::CubicInOut:
        // Cubic: t³ — snappier than quad in the middle
        return t < 0.5f
            ? 4.f * t * t * t
            : 1.f - (-2.f * t + 2.f) * (-2.f * t + 2.f) * (-2.f * t + 2.f) * 0.5f;

    case EasingType::Linear:
    default:
        return t;
    }
}

// -----------------------------------------------------------------------------
// Segment-based keyframe interpolation
// -----------------------------------------------------------------------------
// Each event describes a change that starts at `startTime` and completes
// `duration` seconds later.  Between the end of one event and the start of
// the next, the value holds.  Before the first event, the value is the base.
//
//     base ──┐            ┌─ e0.target ──┐           ┌─ e1.target ──────
//            │  (ease)    │   (hold)     │  (ease)   │
//            └────────────┘              └───────────┘
//            e0.start   e0.start+dur   e1.start   e1.start+dur
//
// The three helpers below share this structure; each specialises the
// interpolation for its value type (scalar, vec2, angle).

glm::vec2 LanotaRenderer::getDiskCenter(double songTime,
                                        const std::vector<MoveEvent>& events) {
    const glm::vec2 base{0.f, 0.f};
    if (events.empty() || songTime < events.front().startTime) return base;

    auto it = std::upper_bound(events.begin(), events.end(), songTime,
        [](double t, const MoveEvent& e){ return t < e.startTime; });
    const MoveEvent& cur = *std::prev(it);
    glm::vec2 prev = (std::prev(it) == events.begin())
        ? base : std::prev(it, 2)->target;

    double segEnd = cur.startTime + cur.duration;
    if (cur.duration <= 1e-6 || songTime >= segEnd) return cur.target;
    float t = static_cast<float>((songTime - cur.startTime) / cur.duration);
    float e = applyEasing(std::clamp(t, 0.f, 1.f), cur.easing);
    return prev + e * (cur.target - prev);
}

float LanotaRenderer::getCurrentRotation(double songTime,
                                         const std::vector<RotationEvent>& events) {
    const float base = 0.f;
    if (events.empty() || songTime < events.front().startTime) return base;

    auto it = std::upper_bound(events.begin(), events.end(), songTime,
        [](double t, const RotationEvent& e){ return t < e.startTime; });
    const RotationEvent& cur = *std::prev(it);
    float prev = (std::prev(it) == events.begin())
        ? base : std::prev(it, 2)->targetAngle;

    double segEnd = cur.startTime + cur.duration;
    if (cur.duration <= 1e-6 || songTime >= segEnd) return cur.targetAngle;
    float t = static_cast<float>((songTime - cur.startTime) / cur.duration);
    float e = applyEasing(std::clamp(t, 0.f, 1.f), cur.easing);
    return prev + e * (cur.targetAngle - prev);
}

float LanotaRenderer::getDiskScale(double songTime,
                                   const std::vector<ScaleEvent>& events) {
    const float base = 1.f;
    if (events.empty() || songTime < events.front().startTime) return base;

    auto it = std::upper_bound(events.begin(), events.end(), songTime,
        [](double t, const ScaleEvent& e){ return t < e.startTime; });
    const ScaleEvent& cur = *std::prev(it);
    float prev = (std::prev(it) == events.begin())
        ? base : std::prev(it, 2)->targetScale;

    double segEnd = cur.startTime + cur.duration;
    if (cur.duration <= 1e-6 || songTime >= segEnd) return cur.targetScale;
    float t = static_cast<float>((songTime - cur.startTime) / cur.duration);
    float e = applyEasing(std::clamp(t, 0.f, 1.f), cur.easing);
    return prev + e * (cur.targetScale - prev);
}

void LanotaRenderer::onUpdate(float /*dt*/, double songTime) {
    m_songTime = songTime;

    // Whole-disk rotation drives every ring uniformly so downstream code
    // (onRender, picking) keeps its `ring.currentAngle` reads and doesn't
    // need any per-ring branching.
    m_diskRotation = getCurrentRotation(m_songTime, m_rotationEvents);
    for (auto& ring : m_rings)
        ring.currentAngle = m_diskRotation;

    // Uniform disk scale: base radii are stored at init time; rescale each
    // frame so notes, dividers, and picks all see the same live value.
    m_diskScale = m_diskInitialScale * getDiskScale(m_songTime, m_scaleEvents);
    for (auto& ring : m_rings)
        ring.radius = ring.baseRadius * m_diskScale;

    // Update disk center from movement events and rebuild the perspective VP.
    // rebuildPerspVP() is cheap (one lookAt + one matrix multiply) so calling
    // it every frame is fine; skip only if there are no events at all.
    if (!m_moveEvents.empty()) {
        glm::vec2 newCenter = getDiskCenter(m_songTime, m_moveEvents);
        if (newCenter != m_diskCenter) {
            m_diskCenter = newCenter;
            rebuildPerspVP();
        }
    }
}

// Build a polyline approximating a world-space circle at Z=0 projected to screen.
void LanotaRenderer::buildRingPolyline(float radius, std::vector<glm::vec2>& out) const {
    float sw = static_cast<float>(m_width);
    float sh = static_cast<float>(m_height);
    out.resize(RING_SEGMENTS + 1);
    for (int i = 0; i <= RING_SEGMENTS; ++i) {
        float a = TWO_PI * i / RING_SEGMENTS;
        out[i] = w2s({cosf(a) * radius, sinf(a) * radius, 0.f}, m_perspVP, sw, sh);
    }
}

void LanotaRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    float sw = static_cast<float>(m_width);
    float sh = static_cast<float>(m_height);

    // ── Small (inner) spawn disk ─────────────────────────────────────────────
    // Drawn once; all rings share the same inner spawn radius. Notes fly
    // radially outward from this disk toward each ring's hit circle.
    {
        std::vector<glm::vec2> inner;
        buildRingPolyline((INNER_RADIUS * m_diskScale), inner);
        renderer.lines().drawPolyline(inner, 2.f, {0.35f, 0.55f, 0.9f, 0.6f}, true);
    }

    // ── Lane dividers + first-lane marker ────────────────────────────────────
    // Radial guides from the inner disk out to — but not past — the outer
    // hit disk, so nothing bleeds outside the large ring. Lane 0 sits at
    // the top (12 o'clock); lane numbers increase clockwise.
    if (m_trackCount > 0 && !m_rings.empty()) {
        float outermost = 0.f;
        for (auto& r : m_rings) outermost = std::max(outermost, r.radius);
        // Keep the divider's inner/outer endpoints strictly between the two
        // disks: a hair outside the inner disk and a hair inside the outer.
        const float dividerInner = (INNER_RADIUS * m_diskScale) + 0.01f;
        const float dividerOuter = outermost   - 0.01f;

        // Dividers are drawn at lane *boundaries*, half a lane offset from
        // lane *centers*. In the clockwise-decreasing convention, the CCW
        // (counter-clockwise, i.e. visually "left") edge of lane N sits at
        // (π/2 − N·θ) + θ/2 = π/2 − (N−½)·θ.
        float halfLane = PI / static_cast<float>(m_trackCount);  // ½·(2π/n)
        for (int lane = 0; lane < m_trackCount; ++lane) {
            float a = PI * 0.5f - (static_cast<float>(lane) / m_trackCount) * TWO_PI + halfLane;
            float ca = cosf(a), sa = sinf(a);
            glm::vec2 p0 = w2s({ca * dividerInner, sa * dividerInner, 0.f}, m_perspVP, sw, sh);
            glm::vec2 p1 = w2s({ca * dividerOuter, sa * dividerOuter, 0.f}, m_perspVP, sw, sh);

            glm::vec4 col{0.6f, 0.75f, 1.f, 0.35f}; // dim blue dividers
            renderer.lines().drawLine(p0, p1, 1.5f, col);
        }

        // Lane-0 center marker: short gold tick at 12 o'clock, sitting on
        // the outer edge of the large disk and pointing slightly inward.
        {
            float a = PI * 0.5f;
            float ca = cosf(a), sa = sinf(a);
            glm::vec2 p0 = w2s({ca * (dividerOuter - 0.25f), sa * (dividerOuter - 0.25f), 0.f},
                               m_perspVP, sw, sh);
            glm::vec2 p1 = w2s({ca * dividerOuter, sa * dividerOuter, 0.f},
                               m_perspVP, sw, sh);
            renderer.lines().drawLine(p0, p1, 3.f, {1.f, 0.9f, 0.3f, 0.95f});
        }
    }

    // Draw cross-lane hold bodies before note heads so heads render on top.
    drawHoldBodies(renderer);

    for (auto& ring : m_rings) {
        // Outer hit ring circle at Z=0
        std::vector<glm::vec2> pts;
        buildRingPolyline(ring.radius, pts);
        renderer.lines().drawPolyline(pts, 2.f, {0.5f, 0.7f, 1.f, 0.8f}, true);

        for (auto& note : ring.notes) {
            // Skip already-consumed notes (touch picked them, or showJudgment hit them).
            if (m_hitNotes.count(note.id)) continue;

            float timeDiff = static_cast<float>(note.time - m_songTime);
            if (timeDiff < -0.3f || timeDiff > APPROACH_SECS) continue;

            auto* rd = std::get_if<LanotaRingData>(&note.data);
            if (!rd) continue;

            // ring.currentAngle already encodes the disk rotation (from getCurrentRotation
            // or accumulated constant speed).  Adding it to the note's chart angle is
            // equivalent to rotating the whole disk — no model matrix needed here because
            // the world-space position is computed on the CPU before being handed to the
            // batcher.  The batcher draws all notes through the same shared VP; there is
            // no per-ring draw call to attach a transform to.
            //
            // Model-matrix rotation would only be preferable if notes were drawn via
            // instanced GPU calls with a per-ring uniform block.  In the CPU-batcher
            // architecture the trig is the transform, and this single line is it:
            // `rd->angle` is the *authored* lane's center angle. Wider notes
            // expand clockwise: a span-S note at lane N covers lanes N..N+S-1,
            // so its visual center sits (S-1)/2 lanes clockwise of rd->angle.
            int   span        = std::clamp(rd->laneSpan, 1, 3);
            float laneAngular = (m_trackCount > 0 ? TWO_PI / m_trackCount : TWO_PI / 7.f);
            float angle       = rd->angle + ring.currentAngle
                              - static_cast<float>(span - 1) * 0.5f * laneAngular;
            // Notes fly radially outward across a flat disk: at timeDiff ==
            // APPROACH_SECS the note spawns on the small inner disk, and at
            // timeDiff == 0 it reaches the large hit ring at ring.radius.
            float travelT    = std::clamp(timeDiff / APPROACH_SECS, 0.f, 1.f);
            float noteRadius = ring.radius - travelT * (ring.radius - (INNER_RADIUS * m_diskScale));
            float noteZ      = 0.f;

            // Centre clip-space test to cull notes behind the camera.
            glm::vec3 centerWorld{cosf(angle) * noteRadius,
                                  sinf(angle) * noteRadius,
                                  noteZ};
            glm::vec4 clipC = m_perspVP * glm::vec4(centerWorld, 1.f);
            if (clipC.w <= 0.f) continue;

            float alpha = timeDiff < 0.f
                ? std::max(0.f, 1.f + timeDiff / 0.3f)
                : 0.4f + 0.6f * std::max(0.f, 1.f - timeDiff / APPROACH_SECS);

            glm::vec4 color = (note.type == NoteType::Flick)
                ? glm::vec4{1.f, 0.35f, 0.35f, alpha}
                : glm::vec4{1.f, 0.85f, 0.3f,  alpha};

            // ── Curved arc tile, foreshortened by m_perspVP ──────────────────
            // The note is a tile on the disk (a plane parallel to z=0 at z=noteZ)
            // that hugs the ring: tangential extent along the arc, radial extent
            // across the ring's thickness.  Tessellate into N quads so the arc
            // bends smoothly with the ring instead of being a flat rectangle.
            constexpr int   NOTE_ARC_SEGMENTS = 12;
            // Full-lane-width arc: angular width = laneSpan * (2π / trackCount).
            // Small 2% padding keeps adjacent notes visually distinct when
            // two same-time notes sit in neighboring lanes. `span` and
            // `laneAngular` are already computed above.
            float angularHalf = 0.5f * span * laneAngular * 0.96f;
            const float radialHalf = (NOTE_WORLD_R * m_diskScale) * 0.55f;      // thickness across the ring
            float       innerR     = noteRadius - radialHalf;
            float       outerR     = noteRadius + radialHalf;

            // Pre-project all sample points along the arc on inner & outer rings.
            glm::vec2 inner[NOTE_ARC_SEGMENTS + 1];
            glm::vec2 outer[NOTE_ARC_SEGMENTS + 1];
            bool      anyOnscreen = false;
            for (int i = 0; i <= NOTE_ARC_SEGMENTS; ++i) {
                float t = static_cast<float>(i) / NOTE_ARC_SEGMENTS;
                float a = angle - angularHalf + t * (2.f * angularHalf);
                float ca = cosf(a), sa = sinf(a);
                glm::vec3 wIn { ca * innerR, sa * innerR, noteZ };
                glm::vec3 wOut{ ca * outerR, sa * outerR, noteZ };
                glm::vec4 cIn  = m_perspVP * glm::vec4(wIn,  1.f);
                glm::vec4 cOut = m_perspVP * glm::vec4(wOut, 1.f);
                if (cIn.w <= 0.f || cOut.w <= 0.f) {
                    // Mark this column as offscreen by setting a sentinel.
                    inner[i] = outer[i] = {-99999.f, -99999.f};
                    continue;
                }
                inner[i] = w2s(wIn,  m_perspVP, sw, sh);
                outer[i] = w2s(wOut, m_perspVP, sw, sh);
                anyOnscreen = true;
            }
            if (!anyOnscreen) continue;

            // Optional dark halo: build a slightly thicker arc just outside.
            float       haloRadial    = radialHalf * 1.35f;
            float       haloAngular   = angularHalf * 1.10f;
            float       haloInnerR    = noteRadius - haloRadial;
            float       haloOuterR    = noteRadius + haloRadial;
            glm::vec2   haloIn[NOTE_ARC_SEGMENTS + 1];
            glm::vec2   haloOut[NOTE_ARC_SEGMENTS + 1];
            bool        haloOk = true;
            for (int i = 0; i <= NOTE_ARC_SEGMENTS && haloOk; ++i) {
                float t = static_cast<float>(i) / NOTE_ARC_SEGMENTS;
                float a = angle - haloAngular + t * (2.f * haloAngular);
                float ca = cosf(a), sa = sinf(a);
                glm::vec3 wIn { ca * haloInnerR, sa * haloInnerR, noteZ };
                glm::vec3 wOut{ ca * haloOuterR, sa * haloOuterR, noteZ };
                glm::vec4 cIn  = m_perspVP * glm::vec4(wIn,  1.f);
                glm::vec4 cOut = m_perspVP * glm::vec4(wOut, 1.f);
                if (cIn.w <= 0.f || cOut.w <= 0.f) { haloOk = false; break; }
                haloIn[i]  = w2s(wIn,  m_perspVP, sw, sh);
                haloOut[i] = w2s(wOut, m_perspVP, sw, sh);
            }

            glm::vec4 haloColor{0.f, 0.f, 0.f, alpha * 0.5f};

            // Halo first (drawn behind the bright fill).
            if (haloOk) {
                for (int i = 0; i < NOTE_ARC_SEGMENTS; ++i) {
                    // Order: inner-current, outer-current, outer-next, inner-next
                    // → matches drawQuadCorners' BL,BR,TR,TL winding.
                    renderer.quads().drawQuadCorners(
                        haloIn[i],  haloOut[i],
                        haloOut[i+1], haloIn[i+1],
                        haloColor, {0.f, 0.f, 1.f, 1.f},
                        renderer.whiteView(), renderer.whiteSampler(),
                        renderer.context(), renderer.descriptors());
                }
            }

            // Bright fill on top.
            for (int i = 0; i < NOTE_ARC_SEGMENTS; ++i) {
                if (inner[i].x  < -90000.f || inner[i+1].x < -90000.f) continue;
                renderer.quads().drawQuadCorners(
                    inner[i],  outer[i],
                    outer[i+1], inner[i+1],
                    color, {0.f, 0.f, 1.f, 1.f},
                    renderer.whiteView(), renderer.whiteSampler(),
                    renderer.context(), renderer.descriptors());
            }
        }
    }
}

// Smooth (Catmull-Rom Hermite) lane evaluator used only for the visual hold
// body in Circle mode. The shared `evalHoldLaneAt` honours `transitionLen`
// so the lane is constant outside each waypoint's transition window — that's
// correct for hit detection but produces a sharp 90° kink in the rendered
// path at every waypoint. Here we ignore `transitionLen` entirely and treat
// each waypoint pair as a full Hermite cubic spanning the whole segment, so
// the path bends through corners as a Bezier-style arc.
static float evalHoldLaneSmoothLanota(const HoldData& h, float tOff) {
    if (h.waypoints.size() < 2) return evalHoldLaneAt(h, tOff);

    const auto& wp = h.waypoints;
    if (tOff <= wp.front().tOffset) return static_cast<float>(wp.front().lane);
    if (tOff >= wp.back().tOffset)  return static_cast<float>(wp.back().lane);

    for (size_t i = 1; i < wp.size(); ++i) {
        const auto& a = wp[i - 1];
        const auto& b = wp[i];
        if (tOff > b.tOffset) continue;

        const float segLen = b.tOffset - a.tOffset;
        if (segLen <= 1e-6f) return static_cast<float>(b.lane);

        const float la = static_cast<float>(a.lane);
        const float lb = static_cast<float>(b.lane);

        // Catmull-Rom tangents — central differences across neighbours so
        // each interior waypoint joins its segments with matching slopes
        // (C1 continuity → smooth bends instead of sharp corners).
        const float lPrev = (i >= 2)            ? static_cast<float>(wp[i - 2].lane) : la;
        const float lNext = (i + 1 < wp.size()) ? static_cast<float>(wp[i + 1].lane) : lb;
        const float ma    = 0.5f * (lb - lPrev);
        const float mb    = 0.5f * (lNext - la);

        const float u   = (tOff - a.tOffset) / segLen;
        const float u2  = u * u;
        const float u3  = u2 * u;
        const float h00 =  2.f * u3 - 3.f * u2 + 1.f;
        const float h10 =        u3 - 2.f * u2 + u;
        const float h01 = -2.f * u3 + 3.f * u2;
        const float h11 =        u3 -       u2;
        return h00 * la + h10 * ma + h01 * lb + h11 * mb;
    }
    return static_cast<float>(wp.back().lane);
}

void LanotaRenderer::drawHoldBodies(Renderer& renderer) {
    if (m_holdBodies.empty() || m_rings.empty()) return;

    float sw = static_cast<float>(m_width);
    float sh = static_cast<float>(m_height);

    // Use the outer ring's radius as the hold's "hit" radius. All holds share
    // ring 0 in the Circle-mode fallback.
    const float hitRadius = m_rings.front().radius;

    auto laneToAngle = [&](float lane) {
        return PI * 0.5f - (lane / static_cast<float>(m_trackCount)) * TWO_PI;
    };

    const float laneAngular = (m_trackCount > 0 ? TWO_PI / m_trackCount : TWO_PI / 7.f);

    for (const auto& hb : m_holdBodies) {
        const HoldData& h = hb.data;
        const double headT = hb.startTime;
        const double tailT = headT + h.duration;
        if (tailT < m_songTime - 0.3) continue;
        if (headT > m_songTime + APPROACH_SECS) continue;
        if (h.duration <= 0.f) continue;

        // Tessellate the hold body along its duration. Each sample gives an
        // angle (from the interpolated lane) and a radius (from timeDiff).
        const int N = (h.waypoints.size() > 1 || h.transition == HoldTransition::Curve) ? 36 : 16;
        const int span = std::clamp(h.laneSpan, 1, 3);
        const float halfA = 0.5f * span * laneAngular * 0.85f;

        // Rhomboid angular half-width — bulges during each rhomboid waypoint
        // transition window. Falls back to legacy single-transition spread.
        auto halfAAt = [&](float tOff) -> float {
            if (!h.waypoints.empty()) {
                int seg = holdActiveSegment(h, tOff);
                if (seg <= 0) return halfA;
                const auto& a = h.waypoints[seg - 1];
                const auto& b = h.waypoints[seg];
                if (b.style != HoldTransition::Rhomboid) return halfA;
                float tLen = std::max(0.f, b.transitionLen);
                if (tLen <= 0.f) return halfA;
                float u = (tOff - (b.tOffset - tLen)) / tLen;
                float tri = 1.f - std::abs(2.f * u - 1.f);
                float spreadA = std::abs((float)b.lane - (float)a.lane) * laneAngular;
                return halfA + tri * spreadA * 0.5f;
            }
            if (h.transition != HoldTransition::Rhomboid
                || h.effectiveEndLane() == h.laneX)
                return halfA;
            float tLen = std::clamp(h.transitionLen, 0.f, h.duration);
            if (tLen <= 0.f) return halfA;
            float tBegin = holdTransitionBegin(h);
            float tEnd   = tBegin + tLen;
            if (tOff <= tBegin || tOff >= tEnd) return halfA;
            float u = (tOff - tBegin) / tLen;
            float tri = 1.f - std::abs(2.f * u - 1.f);
            float spreadA = std::abs(h.effectiveEndLane() - h.laneX) * laneAngular;
            return halfA + tri * spreadA * 0.5f;
        };

        // Render each time slice as a true angular arc slice on the ring —
        // two points placed at (angle ± hA) on the circle of the note's
        // current radius. Adjacent slices connect into a curved sector that
        // follows the ring even when the hold stays in a single lane.
        const bool holdActive = m_activeHoldIds.count(hb.noteId) > 0;
        // Bright white-cyan core that blooms when the player is holding.
        const glm::vec4 bodyColor = holdActive
            ? glm::vec4{1.6f, 2.4f, 3.0f, 0.95f}
            : glm::vec4{0.85f, 1.05f, 1.35f, 0.95f};

        // ── Lanota-style hold body ─────────────────────────────────────────
        // In real Lanota a hold body is a curved 2D track laid out on the
        // disk: one end is the player's current expected position (the head
        // on the hit ring), the other end is where the head will be at the
        // tail's time. Lane shifts cause the track to bend across the disk,
        // and the smooth Catmull-Rom evaluator gives Bezier-like corners.
        //
        // We sample N+1 points along the *remaining* hold duration. Each
        // sample sits at (angle = lane→angle, radius = mapped from time)
        // and the beam thickness is added by offsetting ±halfW along the
        // local *path normal* in screen space, so the beam keeps a constant
        // visual width even as it curves through tight bends.
        const float innerEdge = INNER_RADIUS * m_diskScale;

        // Real-time radius mapping: each part of the hold body sits at the
        // radius given by its remaining travel time. Like a tap, samples
        // spawn at the inner disk and slide outward; once a sample's absT
        // catches up to the song time it pins to the hit ring. This lets
        // the *whole* beam — head and body — fly out from the small inner
        // disk instead of teleporting to the rim.
        auto radiusForAbsT = [&](double absT) {
            float td = static_cast<float>(absT - m_songTime);
            float u  = std::clamp(td / APPROACH_SECS, 0.f, 1.f);
            return hitRadius - u * (hitRadius - innerEdge);
        };

        // Visible portion: head end is max(headT, songTime) so once the head
        // lands it pins to the rim; tail end is min(tailT, songTime+APPROACH)
        // so the deepest visible sample sits just inside the inner disk.
        const double absStart = std::max(static_cast<double>(headT), m_songTime);
        const double absEnd   = std::min(static_cast<double>(tailT),
                                         m_songTime + APPROACH_SECS);
        if (absEnd <= absStart + 1e-6) continue;

        // Sample the spine of the beam first. Centres are kept in screen
        // space so we can offset perpendicular to the path tangent later.
        struct Spine { glm::vec2 c; bool ok; };
        std::vector<Spine> spine(N + 1);
        for (int i = 0; i <= N; ++i) {
            double absT = absStart + (absEnd - absStart) * (double)i / (double)N;
            float  tOff = std::clamp(static_cast<float>(absT - headT), 0.f, h.duration);
            float lane = evalHoldLaneSmoothLanota(h, tOff);
            float ang  = laneToAngle(lane) + m_rings.front().currentAngle
                       - static_cast<float>(span - 1) * 0.5f * laneAngular;
            float radius = radiusForAbsT(absT);
            glm::vec3 w{ cosf(ang) * radius, sinf(ang) * radius, 0.f };
            glm::vec4 c = m_perspVP * glm::vec4(w, 1.f);
            if (c.w <= 0.f) { spine[i] = {{}, false}; continue; }
            spine[i] = { w2s(w, m_perspVP, sw, sh), true };
        }

        // Build inner/outer ribbon points by offsetting each spine sample
        // perpendicular to its screen-space tangent. Constant pixel width.
        const float coreHalfPx = 6.0f;
        const float haloHalfPx = 13.0f;
        struct Edge { glm::vec2 inC, outC, inH, outH; bool ok; };
        std::vector<Edge> edges(N + 1);
        for (int i = 0; i <= N; ++i) {
            if (!spine[i].ok) { edges[i].ok = false; continue; }
            // Tangent from neighbour samples (central difference).
            int   ip = std::max(0, i - 1);
            int   in = std::min(N, i + 1);
            glm::vec2 a = spine[ip].ok ? spine[ip].c : spine[i].c;
            glm::vec2 b = spine[in].ok ? spine[in].c : spine[i].c;
            glm::vec2 t = b - a;
            float len = std::hypot(t.x, t.y);
            if (len < 1e-3f) { edges[i].ok = false; continue; }
            t /= len;
            glm::vec2 nrm{ -t.y, t.x };
            edges[i].inC  = spine[i].c - nrm * coreHalfPx;
            edges[i].outC = spine[i].c + nrm * coreHalfPx;
            edges[i].inH  = spine[i].c - nrm * haloHalfPx;
            edges[i].outH = spine[i].c + nrm * haloHalfPx;
            edges[i].ok   = true;
        }

        // Halo pass first (dim/wide), then bright core on top.
        auto drawRibbon = [&](float halfPx, glm::vec4 baseColor, bool isCore) {
            (void)halfPx;
            for (int i = 0; i + 1 <= N; ++i) {
                if (!edges[i].ok || !edges[i + 1].ok) continue;
                // Brightness ramps from dim tail to bright head.
                float u = 1.f - (float)i / (float)N;   // 1 at head, 0 at tail
                float fade = isCore ? (0.30f + 0.70f * (u * u))
                                    : (0.18f + 0.50f * (u * u));
                glm::vec4 col = baseColor;
                col.a *= fade;
                glm::vec2 inA  = isCore ? edges[i].inC     : edges[i].inH;
                glm::vec2 outA = isCore ? edges[i].outC    : edges[i].outH;
                glm::vec2 inB  = isCore ? edges[i + 1].inC : edges[i + 1].inH;
                glm::vec2 outB = isCore ? edges[i + 1].outC: edges[i + 1].outH;
                renderer.quads().drawQuadCorners(
                    inA, outA, outB, inB,
                    col, {0.f, 0.f, 1.f, 1.f},
                    renderer.whiteView(), renderer.whiteSampler(),
                    renderer.context(), renderer.descriptors());
            }
        };

        const glm::vec4 haloColor = holdActive
            ? glm::vec4{1.0f, 1.0f, 1.0f, 0.85f}
            : glm::vec4{0.55f, 0.80f, 1.0f, 0.85f};
        drawRibbon(haloHalfPx, haloColor, /*isCore=*/false);
        drawRibbon(coreHalfPx, bodyColor, /*isCore=*/true);

        // ── Head anchor: a small arc tile sitting on the rim where the
        // beam meets the disk edge. Real Lanota holds keep this lens-shaped
        // marker at the rim for the entire hold duration, so the player has
        // a clear visual reference for the lane they must keep holding. The
        // tile is built the same way as a tap-note arc tile: angularly
        // tessellated curved sector that hugs the ring.
        {
            float headTOff = std::clamp(static_cast<float>(absStart - headT),
                                        0.f, h.duration);
            float headLane = evalHoldLaneSmoothLanota(h, headTOff);
            float headAng  = laneToAngle(headLane) + m_rings.front().currentAngle
                           - static_cast<float>(span - 1) * 0.5f * laneAngular;
            float headHA   = 0.5f * span * laneAngular * 0.92f;
            float headRad  = radiusForAbsT(absStart);   // travels with the note
            float radHalf  = (NOTE_WORLD_R * m_diskScale) * 0.55f;
            float innerR   = headRad - radHalf;
            float outerR   = headRad + radHalf;
            constexpr int HEAD_SEGS = 12;
            glm::vec2 inPts[HEAD_SEGS + 1];
            glm::vec2 outPts[HEAD_SEGS + 1];
            bool      headOk = true;
            for (int k = 0; k <= HEAD_SEGS && headOk; ++k) {
                float t  = (float)k / (float)HEAD_SEGS;
                float a  = headAng - headHA + t * (2.f * headHA);
                float ca = cosf(a), sa = sinf(a);
                glm::vec3 wIn { ca * innerR, sa * innerR, 0.f };
                glm::vec3 wOut{ ca * outerR, sa * outerR, 0.f };
                glm::vec4 cI = m_perspVP * glm::vec4(wIn,  1.f);
                glm::vec4 cO = m_perspVP * glm::vec4(wOut, 1.f);
                if (cI.w <= 0.f || cO.w <= 0.f) { headOk = false; break; }
                inPts[k]  = w2s(wIn,  m_perspVP, sw, sh);
                outPts[k] = w2s(wOut, m_perspVP, sw, sh);
            }
            if (headOk) {
                glm::vec4 headCol = holdActive
                    ? glm::vec4{1.6f, 2.2f, 2.8f, 1.0f}
                    : glm::vec4{0.95f, 1.10f, 1.45f, 1.0f};
                for (int k = 0; k < HEAD_SEGS; ++k) {
                    renderer.quads().drawQuadCorners(
                        inPts[k],  outPts[k],
                        outPts[k + 1], inPts[k + 1],
                        headCol, {0.f, 0.f, 1.f, 1.f},
                        renderer.whiteView(), renderer.whiteSampler(),
                        renderer.context(), renderer.descriptors());
                }
            }
        }

        // Sample-point markers
        for (const auto& sp : h.samplePoints) {
            float tOff = sp.tOffset;
            if (tOff < 0.f || tOff > h.duration) continue;
            double absT = headT + tOff;
            if (absT < m_songTime - 0.05 || absT > m_songTime + APPROACH_SECS) continue;

            float timeDiff = static_cast<float>(absT - m_songTime);
            float travelT  = std::clamp(timeDiff / APPROACH_SECS, 0.f, 1.f);
            float radius   = hitRadius - travelT * (hitRadius - (INNER_RADIUS * m_diskScale));

            float lane  = evalHoldLaneAt(h, tOff);
            float angle = laneToAngle(lane) + m_rings.front().currentAngle
                        - static_cast<float>(span - 1) * 0.5f * laneAngular;
            float ca = cosf(angle), sa = sinf(angle);
            float r  = (NOTE_WORLD_R * m_diskScale) * 0.4f;
            glm::vec3 wA{ca * (radius - r), sa * (radius - r), 0.f};
            glm::vec3 wB{ca * (radius + r), sa * (radius + r), 0.f};
            glm::vec2 perp{-sa * r, ca * r};
            glm::vec2 sBL = w2s({wA.x - perp.x, wA.y - perp.y, 0.f}, m_perspVP, sw, sh);
            glm::vec2 sBR = w2s({wA.x + perp.x, wA.y + perp.y, 0.f}, m_perspVP, sw, sh);
            glm::vec2 sTR = w2s({wB.x + perp.x, wB.y + perp.y, 0.f}, m_perspVP, sw, sh);
            glm::vec2 sTL = w2s({wB.x - perp.x, wB.y - perp.y, 0.f}, m_perspVP, sw, sh);
            renderer.quads().drawQuadCorners(
                sBL, sBR, sTR, sTL,
                {1.f, 0.95f, 0.3f, 0.95f}, {0.f, 0.f, 1.f, 1.f},
                renderer.whiteView(), renderer.whiteSampler(),
                renderer.context(), renderer.descriptors());
        }
    }
}

void LanotaRenderer::onShutdown(Renderer& renderer) {
    m_rings.clear();
    m_hitNotes.clear();
    m_holdBodies.clear();
    m_renderer = nullptr;
}

// ── Hit picking + visual feedback ────────────────────────────────────────────

void LanotaRenderer::markNoteHit(uint32_t noteId) {
    m_hitNotes.insert(noteId);
}

bool LanotaRenderer::projectNoteScreen(uint32_t noteId, glm::vec2& outScreen) const {
    float sw = static_cast<float>(m_width);
    float sh = static_cast<float>(m_height);
    for (const auto& ring : m_rings) {
        for (const auto& note : ring.notes) {
            if (note.id != noteId) continue;
            const auto* rd = std::get_if<LanotaRingData>(&note.data);
            if (!rd) return false;
            int   span        = std::clamp(rd->laneSpan, 1, 3);
            float laneAngular = (m_trackCount > 0 ? TWO_PI / m_trackCount : TWO_PI / 7.f);
            float angle    = rd->angle + ring.currentAngle
                           - static_cast<float>(span - 1) * 0.5f * laneAngular;
            float timeDiff = static_cast<float>(note.time - m_songTime);
            float travelT    = std::clamp(timeDiff / APPROACH_SECS, 0.f, 1.f);
            float noteRadius = ring.radius - travelT * (ring.radius - (INNER_RADIUS * m_diskScale));
            glm::vec3 world{cosf(angle) * noteRadius,
                            sinf(angle) * noteRadius,
                            0.f};
            glm::vec4 clip = m_perspVP * glm::vec4(world, 1.f);
            if (clip.w <= 0.f) return false;
            outScreen = w2s(world, m_perspVP, sw, sh);
            return true;
        }
    }
    return false;
}

std::optional<LanotaRenderer::PickResult>
LanotaRenderer::pickNoteAt(glm::vec2 screenPx, double songTime, float pixelTol) const {
    float sw = static_cast<float>(m_width);
    float sh = static_cast<float>(m_height);
    const float HIT_WINDOW_SEC = 0.15f;

    std::optional<PickResult> best;
    float bestScore = std::numeric_limits<float>::max();
    const float pxTolSq = pixelTol * pixelTol;

    for (size_t r = 0; r < m_rings.size(); ++r) {
        const auto& ring = m_rings[r];
        for (const auto& note : ring.notes) {
            if (m_hitNotes.count(note.id)) continue;

            float timeDiff = static_cast<float>(note.time - songTime);
            if (std::abs(timeDiff) > HIT_WINDOW_SEC) continue;

            const auto* rd = std::get_if<LanotaRingData>(&note.data);
            if (!rd) continue;

            int   span        = std::clamp(rd->laneSpan, 1, 3);
            float laneAngular = (m_trackCount > 0 ? TWO_PI / m_trackCount : TWO_PI / 7.f);
            float angle      = rd->angle + ring.currentAngle
                             - static_cast<float>(span - 1) * 0.5f * laneAngular;
            float travelT    = std::clamp(timeDiff / APPROACH_SECS, 0.f, 1.f);
            float noteRadius = ring.radius - travelT * (ring.radius - (INNER_RADIUS * m_diskScale));
            glm::vec3 world{cosf(angle) * noteRadius,
                            sinf(angle) * noteRadius,
                            0.f};
            glm::vec4 clip = m_perspVP * glm::vec4(world, 1.f);
            if (clip.w <= 0.f) continue;

            glm::vec2 screen = w2s(world, m_perspVP, sw, sh);
            glm::vec2 d  = screen - screenPx;
            float distSq = d.x * d.x + d.y * d.y;
            if (distSq > pxTolSq) continue;

            // Combined score: prefer notes that are both close in pixels and
            // close in time.  Pixel distance dominates so a closer note wins
            // even if its timing is slightly worse, but ties are broken by time.
            float score = distSq + (timeDiff * timeDiff) * 2.0e5f;
            if (score < bestScore) {
                bestScore = score;
                best = PickResult{note.id, static_cast<int>(r), note.type};
            }
        }
    }
    return best;
}

void LanotaRenderer::emitHitFeedback(uint32_t noteId, Judgment judgment) {
    if (!m_renderer) return;
    if (judgment == Judgment::Miss) return;

    glm::vec2 screen;
    if (!projectNoteScreen(noteId, screen)) return;

    glm::vec4 pColor;
    int       pCount = 12;
    switch (judgment) {
        case Judgment::Perfect: pColor = {0.2f, 1.f,   0.3f, 1.f}; pCount = 20; break;
        case Judgment::Good:    pColor = {0.3f, 0.6f,  1.f,  1.f}; pCount = 14; break;
        case Judgment::Bad:     pColor = {1.f,  0.25f, 0.2f, 1.f}; pCount = 8;  break;
        default: return;
    }
    m_renderer->particles().emitBurst(screen, pColor, pCount, 200.f, 8.f, 0.5f);
}

std::optional<uint32_t>
LanotaRenderer::findNoteByAngle(float targetAngle, float angularTol) const {
    const float HIT_WINDOW_SEC = 0.15f;

    std::optional<uint32_t> best;
    float bestScore = std::numeric_limits<float>::max();

    // Wrap helper: shortest signed angular delta in (-π, π].
    auto wrapDelta = [](float a, float b) {
        float d = std::fmod(a - b + PI, TWO_PI);
        if (d < 0.f) d += TWO_PI;
        return d - PI;
    };

    for (const auto& ring : m_rings) {
        for (const auto& note : ring.notes) {
            if (m_hitNotes.count(note.id)) continue;

            float timeDiff = static_cast<float>(note.time - m_songTime);
            if (std::abs(timeDiff) > HIT_WINDOW_SEC) continue;

            const auto* rd = std::get_if<LanotaRingData>(&note.data);
            if (!rd) continue;

            // Compare against the *authored* angle (rd->angle), not the rotated
            // one — the keyboard maps lane → authored angle directly so the
            // mental model is "key N hits the note that was originally placed
            // at lane N's angle", regardless of how far the disk has spun.
            float dAngle = std::abs(wrapDelta(rd->angle, targetAngle));
            if (dAngle > angularTol) continue;

            float score = dAngle * dAngle + (timeDiff * timeDiff) * 100.f;
            if (score < bestScore) {
                bestScore = score;
                best = note.id;
            }
        }
    }
    return best;
}

void LanotaRenderer::showJudgment(int lane, Judgment judgment) {
    if (m_trackCount <= 0) return;
    // Same formula as the lane→angle fallback in onInit so the keyboard maps
    // back to the same notes the fallback synthesized.
    float targetAngle = PI * 0.5f - (static_cast<float>(lane) / m_trackCount) * TWO_PI;

    // Tolerance = half a lane, so lane N only matches notes authored in lane N.
    float angularTol = 0.5f * TWO_PI / static_cast<float>(m_trackCount);
    auto noteId = findNoteByAngle(targetAngle, angularTol);
    if (noteId) {
        markNoteHit(*noteId);
        emitHitFeedback(*noteId, judgment);
        return;
    }

    // No standalone note at this lane/time — this is a Bandori-style hold
    // sample tick passing under the hit line. Emit a burst anyway so the
    // player sees feedback as the hold carves its path.
    if (judgment == Judgment::Miss || !m_renderer) return;
    glm::vec4 pColor;
    int       pCount = 12;
    switch (judgment) {
        case Judgment::Perfect: pColor = {0.2f, 1.f,   0.3f, 1.f}; pCount = 20; break;
        case Judgment::Good:    pColor = {0.3f, 0.6f,  1.f,  1.f}; pCount = 14; break;
        case Judgment::Bad:     pColor = {1.f,  0.25f, 0.2f, 1.f}; pCount = 8;  break;
        default: return;
    }
    // Position the burst on the outer hit ring at the lane's angle — that's
    // where the hold head sits during the hold, on the close (large) disk.
    float sw = static_cast<float>(m_width);
    float sh = static_cast<float>(m_height);
    float ringAngleOffset = m_rings.empty() ? 0.f : m_rings.front().currentAngle;
    float hitR            = m_rings.empty() ? 1.f : m_rings.front().radius;
    float angle = targetAngle + ringAngleOffset;
    glm::vec3 world{cosf(angle) * hitR,
                    sinf(angle) * hitR,
                    0.f};
    glm::vec4 clip = m_perspVP * glm::vec4(world, 1.f);
    if (clip.w <= 0.f) return;
    glm::vec2 screen = w2s(world, m_perspVP, sw, sh);
    m_renderer->particles().emitBurst(screen, pColor, pCount, 200.f, 8.f, 0.5f);
}

// -----------------------------------------------------------------------------
// Lane reachability at a given song time
// -----------------------------------------------------------------------------
// Returns a bitmask (bit i = 1 ↔ lane i reachable).  A lane is reachable
// when its projected outer-ring hit point (at m_rings.front().baseRadius,
// scaled by the sampled disk scale, rotated by the sampled disk rotation,
// centred at the sampled disk center) falls inside the viewport inset by
// a 5% margin on each side.  Used by the editor to gray out unreachable
// lanes on the note-placement timeline without running gameplay.
uint32_t LanotaRenderer::computeEnabledLanesAt(double songTime) const {
    if (m_trackCount <= 0 || m_rings.empty() || m_width == 0 || m_height == 0)
        return 0xFFFFFFFFu;

    const float sw = static_cast<float>(m_width);
    const float sh = static_cast<float>(m_height);
    const float marginX = sw * 0.05f;
    const float marginY = sh * 0.05f;

    // Sample the same transforms the runtime uses.
    const float     scale  = getDiskScale   (songTime, m_scaleEvents);
    const float     rot    = getCurrentRotation(songTime, m_rotationEvents);
    const glm::vec2 center = getDiskCenter  (songTime, m_moveEvents);

    // Rebuild the perspective VP around the sampled disk center without
    // touching mutable state — this is a const query.
    float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    Camera persp = Camera::makePerspective(FOV_Y_DEG, aspect, 0.1f, 200.f);
    persp.lookAt({center.x, center.y, 4.f}, {center.x, center.y, 0.f});
    glm::mat4 vp = persp.viewProjection();

    const float outerR = m_rings.front().baseRadius * scale;

    uint32_t mask = 0;
    for (int lane = 0; lane < m_trackCount && lane < 32; ++lane) {
        float a = PI * 0.5f - (static_cast<float>(lane) / m_trackCount) * TWO_PI + rot;
        glm::vec3 world{std::cos(a) * outerR, std::sin(a) * outerR, 0.f};
        glm::vec4 clip = vp * glm::vec4(world, 1.f);
        if (clip.w <= 0.f) continue;
        glm::vec2 sp = w2s(world, vp, sw, sh);
        if (sp.x < marginX || sp.x > sw - marginX) continue;
        if (sp.y < marginY || sp.y > sh - marginY) continue;
        mask |= (1u << lane);
    }
    return mask;
}
