#include "BandoriRenderer.h"
#include "renderer/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

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

void BandoriRenderer::onInit(Renderer& renderer, const ChartData& chart) {
    m_notes = chart.notes;
    onResize(renderer.width(), renderer.height());
}

void BandoriRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;

    float aspect = h > 0 ? static_cast<float>(w) / h : 1.f;
    Camera persp = Camera::makePerspective(FOV_Y_DEG, aspect, 0.5f, 300.f);
    // Camera sits slightly above and behind the hit zone, angled down the highway.
    persp.lookAt({0.f, 1.8f, 3.5f}, {0.f, 0.f, -20.f});
    m_perspVP  = persp.viewProjection();
    m_proj11y  = std::abs(persp.projection()[1][1]);

    // Batchers still use a flat screen-space ortho camera (y=0 bottom, y=h top).
    m_camera = Camera::makeOrtho(0.f, static_cast<float>(w),
                                  static_cast<float>(h), 0.f);
}

void BandoriRenderer::onUpdate(float dt, double songTime) {
    double maxTime = 0.0;
    for (auto& n : m_notes) maxTime = std::max(maxTime, n.time);
    double loopDuration = maxTime + 1.0;
    double prevSongTime = m_songTime;
    m_songTime = loopDuration > 0.0 ? fmod(songTime, loopDuration) : songTime;
    if (m_songTime < prevSongTime) m_hitNotes.clear();
}

void BandoriRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    float sw = static_cast<float>(m_width);
    float sh = static_cast<float>(m_height);

    // Lane dividers converge to vanishing point — one edge line per boundary
    for (int i = 0; i <= LANE_COUNT; ++i) {
        float wx   = (i - LANE_COUNT * 0.5f) * LANE_SPACING;
        glm::vec2 near = w2s({wx, 0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        glm::vec2 far  = w2s({wx, 0.f, APPROACH_Z}, m_perspVP, sw, sh);
        renderer.lines().drawLine(near, far, 1.5f, {1.f, 1.f, 1.f, 0.2f});
    }

    // Hit zone line across all lanes
    {
        float leftX  = -(LANE_COUNT * 0.5f) * LANE_SPACING;
        float rightX =  (LANE_COUNT * 0.5f) * LANE_SPACING;
        glm::vec2 l = w2s({leftX,  0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        glm::vec2 r = w2s({rightX, 0.f, HIT_ZONE_Z}, m_perspVP, sw, sh);
        renderer.lines().drawLine(l, r, 2.f, {1.f, 1.f, 0.f, 0.8f});
    }

    // Notes
    for (auto& note : m_notes) {
        float laneX = 0.f;
        if      (auto* tap   = std::get_if<TapData>  (&note.data)) laneX = tap->laneX;
        else if (auto* hold  = std::get_if<HoldData> (&note.data)) laneX = hold->laneX;
        else if (auto* flick = std::get_if<FlickData>(&note.data)) laneX = flick->laneX;
        else continue;

        float timeDiff = static_cast<float>(note.time - m_songTime);
        float noteZ    = -timeDiff * SCROLL_SPEED;
        // Only render notes on the visible highway section
        if (noteZ > 1.f || noteZ < APPROACH_Z - 2.f) continue;

        float worldX = (laneX - (LANE_COUNT - 1) * 0.5f) * LANE_SPACING;
        glm::vec3 worldPos{worldX, 0.f, noteZ};
        glm::vec4 clip = m_perspVP * glm::vec4(worldPos, 1.f);
        if (clip.w <= 0.f) continue;

        glm::vec2 screen = w2s(worldPos, m_perspVP, sw, sh);
        float sz = pxSize(NOTE_WORLD_W, clip.w, m_proj11y, sh);
        if (sz < 2.f) continue;

        glm::vec4 color = {1.f, 0.8f, 0.2f, 1.f};
        if (note.type == NoteType::Hold)  color = {0.2f, 0.8f, 1.f, 1.f};
        if (note.type == NoteType::Flick) color = {1.f, 0.3f, 0.3f, 1.f};

        if (timeDiff > -0.05f && timeDiff < 0.05f && !m_hitNotes.count(note.id)) {
            m_hitNotes.insert(note.id);
            renderer.particles().emitBurst(screen, color, 16, 250.f, 10.f, 0.6f);
        }

        renderer.quads().drawQuad(
            screen, {sz, sz * 0.4f}, 0.f,
            color, {0.f, 0.f, 1.f, 1.f},
            renderer.whiteView(), renderer.whiteSampler(),
            renderer.context(), renderer.descriptors());
    }
}

void BandoriRenderer::onShutdown(Renderer& renderer) {}
