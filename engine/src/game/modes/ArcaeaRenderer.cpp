#include "ArcaeaRenderer.h"
#include "renderer/Renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

void ArcaeaRenderer::onInit(Renderer& renderer, const ChartData& chart, const GameModeConfig*) {
    for (auto& note : chart.notes) {
        if (note.type == NoteType::Arc) {
            ArcMesh am{};
            am.data      = std::get<ArcData>(note.data);
            am.startTime = note.time;
            am.mesh      = buildArcMesh(renderer, am.data);
            m_arcs.push_back(std::move(am));
        } else if (note.type == NoteType::Tap || note.type == NoteType::Flick) {
            m_tapNotes.push_back(note);
        }
    }

    // Build ground plane mesh
    m_groundMesh = buildGroundMesh(renderer);
    m_tapMesh    = buildTapMesh(renderer);

    onResize(renderer.width(), renderer.height());
}

void ArcaeaRenderer::onResize(uint32_t w, uint32_t h) {
    m_width  = w;
    m_height = h;
    float aspect = static_cast<float>(w) / static_cast<float>(h);
    m_camera = Camera::makePerspective(45.f, aspect, 0.1f, 200.f);
    m_camera.lookAt({0.f, 3.f, 10.f}, {0.f, 0.f, 0.f});
}

void ArcaeaRenderer::onUpdate(float dt, double songTime) {
    // Loop
    double maxTime = 0.0;
    for (auto& am : m_arcs)
        maxTime = std::max(maxTime, am.startTime + am.data.duration);
    for (auto& n : m_tapNotes)
        maxTime = std::max(maxTime, n.time);
    double loopDuration = maxTime + 1.0;
    m_songTime = loopDuration > 0.0 ? fmod(songTime, loopDuration) : songTime;
}

glm::vec2 ArcaeaRenderer::evalArc(const ArcData& arc, float t) const {
    auto ease = [](float t, float e) -> float {
        if (e == 0.f) return t;
        return e > 0.f ? 1.f - powf(1.f - t, e + 1.f) : powf(t, -e + 1.f);
    };
    float x = arc.startPos.x + (arc.endPos.x - arc.startPos.x) * ease(t, arc.curveXEase);
    float y = arc.startPos.y + (arc.endPos.y - arc.startPos.y) * ease(t, arc.curveYEase);
    return {x, y};
}

