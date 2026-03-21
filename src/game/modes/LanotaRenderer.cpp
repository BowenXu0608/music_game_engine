#include "LanotaRenderer.h"
#include "renderer/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <unordered_map>

static constexpr float TWO_PI = 6.28318530717958f;

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

void LanotaRenderer::onInit(Renderer& renderer, const ChartData& chart) {
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

    float aspect = h > 0 ? static_cast<float>(w) / h : 1.f;
    Camera persp = Camera::makePerspective(FOV_Y_DEG, aspect, 0.1f, 200.f);
    // Camera looks straight down the tunnel axis. Ring hit-plane is at Z=0.
    // Notes approach from negative Z (deep in the tunnel) toward Z=0.
    persp.lookAt({0.f, 0.f, 4.f}, {0.f, 0.f, 0.f});
    m_perspVP = persp.viewProjection();
    m_proj11y = std::abs(persp.projection()[1][1]);

    m_camera = Camera::makeOrtho(0.f, static_cast<float>(w),
                                  static_cast<float>(h), 0.f);
}

void LanotaRenderer::onUpdate(float dt, double songTime) {
    double maxTime = 0.0;
    for (auto& ring : m_rings)
        for (auto& n : ring.notes)
            maxTime = std::max(maxTime, n.time);
    double loopDuration = maxTime + 1.0;
    m_songTime = loopDuration > 0.0 ? fmod(songTime, loopDuration) : songTime;

    for (auto& ring : m_rings)
        ring.currentAngle += ring.rotationSpeed * dt;
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

            glm::vec4 color = {1.f, 0.85f, 0.3f, alpha};

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
