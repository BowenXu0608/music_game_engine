#include "ArcaeaRenderer.h"
#include "renderer/Renderer.h"
#include "ui/ProjectHub.h"   // GameModeConfig definition
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>
#include <algorithm>

void ArcaeaRenderer::onInit(Renderer& renderer, const ChartData& chart,
                            const GameModeConfig* config) {
    m_renderer = &renderer;
    if (config && config->trackCount > 0) m_laneCount = config->trackCount;
    if (config)                            m_skyHeight = config->skyHeight;

    for (auto& note : chart.notes) {
        if (note.type == NoteType::Arc) {
            ArcMesh am{};
            am.data      = std::get<ArcData>(note.data);
            am.startTime = note.time;
            am.mesh      = buildDynamicArcMesh(renderer);
            writeArcVertices(am, 0.f);
            m_arcs.push_back(std::move(am));
        } else if (note.type == NoteType::Tap || note.type == NoteType::Flick) {
            m_tapNotes.push_back(note);
        } else if (note.type == NoteType::ArcTap) {
            m_arcTaps.push_back(note);
        }
    }

    // Auto-expand lane count if the chart uses higher indices than the
    // configured trackCount (mirrors BandoriRenderer — charts may be authored
    // before trackCount is set in the config).
    for (auto& n : m_tapNotes) {
        int lane = -1;
        if (auto* tap = std::get_if<TapData>(&n.data))         lane = static_cast<int>(std::lround(tap->laneX));
        else if (auto* fl = std::get_if<FlickData>(&n.data))   lane = static_cast<int>(std::lround(fl->laneX));
        if (lane >= m_laneCount) m_laneCount = lane + 1;
    }

    // Build ground plane mesh
    m_groundMesh = buildGroundMesh(renderer);
    m_tapMesh    = buildTapMesh(renderer);
    m_gateMesh   = buildGateMesh(renderer, m_skyHeight);
    m_arcTapMesh = buildArcTapMesh(renderer);

    // Pre-compute a (time → sky world-position) table for sky-only events:
    // arctaps and arc start/end boundaries. Engine clamps arc/arctap lane
    // to 0, so at showJudgment time `lane==0` is the signal to consult this
    // table. Ground taps/flicks and hold sample ticks carry their real lane
    // and are positioned directly from lane math — they must NOT be in this
    // table, or a nearby hold-tick would snap to an arctap's sky Y.
    for (const auto& note : chart.notes) {
        if (note.type == NoteType::ArcTap) {
            if (auto* tap = std::get_if<TapData>(&note.data)) {
                float wx = (tap->laneX * 2.f - 1.f) * LANE_HALF_WIDTH;
                float wy = GROUND_Y + tap->scanY * m_skyHeight;
                m_hitEvents.push_back({note.time, glm::vec2{wx, wy}});
            }
        } else if (note.type == NoteType::Arc) {
            if (auto* arc = std::get_if<ArcData>(&note.data)) {
                if (arc->isVoid) continue;  // invisible arctap carriers
                m_hitEvents.push_back({note.time,                 evalArc(*arc, 0.f)});
                m_hitEvents.push_back({note.time + arc->duration, evalArc(*arc, 1.f)});
            }
        }
    }
    std::sort(m_hitEvents.begin(), m_hitEvents.end(),
              [](const HitEvent& a, const HitEvent& b) { return a.time < b.time; });

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
    // No looping: songTime advances monotonically. Notes whose time has passed
    // are culled by the per-type checks below (tap: z < -2, arc: songRel >=
    // duration), so they don't re-trigger when the song ends.
    m_songTime = songTime;

    // Rebuild each arc so its head vertex sits exactly on the judgment line.
    // tClip is the continuous float parameter corresponding to the current
    // audio time, so the trailing edge recedes smoothly frame-to-frame with
    // no segment-level popping.
    for (auto& am : m_arcs) {
        double songRel = m_songTime - am.startTime;
        float tClip;
        if (am.data.duration <= 0.0)          tClip = 0.f;
        else if (songRel <= 0.0)              tClip = 0.f;
        else if (songRel >= am.data.duration) tClip = 1.f;
        else                                   tClip = static_cast<float>(songRel / am.data.duration);
        writeArcVertices(am, tClip);
    }
}

