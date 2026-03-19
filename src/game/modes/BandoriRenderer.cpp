#include "BandoriRenderer.h"
#include "renderer/Renderer.h"
#include <glm/glm.hpp>
#include <cmath>

static constexpr int   BANDORI_LANES     = 7;
static constexpr float BANDORI_NOTE_SIZE = 60.f;

void BandoriRenderer::onInit(Renderer& renderer, const ChartData& chart) {
    m_notes = chart.notes;
    onResize(renderer.width(), renderer.height());
}

void BandoriRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;
    m_camera = Camera::makeOrtho(0.f, static_cast<float>(w),
                                  static_cast<float>(h), 0.f);
    m_hitZoneY   = h * 0.15f;
    m_laneWidth  = static_cast<float>(w) / BANDORI_LANES;
    m_laneStartX = m_laneWidth * 0.5f;
}

void BandoriRenderer::onUpdate(float dt, double songTime) {
    double maxTime = 0.0;
    for (auto& n : m_notes) maxTime = std::max(maxTime, n.time);
    double loopDuration = maxTime + 1.0;
    double prevSongTime = m_songTime;
    m_songTime = loopDuration > 0.0 ? fmod(songTime, loopDuration) : songTime;
    // Reset hit notes on loop
    if (m_songTime < prevSongTime) m_hitNotes.clear();
}

void BandoriRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    for (auto& note : m_notes) {
        float laneX = 0.f;
        if (auto* tap = std::get_if<TapData>(&note.data))
            laneX = tap->laneX;
        else if (auto* hold = std::get_if<HoldData>(&note.data))
            laneX = hold->laneX;
        else if (auto* flick = std::get_if<FlickData>(&note.data))
            laneX = flick->laneX;
        else
            continue;

        float noteY = m_hitZoneY + static_cast<float>(note.time - m_songTime) * m_scrollSpeed;
        if (noteY < -BANDORI_NOTE_SIZE || noteY > m_height + BANDORI_NOTE_SIZE) continue;

        float x = m_laneStartX + laneX * m_laneWidth;
        glm::vec4 color = {1.f, 0.8f, 0.2f, 1.f};
        if (note.type == NoteType::Hold)  color = {0.2f, 0.8f, 1.f, 1.f};
        if (note.type == NoteType::Flick) color = {1.f, 0.3f, 0.3f, 1.f};

        // Emit hit effect when note reaches hit zone
        double timeDiff = note.time - m_songTime;
        if (timeDiff > -0.05 && timeDiff < 0.05 && m_hitNotes.find(note.id) == m_hitNotes.end()) {
            m_hitNotes.insert(note.id);
            renderer.particles().emitBurst({x, m_hitZoneY}, color, 16, 250.f, 10.f, 0.6f);
        }

        renderer.quads().drawQuad(
            {x, noteY}, {BANDORI_NOTE_SIZE, BANDORI_NOTE_SIZE * 0.4f}, 0.f,
            color, {0.f, 0.f, 1.f, 1.f},
            renderer.whiteView(), renderer.whiteSampler(),
            renderer.context(), renderer.descriptors());
    }

    // Lane dividers
    for (int i = 1; i < BANDORI_LANES; ++i) {
        float x = m_laneWidth * i;
        renderer.lines().drawLine({x, 0.f}, {x, static_cast<float>(m_height)},
                                   1.5f, {1.f, 1.f, 1.f, 0.2f});
    }

    // Hit zone line
    renderer.lines().drawLine({0.f, m_hitZoneY},
                               {static_cast<float>(m_width), m_hitZoneY},
                               2.f, {1.f, 1.f, 0.f, 0.8f});
}

void BandoriRenderer::onShutdown(Renderer& renderer) {}
