#include "CytusRenderer.h"
#include "renderer/Renderer.h"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>

static constexpr float NOTE_RADIUS    = 36.f;
static constexpr float NOTE_SIZE      = NOTE_RADIUS * 2.f;
static constexpr float SCAN_THICKNESS = 4.f;

void CytusRenderer::onInit(Renderer& renderer, const ChartData& chart) {
    float w = static_cast<float>(renderer.width());
    float h = static_cast<float>(renderer.height());

    // laneX is 0-6, map to normalized X across 10%-90% of screen width
    // Distribute notes across 5 X columns
    static const float colsX[] = { 0.15f, 0.3f, 0.5f, 0.7f, 0.85f };

    for (size_t i = 0; i < chart.notes.size(); ++i) {
        auto& n = chart.notes[i];
        float laneX   = 0.f;
        float duration = 0.f;
        bool  isHold  = false;

        if (auto* tap = std::get_if<TapData>(&n.data))
            laneX = tap->laneX;
        else if (auto* hold = std::get_if<HoldData>(&n.data)) {
            laneX    = hold->laneX;
            duration = hold->duration;
            isHold   = true;
        } else if (auto* flick = std::get_if<FlickData>(&n.data))
            laneX = flick->laneX;
        else
            continue;

        CytusNote cn{};
        cn.x            = (laneX / 6.f) * w * 0.8f + w * 0.1f;
        cn.y            = colsX[i % 5] * h;
        cn.time         = n.time;
        cn.isHold       = isHold;
        cn.holdDuration = duration;
        m_notes.push_back(cn);
    }

    onResize(renderer.width(), renderer.height());
}

void CytusRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;
    m_camera = Camera::makeOrtho(0.f, static_cast<float>(w),
                                  static_cast<float>(h), 0.f);
}

void CytusRenderer::onUpdate(float dt, double songTime) {
    double maxTime = 0.0;
    for (auto& n : m_notes) maxTime = std::max(maxTime, n.time);
    double loopDuration = maxTime + 1.0;
    m_songTime = loopDuration > 0.0 ? fmod(songTime, loopDuration) : songTime;

    float h    = static_cast<float>(m_height);
    int   page = static_cast<int>(m_songTime / m_pageDuration);
    float t    = static_cast<float>(fmod(m_songTime, m_pageDuration)) / m_pageDuration;
    // Even pages: bottom→top (h→0), odd pages: top→bottom (0→h)
    m_scanLineY = (page % 2 == 0) ? (1.f - t) * h : t * h;
}

void CytusRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    float w = static_cast<float>(m_width);
    float h = static_cast<float>(m_height);

    // Scan line glow
    renderer.lines().drawLine({0.f, m_scanLineY}, {w, m_scanLineY},
                               24.f, {1.f, 1.f, 1.f, 0.07f});
    // Scan line core
    renderer.lines().drawLine({0.f, m_scanLineY}, {w, m_scanLineY},
                               SCAN_THICKNESS, {1.f, 1.f, 1.f, 0.9f});

    for (auto& note : m_notes) {
        double dt = note.time - m_songTime;

        if (dt > m_approachSecs || dt < -0.3) continue;

        float alpha = 1.f;
        if (dt > 0.f)
            alpha = 1.f - static_cast<float>(dt / m_approachSecs) * 0.3f;
        else
            alpha = std::max(0.f, 1.f + static_cast<float>(dt) / 0.3f);

        // Hold connector line from note Y to scan line Y
        if (note.isHold) {
            renderer.lines().drawLine(
                {note.x, note.y}, {note.x, m_scanLineY},
                NOTE_RADIUS * 0.5f, {0.4f, 0.8f, 1.f, alpha * 0.5f});
        }

        glm::vec4 color = note.isHold
            ? glm::vec4{0.3f, 0.7f, 1.f, alpha}
            : glm::vec4{1.f, 1.f, 1.f, alpha};

        // Outer ring
        renderer.quads().drawQuad(
            {note.x, note.y}, {NOTE_SIZE + 8.f, NOTE_SIZE + 8.f}, 0.f,
            {0.f, 0.f, 0.f, alpha * 0.6f}, {0.f, 0.f, 1.f, 1.f},
            renderer.whiteView(), renderer.whiteSampler(),
            renderer.context(), renderer.descriptors());

        // Inner fill
        renderer.quads().drawQuad(
            {note.x, note.y}, {NOTE_SIZE, NOTE_SIZE}, 0.f,
            color, {0.f, 0.f, 1.f, 1.f},
            renderer.whiteView(), renderer.whiteSampler(),
            renderer.context(), renderer.descriptors());
    }
}

void CytusRenderer::onShutdown(Renderer& renderer) {}