glm::vec2 ArcaeaRenderer::evalArc(const ArcData& arc, float t) const {
    auto ease = [](float t, float e) -> float {
        if (e == 0.f) return t;
        return e > 0.f ? 1.f - powf(1.f - t, e + 1.f) : powf(t, -e + 1.f);
    };
    // Chart arcs author startX/endX and startY/endY in normalized [0,1] space,
    // where x=0 is the left lane edge, x=1 is the right lane edge, y=0 is the
    // ground judgment line and y=1 is the sky judgment line. Map to the same
    // world coords used by the gate so the arc head lands exactly on the gate
    // plane.
    float nx = arc.startPos.x + (arc.endPos.x - arc.startPos.x) * ease(t, arc.curveXEase);
    float ny = arc.startPos.y + (arc.endPos.y - arc.startPos.y) * ease(t, arc.curveYEase);
    float wx = (nx * 2.f - 1.f) * LANE_HALF_WIDTH;
    float wy = GROUND_Y + ny * m_skyHeight;
    return {wx, wy};
}

Mesh ArcaeaRenderer::buildDynamicArcMesh(Renderer& renderer) {
    // Vertex buffer is host-mapped so we can memcpy a new shape every frame
    // without going through staging (staging uses vkQueueWaitIdle per upload).
    const size_t vertCount = static_cast<size_t>(ARC_SEGMENTS + 1) * 2;
    const size_t idxCount  = static_cast<size_t>(ARC_SEGMENTS) * 6;

    Mesh mesh;
    mesh.indexCount   = static_cast<uint32_t>(idxCount);
    mesh.vertexBuffer = renderer.buffers().createDynamicBuffer(
        sizeof(MeshVertex) * vertCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    mesh.indexBuffer  = renderer.buffers().createDeviceBuffer(
        sizeof(uint32_t) * idxCount, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    std::vector<uint32_t> indices;
    indices.reserve(idxCount);
    for (int i = 0; i < ARC_SEGMENTS; ++i) {
        uint32_t b = static_cast<uint32_t>(i) * 2;
        indices.insert(indices.end(), {b, b+1, b+2, b+1, b+3, b+2});
    }
    renderer.buffers().uploadToBuffer(renderer.context(), mesh.indexBuffer,
                                      indices.data(), sizeof(uint32_t) * idxCount);
    return mesh;
}

void ArcaeaRenderer::writeArcVertices(ArcMesh& am, float tClip) {
    const float width = 0.35f;
    const glm::vec4 color = am.data.color == 0
        ? glm::vec4{0.4f, 0.8f, 1.f, 0.9f}
        : glm::vec4{1.f, 0.4f, 0.7f, 0.9f};

    std::vector<MeshVertex> verts;
    verts.reserve((ARC_SEGMENTS + 1) * 2);
    for (int i = 0; i <= ARC_SEGMENTS; ++i) {
        float frac = static_cast<float>(i) / ARC_SEGMENTS;
        float t    = tClip + (1.f - tClip) * frac;
        float z    = -t * static_cast<float>(am.data.duration) * SCROLL_SPEED;
        glm::vec2 xy = evalArc(am.data, t);

        MeshVertex vL{}, vR{};
        vL.pos    = {xy.x - width * 0.5f, xy.y, z};
        vR.pos    = {xy.x + width * 0.5f, xy.y, z};
        // Camera-facing normal: mesh.frag's rim-glow is 1 - |dot(n, {0,0,1})|,
        // so {0,0,1} gives rim=0 and the base vertex color passes through
        // unmodified. A {0,1,0} normal would push rim=1 and triple the color,
        // saturating arcs (and the taps they're near) to pure white.
        vL.normal = vR.normal = {0.f, 0.f, 1.f};
        vL.color  = vR.color  = color;
        vL.uv     = {0.f, t};
        vR.uv     = {1.f, t};
        verts.push_back(vL);
        verts.push_back(vR);
    }

    std::memcpy(am.mesh.vertexBuffer.mapped, verts.data(),
                sizeof(MeshVertex) * verts.size());
}

Mesh ArcaeaRenderer::buildGroundMesh(Renderer& renderer) {
    // Ground quad in XZ plane, spanning the full lane width at the near edge
    // and the full lane depth from JUDGMENT_Z (near) to LANE_FAR_Z (far).
    const float w = LANE_HALF_WIDTH;
    std::vector<MeshVertex> verts = {
        {{-w, GROUND_Y, JUDGMENT_Z}, {0,1,0}, {0,0}, {0.15f, 0.15f, 0.25f, 1.f}},
        {{ w, GROUND_Y, JUDGMENT_Z}, {0,1,0}, {1,0}, {0.15f, 0.15f, 0.25f, 1.f}},
        {{ w, GROUND_Y, LANE_FAR_Z}, {0,1,0}, {1,1}, {0.05f, 0.05f, 0.15f, 1.f}},
        {{-w, GROUND_Y, LANE_FAR_Z}, {0,1,0}, {0,1}, {0.05f, 0.05f, 0.15f, 1.f}},
    };
    std::vector<uint32_t> indices = {0,1,2, 2,3,0};
    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

Mesh ArcaeaRenderer::buildGateMesh(Renderer& renderer, float skyHeight) {
    // The gate frames the NEAR (front) edge of the ground lane. Its four bottom
    // corners coincide exactly with the ground mesh's near-edge corners:
    //     bottom-left  = (-LANE_HALF_WIDTH, GROUND_Y, JUDGMENT_Z)
    //     bottom-right = (+LANE_HALF_WIDTH, GROUND_Y, JUDGMENT_Z)
    // The top corners offset upward by skyHeight. All 4 bars live on z =
    // JUDGMENT_Z so they are coplanar with the lane's near edge.
    //
    // Visual hierarchy per Arcaea convention:
    //   * bottom bar (ground judgment line) — brightest + thickest
    //   * sky bar and vertical posts         — dimmer reference lines
    const float xL    = -LANE_HALF_WIDTH;
    const float xR    =  LANE_HALF_WIDTH;
    const float y0    = GROUND_Y;
    const float y1    = GROUND_Y + skyHeight;
    const float z     = JUDGMENT_Z;
    const float tMain = 0.08f;  // bottom bar thickness (thicker, emphasized)
    const float tRef  = 0.035f; // reference-line thickness (posts + sky bar)
    const glm::vec4 bottomCol{1.00f, 0.95f, 0.55f, 1.00f}; // bright warm — the hit line
    const glm::vec4 skyCol   {0.55f, 0.80f, 1.00f, 0.55f}; // dim cool — sky reference
    const glm::vec4 postCol  {0.80f, 0.82f, 0.92f, 0.50f}; // dim neutral — side reference
    const glm::vec3 n{0.f, 0.f, 1.f};

    std::vector<MeshVertex> verts;
    std::vector<uint32_t>   indices;
    auto addBar = [&](float x0, float yy0, float x1, float yy1, const glm::vec4& c) {
        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({{x0, yy0, z}, n, {0.f, 0.f}, c});
        verts.push_back({{x1, yy0, z}, n, {1.f, 0.f}, c});
        verts.push_back({{x1, yy1, z}, n, {1.f, 1.f}, c});
        verts.push_back({{x0, yy1, z}, n, {0.f, 1.f}, c});
        indices.insert(indices.end(), {base, base+1, base+2, base+2, base+3, base});
    };

    // Bottom (judgment) bar: thicker, grown UPWARD from y0 so its bottom edge
    // still matches the ground's near edge exactly.
    addBar(xL,                y0,               xR,                y0 + tMain,       bottomCol);
    // Sky bar
    addBar(xL,                y1 - tRef * 0.5f, xR,                y1 + tRef * 0.5f, skyCol);
    // Left post — grown INWARD from xL so its outer edge stays at xL.
    addBar(xL,                y0,               xL + tRef,         y1,               postCol);
    // Right post — grown INWARD from xR so its outer edge stays at xR.
    addBar(xR - tRef,         y0,               xR,                y1,               postCol);

    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

Mesh ArcaeaRenderer::buildArcTapMesh(Renderer& renderer) {
    // Thin horizontal bar in the XY plane, camera-facing — the Arcaea
    // reference ArcTap is a glowing pill suspended in the sky. We center
    // on (0, 0) so the draw-time translate places it at (arcX, arcY).
    float hw = 0.32f;  // horizontal half-length
    float hh = 0.08f;  // vertical half-thickness
    const glm::vec3 n{0.f, 0.f, 1.f};   // camera-facing, no rim blowout
    const glm::vec4 col{1.0f, 1.0f, 1.0f, 1.0f}; // white core
    std::vector<MeshVertex> verts = {
        {{-hw, -hh, 0.f}, n, {0,0}, col},
        {{ hw, -hh, 0.f}, n, {1,0}, col},
        {{ hw,  hh, 0.f}, n, {1,1}, col},
        {{-hw,  hh, 0.f}, n, {0,1}, col},
    };
    std::vector<uint32_t> indices = {0,1,2, 2,3,0};
    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

Mesh ArcaeaRenderer::buildTapMesh(Renderer& renderer) {
    // Size the tap to fit its lane slot — otherwise edge-lane taps bleed
    // past the lane's near-edge corner (half-width > half-slot).
    float slotHalf = LANE_HALF_WIDTH / static_cast<float>(std::max(1, m_laneCount));
    float hw = std::min(0.4f, slotHalf * 0.9f);
    float hh = 0.05f;
    float hd = std::min(0.4f, slotHalf * 0.9f);
    glm::vec4 col = {1.f, 0.9f, 0.5f, 1.f};
    // Camera-facing normal (see writeArcVertices comment) so the bright tap
    // color doesn't get tripled and saturate to pure white via the rim glow.
    const glm::vec3 n{0.f, 0.f, 1.f};
    std::vector<MeshVertex> verts = {
        {{-hw, GROUND_Y+hh, -hd}, n, {0,0}, col},
        {{ hw, GROUND_Y+hh, -hd}, n, {1,0}, col},
        {{ hw, GROUND_Y+hh,  hd}, n, {1,1}, col},
        {{-hw, GROUND_Y+hh,  hd}, n, {0,1}, col},
    };
    std::vector<uint32_t> indices = {0,1,2, 2,3,0};
    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

void ArcaeaRenderer::onRender(Renderer& renderer) {
    renderer.setCamera(m_camera);

    // Ground
    renderer.meshes().drawMesh(m_groundMesh, glm::mat4(1.f), {1,1,1,1});

    // Judgment gate (bottom bar + sky line + two vertical posts at z=0)
    renderer.meshes().drawMesh(m_gateMesh, glm::mat4(1.f), {1,1,1,1});

    // Tap notes — cull the moment they cross the judgment plane so they
    // never render past the lane's near edge (they'd otherwise fall through
    // the foreground for ~0.25s before being culled).
    for (auto& note : m_tapNotes) {
        float z = static_cast<float>(note.time - m_songTime) * SCROLL_SPEED;
        if (z < 0.f || z > 30.f) continue;
        float laneX = 0.f;
        if (auto* tap = std::get_if<TapData>(&note.data))     laneX = tap->laneX;
        else if (auto* fl = std::get_if<FlickData>(&note.data)) laneX = fl->laneX;
        else continue;

        // Map lane index [0, m_laneCount-1] into N equal slots across the
        // ground's near edge. Each lane center sits half-a-slot inside the
        // edge, so a tap sized to fit its slot never crosses the lane
        // boundary. (Bandori-style; earlier (N-1)-based mapping put lane 0
        // *on* the edge, so even a slightly-wide tap bled outside.)
        float laneSpacing = (2.f * LANE_HALF_WIDTH) / static_cast<float>(std::max(1, m_laneCount));
        float wx = (laneX - (m_laneCount - 1) * 0.5f) * laneSpacing;
        glm::mat4 model = glm::translate(glm::mat4(1.f), {wx, 0.f, JUDGMENT_Z - z});
        glm::vec4 tint = (note.type == NoteType::Flick)
            ? glm::vec4{1.f, 0.35f, 0.35f, 1.f}
            : glm::vec4{1.f, 1.f,   1.f,   1.f};
        renderer.meshes().drawMesh(m_tapMesh, model, tint);
    }

    // ArcTaps — camera-facing glowing bars floating in the sky. Positioned
    // at (arcX, arcY) in normalized [0,1] space, mapped to world coords
    // through the same LANE_HALF_WIDTH / skyHeight transform the arcs use.
    for (auto& note : m_arcTaps) {
        float z = static_cast<float>(note.time - m_songTime) * SCROLL_SPEED;
        if (z < 0.f || z > 30.f) continue;
        auto* tap = std::get_if<TapData>(&note.data);
        if (!tap) continue;
        float wx = (tap->laneX * 2.f - 1.f) * LANE_HALF_WIDTH;
        float wy = GROUND_Y + tap->scanY * m_skyHeight;
        glm::mat4 model = glm::translate(glm::mat4(1.f), {wx, wy, JUDGMENT_Z - z});
        renderer.meshes().drawMesh(m_arcTapMesh, model, {1, 1, 1, 1});
    }

    // Arc ribbons — head vertex already sits on the judgment line (z=0) thanks
    // to the per-frame rebuild in onUpdate. Skip arcs that have been fully
    // consumed so we don't draw a degenerate collapsed mesh. Void arcs are
    // invisible carriers for ArcTaps (per Arcaea convention) — skip them
    // entirely rather than drawing semi-transparent gray ribbons.
    for (auto& am : m_arcs) {
        if (am.data.isVoid) continue;
        double songRel = m_songTime - am.startTime;
        if (songRel >= am.data.duration) continue;
        float zOffset = static_cast<float>(am.startTime - m_songTime) * SCROLL_SPEED;
        if (zOffset > 30.f) continue;
        glm::mat4 model = glm::translate(glm::mat4(1.f), {0.f, 0.f, -zOffset});
        renderer.meshes().drawMesh(am.mesh, model, {1,1,1,1});
    }
}

void ArcaeaRenderer::onShutdown(Renderer& renderer) {
    renderer.meshes().destroyMesh(renderer.buffers(), m_groundMesh);
    renderer.meshes().destroyMesh(renderer.buffers(), m_tapMesh);
    renderer.meshes().destroyMesh(renderer.buffers(), m_gateMesh);
    renderer.meshes().destroyMesh(renderer.buffers(), m_arcTapMesh);
    for (auto& am : m_arcs)
        renderer.meshes().destroyMesh(renderer.buffers(), am.mesh);
    m_arcs.clear();
    m_tapNotes.clear();
    m_arcTaps.clear();
    m_hitEvents.clear();
    m_renderer = nullptr;
}

void ArcaeaRenderer::showJudgment(int lane, Judgment judgment) {
    if (!m_renderer || judgment == Judgment::Miss) return;

    // Particles are transformed by the active camera's viewProj (see
    // quad.vert); Arcaea's camera is 3D perspective, so we emit in world
    // space, not screen pixels.
    //
    // Routing by `lane`:
    //   lane > 0  → a lane-based hit (tap, flick, hold-tick) — emit at the
    //               ground slot for that lane.
    //   lane == 0 → either a real lane-0 tap OR an arc/arctap (Engine clamps
    //               their lane=-1 to 0). If a sky event exists within a
    //               tight timing window right now, prefer it. Otherwise use
    //               lane-0 ground.
    glm::vec2 worldHit;
    float laneSpacing = (2.f * LANE_HALF_WIDTH) / static_cast<float>(std::max(1, m_laneCount));
    auto groundForLane = [&](int ln) {
        float wx = (static_cast<float>(ln) - (m_laneCount - 1) * 0.5f) * laneSpacing;
        return glm::vec2{wx, GROUND_Y};
    };

    if (lane > 0) {
        worldHit = groundForLane(lane);
    } else {
        constexpr double kWindow = 0.03;  // ~2 frames at 60fps
        const HitEvent* best = nullptr;
        double bestDelta = kWindow;
        for (const auto& e : m_hitEvents) {
            double d = std::abs(e.time - m_songTime);
            if (d < bestDelta) { bestDelta = d; best = &e; }
        }
        worldHit = best ? best->worldPos : groundForLane(0);
    }

    glm::vec4 pColor{1, 1, 1, 1};
    int pCount = 12;
    switch (judgment) {
        case Judgment::Perfect: pColor = {0.2f, 1.0f, 0.3f, 1.f}; pCount = 20; break;
        case Judgment::Good:    pColor = {0.3f, 0.6f, 1.0f, 1.f}; pCount = 14; break;
        case Judgment::Bad:     pColor = {1.0f, 0.25f, 0.2f, 1.f}; pCount = 8;  break;
        default: break;
    }
    m_renderer->particles().emitBurst(worldHit, pColor, pCount, 3.f, 0.15f, 0.5f);
}