Mesh ArcaeaRenderer::buildArcMesh(Renderer& renderer, const ArcData& arc) {
    std::vector<MeshVertex> verts;
    std::vector<uint32_t>   indices;

    float width = 0.35f;
    for (int i = 0; i <= ARC_SEGMENTS; ++i) {
        float t    = static_cast<float>(i) / ARC_SEGMENTS;
        float z    = -t * arc.duration * SCROLL_SPEED;
        glm::vec2 xy = evalArc(arc, t);

        glm::vec4 color = arc.color == 0
            ? glm::vec4{0.4f, 0.8f, 1.f, 0.9f}
            : glm::vec4{1.f, 0.4f, 0.7f, 0.9f};

        MeshVertex vL{}, vR{};
        vL.pos    = {xy.x - width * 0.5f, xy.y, z};
        vR.pos    = {xy.x + width * 0.5f, xy.y, z};
        vL.normal = vR.normal = {0.f, 1.f, 0.f};
        vL.color  = vR.color  = color;
        vL.uv     = {0.f, t};
        vR.uv     = {1.f, t};
        verts.push_back(vL);
        verts.push_back(vR);

        if (i < ARC_SEGMENTS) {
            uint32_t b = i * 2;
            indices.insert(indices.end(), {b, b+1, b+2, b+1, b+3, b+2});
        }
    }

    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

Mesh ArcaeaRenderer::buildGroundMesh(Renderer& renderer) {
    // Large ground quad in XZ plane
    float w = 3.f, d = 60.f;
    std::vector<MeshVertex> verts = {
        {{-w, GROUND_Y,   0.f}, {0,1,0}, {0,0}, {0.15f, 0.15f, 0.25f, 1.f}},
        {{ w, GROUND_Y,   0.f}, {0,1,0}, {1,0}, {0.15f, 0.15f, 0.25f, 1.f}},
        {{ w, GROUND_Y,  -d  }, {0,1,0}, {1,1}, {0.05f, 0.05f, 0.15f, 1.f}},
        {{-w, GROUND_Y,  -d  }, {0,1,0}, {0,1}, {0.05f, 0.05f, 0.15f, 1.f}},
    };
    std::vector<uint32_t> indices = {0,1,2, 2,3,0};
    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

Mesh ArcaeaRenderer::buildTapMesh(Renderer& renderer) {
    float hw = 0.4f, hh = 0.05f, hd = 0.4f;
    glm::vec4 col = {1.f, 0.9f, 0.5f, 1.f};
    std::vector<MeshVertex> verts = {
        {{-hw, GROUND_Y+hh, -hd}, {0,1,0}, {0,0}, col},
        {{ hw, GROUND_Y+hh, -hd}, {0,1,0}, {1,0}, col},
        {{ hw, GROUND_Y+hh,  hd}, {0,1,0}, {1,1}, col},
        {{-hw, GROUND_Y+hh,  hd}, {0,1,0}, {0,1}, col},
    };
    std::vector<uint32_t> indices = {0,1,2, 2,3,0};
    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

void ArcaeaRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    // Ground
    renderer.meshes().drawMesh(m_groundMesh, glm::mat4(1.f), {1,1,1,1});

    // Lane divider lines on ground
    for (int i = -1; i <= 1; ++i) {
        float x = i * 1.5f;
        // Draw as thin mesh strip — use lines projected in 3D via quad batch is not ideal,
        // so draw as a thin ground mesh via line batch is 2D only.
        // Skip for now — ground color provides enough context.
    }

    // Tap notes
    for (auto& note : m_tapNotes) {
        float z = static_cast<float>(note.time - m_songTime) * SCROLL_SPEED;
        if (z < -2.f || z > 30.f) continue;
        float laneX = 0.f;
        if (auto* tap = std::get_if<TapData>(&note.data))     laneX = tap->laneX;
        else if (auto* fl = std::get_if<FlickData>(&note.data)) laneX = fl->laneX;
        else continue;

        // Map laneX (0-5) to world X (-2.5 to 2.5)
        float wx = (laneX / 4.f - 0.5f) * 4.f;
        glm::mat4 model = glm::translate(glm::mat4(1.f), {wx, 0.f, -z});
        glm::vec4 tint = (note.type == NoteType::Flick)
            ? glm::vec4{1.f, 0.35f, 0.35f, 1.f}
            : glm::vec4{1.f, 1.f,   1.f,   1.f};
        renderer.meshes().drawMesh(m_tapMesh, model, tint);
    }

    // Arc ribbons
    for (auto& am : m_arcs) {
        float zOffset = static_cast<float>(am.startTime - m_songTime) * SCROLL_SPEED;
        if (zOffset > 30.f || zOffset < -am.data.duration * SCROLL_SPEED - 2.f) continue;
        glm::mat4 model = glm::translate(glm::mat4(1.f), {0.f, 0.f, -zOffset});
        glm::vec4 tint  = am.data.isVoid ? glm::vec4{1,1,1,0.4f} : glm::vec4{1,1,1,1};
        renderer.meshes().drawMesh(am.mesh, model, tint);
    }
}

void ArcaeaRenderer::onShutdown(Renderer& renderer) {
    renderer.meshes().destroyMesh(renderer.buffers(), m_groundMesh);
    renderer.meshes().destroyMesh(renderer.buffers(), m_tapMesh);
    for (auto& am : m_arcs)
        renderer.meshes().destroyMesh(renderer.buffers(), am.mesh);
    m_arcs.clear();
    m_tapNotes.clear();
}
