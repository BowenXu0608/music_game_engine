#include "PhigrosRenderer.h"
#include "renderer/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

void PhigrosRenderer::onInit(Renderer& renderer, const ChartData& chart, const GameModeConfig*) {
    m_lineEvents = chart.judgmentLines;
    onResize(renderer.width(), renderer.height());

    for (size_t i = 0; i < m_lineEvents.size(); ++i) {
        LineState ls{};
        ls.nodeID   = m_scene.createNode();
        ls.notes    = m_lineEvents[i].attachedNotes;
        ls.position = m_lineEvents[i].position;
        ls.rotation = m_lineEvents[i].rotation;
        ls.speed    = m_lineEvents[i].speed > 0.f ? m_lineEvents[i].speed : 300.f;
        m_lines.push_back(ls);

        for (auto& note : ls.notes) {
            NodeID noteNode = m_scene.createNode(ls.nodeID);
            m_noteNodes[note.id] = noteNode;
        }
    }
}

void PhigrosRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;
    float hw = static_cast<float>(w) * 0.5f;
    float hh = static_cast<float>(h) * 0.5f;
    // Centered ortho: origin at screen center, Y increases upward visually
    // Using same convention as confirmed: makeOrtho(l, r, h, 0) → Y=0 at bottom visually
    // For centered: left=-hw, right=hw, bottom=hh (maps to NDC-1=bottom), top=-hh (maps to NDC+1=top)
    m_camera = Camera::makeOrtho(-hw, hw, hh, -hh);
}

void PhigrosRenderer::onUpdate(float dt, double songTime) {
    m_songTime = songTime;

    updateLineKeyframes(m_songTime);

    for (auto& ls : m_lines) {
        for (auto& note : ls.notes) {
            auto it = m_noteNodes.find(note.id);
            if (it == m_noteNodes.end()) continue;
            SceneNode* node = m_scene.get(it->second);
            if (!node) continue;

            float posOnLine = 0.f;
            if (auto* pd = std::get_if<PhigrosNoteData>(&note.data))
                posOnLine = pd->posOnLine;

            // Note sits on the line at posOnLine (local X), approaches from above (local Y)
            float dist = static_cast<float>(note.time - m_songTime) * ls.speed;
            node->localTransform.position = {posOnLine, dist, 0.f};
            m_scene.markDirty(it->second);
        }
    }

    m_scene.update();
}

void PhigrosRenderer::updateLineKeyframes(double songTime) {
    for (size_t i = 0; i < m_lineEvents.size() && i < m_lines.size(); ++i) {
        auto& events = m_lineEvents[i];

        // Use the single event's values as the keyframe (simple: one event per line)
        // For multi-keyframe support, find last event at or before songTime
        glm::vec2 pos = events.position;
        float     rot = events.rotation;
        float     spd = events.speed > 0.f ? events.speed : 300.f;

        // Animate: rotate the line slowly if no keyframes
        rot += static_cast<float>(songTime) * 0.3f;

        m_lines[i].position = pos;
        m_lines[i].rotation = rot;
        m_lines[i].speed    = spd;

        SceneNode* node = m_scene.get(m_lines[i].nodeID);
        if (node) {
            node->localTransform.position = {pos.x, pos.y, 0.f};
            node->localTransform.setRotationZ(rot);
            m_scene.markDirty(m_lines[i].nodeID);
        }
    }
}

void PhigrosRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    float hw = static_cast<float>(m_width)  * 0.5f;
    float hh = static_cast<float>(m_height) * 0.5f;

    for (auto& ls : m_lines) {
        // Draw judgment line
        float c   = cosf(ls.rotation), s = sinf(ls.rotation);
        glm::vec2 dir = {c, s};
        float     len = hw * 1.5f;
        glm::vec2 p0  = ls.position - dir * len;
        glm::vec2 p1  = ls.position + dir * len;

        // Glow
        renderer.lines().drawLine(p0, p1, 12.f, {1.f, 1.f, 1.f, 0.15f});
        // Core
        renderer.lines().drawLine(p0, p1, 3.f, {1.f, 1.f, 1.f, 0.9f});

        // Draw notes
        for (auto& note : ls.notes) {
            auto it = m_noteNodes.find(note.id);
            if (it == m_noteNodes.end()) continue;

            glm::mat4 world = m_scene.worldMatrix(it->second);
            glm::vec2 wpos  = {world[3][0], world[3][1]};

            // Cull off-screen
            if (wpos.x < -hw - 60.f || wpos.x > hw + 60.f) continue;
            if (wpos.y < -hh - 60.f || wpos.y > hh + 60.f) continue;

            // Only show notes within approach window
            double dt = note.time - m_songTime;
            if (dt > 2.0 || dt < -0.3) continue;

            float alpha = (dt < 0.0)
                ? std::max(0.f, 1.f + static_cast<float>(dt) / 0.3f)
                : 1.f;

            glm::vec4 color = {1.f, 0.9f, 0.3f, alpha};
            if (note.type == NoteType::Hold)  color = {0.3f, 0.8f, 1.f, alpha};
            if (note.type == NoteType::Flick) color = {1.f, 0.35f, 0.35f, alpha};

            renderer.quads().drawQuad(
                wpos, {50.f, 18.f}, ls.rotation,
                color, {0.f, 0.f, 1.f, 1.f},
                renderer.whiteView(), renderer.whiteSampler(),
                renderer.context(), renderer.descriptors());
        }
    }
}

void PhigrosRenderer::onShutdown(Renderer& renderer) {
    m_lines.clear();
    m_noteNodes.clear();
}

std::vector<PhigrosRenderer::LineInfo> PhigrosRenderer::getActiveLines() const {
    std::vector<LineInfo> result;
    result.reserve(m_lines.size());
    float hw = static_cast<float>(m_width)  * 0.5f;
    float hh = static_cast<float>(m_height) * 0.5f;
    for (const auto& ls : m_lines) {
        // Convert from centered-ortho space to screen pixels (origin top-left)
        glm::vec2 screenOrigin{ls.position.x + hw, hh - ls.position.y};
        result.push_back({screenOrigin, ls.rotation});
    }
    return result;
}
