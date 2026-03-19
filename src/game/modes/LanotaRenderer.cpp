#include "LanotaRenderer.h"
#include "renderer/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <unordered_map>

static constexpr float TWO_PI = 6.28318530717958f;

void LanotaRenderer::onInit(Renderer& renderer, const ChartData& chart) {
    onResize(renderer.width(), renderer.height());

    std::unordered_map<int, std::vector<NoteEvent>> ringNotes;
    for (auto& note : chart.notes)
        if (auto* rd = std::get_if<LanotaRingData>(&note.data))
            ringNotes[rd->ringIndex].push_back(note);

    for (auto& [idx, notes] : ringNotes) {
        Ring ring{};
        ring.nodeID        = m_scene.createNode();
        ring.radius        = BASE_RADIUS + idx * 80.f;
        ring.rotationSpeed = 0.4f + idx * 0.15f;
        ring.currentAngle  = 0.f;
        ring.notes         = notes;

        for (auto& note : notes) {
            NodeID noteNode = m_scene.createNode(ring.nodeID);
            if (auto* rd = std::get_if<LanotaRingData>(&note.data)) {
                SceneNode* n = m_scene.get(noteNode);
                if (n) n->localTransform.position = {
                    cosf(rd->angle) * ring.radius,
                    sinf(rd->angle) * ring.radius, 0.f};
            }
            m_noteNodes[note.id] = noteNode;
        }
        m_rings.push_back(std::move(ring));
    }
}

void LanotaRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;
    float hw = static_cast<float>(w) * 0.5f;
    float hh = static_cast<float>(h) * 0.5f;
    m_camera = Camera::makeOrtho(-hw, hw, hh, -hh);
}

void LanotaRenderer::onUpdate(float dt, double songTime) {
    // Loop
    double maxTime = 0.0;
    for (auto& ring : m_rings)
        for (auto& n : ring.notes)
            maxTime = std::max(maxTime, n.time);
    double loopDuration = maxTime + 1.0;
    m_songTime = loopDuration > 0.0 ? fmod(songTime, loopDuration) : songTime;

    for (auto& ring : m_rings) {
        ring.currentAngle += ring.rotationSpeed * dt;
        SceneNode* node = m_scene.get(ring.nodeID);
        if (node) {
            node->localTransform.setRotationZ(ring.currentAngle);
            m_scene.markDirty(ring.nodeID);
        }
    }
    m_scene.update();
}

void LanotaRenderer::buildRingPolyline(float radius, std::vector<glm::vec2>& out) const {
    out.resize(RING_SEGMENTS + 1);
    for (int i = 0; i <= RING_SEGMENTS; ++i) {
        float a = TWO_PI * i / RING_SEGMENTS;
        out[i]  = {cosf(a) * radius, sinf(a) * radius};
    }
}

void LanotaRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    static constexpr float APPROACH_SECS = 2.f;

    for (auto& ring : m_rings) {
        // Draw ring
        std::vector<glm::vec2> pts;
        buildRingPolyline(ring.radius, pts);
        renderer.lines().drawPolyline(pts, 2.f, {0.5f, 0.7f, 1.f, 0.5f}, true);

        for (auto& note : ring.notes) {
            float timeDiff = static_cast<float>(note.time - m_songTime);
            if (timeDiff < -0.3f || timeDiff > APPROACH_SECS) continue;

            auto it = m_noteNodes.find(note.id);
            if (it == m_noteNodes.end()) continue;

            glm::mat4 world = m_scene.worldMatrix(it->second);
            glm::vec2 wpos  = {world[3][0], world[3][1]};

            // Note approaches from center outward: scale radius from 0 up to ring.radius
            float t = 1.f - std::max(0.f, timeDiff / APPROACH_SECS);
            float approachRadius = ring.radius * t;

            // Recompute world pos with approach radius
            if (auto* rd = std::get_if<LanotaRingData>(&note.data)) {
                float angle = rd->angle + ring.currentAngle;
                wpos = {cosf(angle) * approachRadius, sinf(angle) * approachRadius};
            }

            float alpha = (timeDiff < 0.f)
                ? std::max(0.f, 1.f + timeDiff / 0.3f)
                : 0.4f + 0.6f * t;

            glm::vec4 color = {1.f, 0.85f, 0.3f, alpha};

            // Outer ring
            renderer.quads().drawQuad(
                wpos, {32.f, 32.f}, 0.f,
                {0.f, 0.f, 0.f, alpha * 0.5f}, {0.f, 0.f, 1.f, 1.f},
                renderer.whiteView(), renderer.whiteSampler(),
                renderer.context(), renderer.descriptors());
            // Inner fill
            renderer.quads().drawQuad(
                wpos, {24.f, 24.f}, 0.f,
                color, {0.f, 0.f, 1.f, 1.f},
                renderer.whiteView(), renderer.whiteSampler(),
                renderer.context(), renderer.descriptors());
        }
    }
}

void LanotaRenderer::onShutdown(Renderer& renderer) {
    m_rings.clear();
    m_noteNodes.clear();
}
