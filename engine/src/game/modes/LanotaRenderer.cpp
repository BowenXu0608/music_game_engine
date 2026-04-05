#include "LanotaRenderer.h"
#include "renderer/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
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

void LanotaRenderer::onInit(Renderer& renderer, const ChartData& chart, const GameModeConfig*) {
    onResize(renderer.width(), renderer.height());

    std::unordered_map<int, std::vector<NoteEvent>> ringNotes;
    for (auto& note : chart.notes)
        if (auto* rd = std::get_if<LanotaRingData>(&note.data))
            ringNotes[rd->ringIndex].push_back(note);

    for (auto& [idx, notes] : ringNotes) {
        Ring ring{};
        ring.radius        = BASE_RADIUS + idx * RING_SPACING;
        ring.rotationSpeed = 0.4f + idx * 0.15f;
        ring.currentAngle  = 0.f;
        ring.notes         = notes;
        m_rings.push_back(std::move(ring));
    }
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
// Disk center interpolation
// -----------------------------------------------------------------------------
glm::vec2 LanotaRenderer::getDiskCenter(double songTime,
                                        const std::vector<DiskMoveEvent>& events) {
    if (events.empty())                        return {0.f, 0.f};
    if (songTime <= events.front().time)       return events.front().target;
    if (songTime >= events.back().time)        return events.back().target;

    // Find segment: last event with time <= songTime
    auto it = std::upper_bound(events.begin(), events.end(), songTime,
        [](double t, const DiskMoveEvent& e){ return t < e.time; });
    const DiskMoveEvent& next = *it;
    const DiskMoveEvent& cur  = *std::prev(it);

    float t = static_cast<float>((songTime - cur.time) / (next.time - cur.time));
    float e = applyEasing(t, cur.easing);   // cur defines the easing for this segment
    return cur.target + e * (next.target - cur.target);
}

// -----------------------------------------------------------------------------
// Disk rotation interpolation
// -----------------------------------------------------------------------------
// events must be sorted by time (guaranteed by onInit).
// Before the first keyframe  → hold the first angle.
// Between two keyframes      → linear interpolation.
// After the last keyframe    → hold the last angle.
// Empty list                 → return fallbackAngle unchanged (caller accumulates dt).
float LanotaRenderer::getCurrentRotation(double songTime,
                                         const std::vector<RotationEvent>& events,
                                         float fallbackAngle) {
    if (events.empty()) return fallbackAngle;

    // Before first event: clamp to first keyframe angle
    if (songTime <= events.front().time) return events.front().targetAngle;

    // After last event: hold final angle
    if (songTime >= events.back().time)  return events.back().targetAngle;

    // Find the segment: last event with time <= songTime
    // Upper-bound gives the first event strictly greater than songTime.
    auto it = std::upper_bound(events.begin(), events.end(), songTime,
        [](double t, const RotationEvent& e){ return t < e.time; });
    const RotationEvent& next = *it;
    const RotationEvent& cur  = *std::prev(it);

    double segLen = next.time - cur.time;
    float  t      = static_cast<float>((songTime - cur.time) / segLen); // [0, 1]
    return cur.targetAngle + t * (next.targetAngle - cur.targetAngle);
}

void LanotaRenderer::onUpdate(float dt, double songTime) {
    double maxTime = 0.0;
    for (auto& ring : m_rings)
        for (auto& n : ring.notes)
            maxTime = std::max(maxTime, n.time);
    double loopDuration = maxTime + 1.0;
    m_songTime = loopDuration > 0.0 ? fmod(songTime, loopDuration) : songTime;

    for (auto& ring : m_rings) {
        if (ring.rotationEvents.empty()) {
            ring.currentAngle += ring.rotationSpeed * dt;
        } else {
            ring.currentAngle = getCurrentRotation(m_songTime,
                                                   ring.rotationEvents,
                                                   ring.currentAngle);
        }
    }

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

    for (auto& ring : m_rings) {
        // Hit ring circle at Z=0
        std::vector<glm::vec2> pts;
        buildRingPolyline(ring.radius, pts);
        renderer.lines().drawPolyline(pts, 2.f, {0.5f, 0.7f, 1.f, 0.5f}, true);

        for (auto& note : ring.notes) {
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
            float angle = rd->angle + ring.currentAngle;
            // Note travels from deep in the tunnel (large negative Z) toward Z=0.
            // At timeDiff == 0 the note lands on the ring plane.
            float noteZ = -timeDiff * SCROLL_SPEED_Z;

            glm::vec3 notePos{cosf(angle) * ring.radius, sinf(angle) * ring.radius, noteZ};
            glm::vec4 clip = m_perspVP * glm::vec4(notePos, 1.f);
            if (clip.w <= 0.f) continue;

            glm::vec2 screen = w2s(notePos, m_perspVP, sw, sh);
            // Perspective-correct size: appears tiny at depth, full size at Z=0
            float sz = NOTE_WORLD_R * m_proj11y * sh * 0.5f / clip.w;
            if (sz < 2.f) continue;

            float alpha = timeDiff < 0.f
                ? std::max(0.f, 1.f + timeDiff / 0.3f)
                : 0.4f + 0.6f * std::max(0.f, 1.f - timeDiff / APPROACH_SECS);

            glm::vec4 color = (note.type == NoteType::Flick)
                ? glm::vec4{1.f, 0.35f, 0.35f, alpha}
                : glm::vec4{1.f, 0.85f, 0.3f,  alpha};

            // Dark outer halo + bright inner fill
            renderer.quads().drawQuad(
                screen, {sz * 1.3f, sz * 1.3f}, 0.f,
                {0.f, 0.f, 0.f, alpha * 0.5f}, {0.f, 0.f, 1.f, 1.f},
                renderer.whiteView(), renderer.whiteSampler(),
                renderer.context(), renderer.descriptors());
            renderer.quads().drawQuad(
                screen, {sz, sz}, 0.f,
                color, {0.f, 0.f, 1.f, 1.f},
                renderer.whiteView(), renderer.whiteSampler(),
                renderer.context(), renderer.descriptors());
        }
    }
}

void LanotaRenderer::onShutdown(Renderer& renderer) {
    m_rings.clear();
}
