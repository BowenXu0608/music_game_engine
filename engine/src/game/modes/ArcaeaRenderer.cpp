#include "ArcaeaRenderer.h"
#include "renderer/Renderer.h"
#include "renderer/MaterialAssetLibrary.h"
#include "ui/ProjectHub.h"   // GameModeConfig definition
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {

// Slot ids mirror MaterialSlots.cpp::kArcaeaSlots.
enum ArcaeaSlot : uint16_t {
    SlotClickNote    = 0,
    SlotFlickNote    = 1,
    SlotArcTapTile   = 2,
    SlotArcTapShadow = 3,
    SlotArcBlue      = 4,
    SlotArcRed       = 5,
    SlotArcBlueShdw  = 6,
    SlotArcRedShdw   = 7,
    SlotGround       = 8,
    SlotJudgmentBar  = 9,
    SlotSkyLine      = 10,
    SlotSidePosts    = 11,
};

} // namespace

Material ArcaeaRenderer::slotOrFallback(uint16_t slot, const Material& fallback) const {
    auto it = m_chartMaterials.find(slot);
    if (it == m_chartMaterials.end()) return fallback;
    Material m = it->second;
    // Texture/sampler resolution by path is Phase-later; every chart material
    // currently rides the white fallback texture.
    m.texture = fallback.texture;
    m.sampler = fallback.sampler;
    return m;
}

void ArcaeaRenderer::onInit(Renderer& renderer, const ChartData& chart,
                            const GameModeConfig* config) {
    m_renderer = &renderer;
    if (config && config->trackCount > 0) m_laneCount = config->trackCount;
    if (config)                            m_skyHeight = config->skyHeight;

    // Import per-slot material overrides from the chart. resolveMaterial()
    // picks whichever form the entry uses (asset reference or legacy inline).
    m_chartMaterials.clear();
    for (const auto& md : chart.materials) {
        m_chartMaterials[md.slot] = resolveMaterial(md, m_materialLibrary);
    }

    for (auto& note : chart.notes) {
        if (note.type == NoteType::Arc) {
            ArcMesh am{};
            am.data      = std::get<ArcData>(note.data);
            am.startTime = note.time;
            am.mesh      = buildDynamicArcMesh(renderer);
            am.shadow    = buildDynamicArcShadowMesh(renderer);
            writeArcVertices(am, 0.f);
            writeArcShadowVertices(am, 0.f);
            m_arcs.push_back(std::move(am));
        } else if (note.type == NoteType::Tap || note.type == NoteType::Flick) {
            m_tapNotes.push_back(note);
        } else if (note.type == NoteType::Hold) {
            m_holdNotes.push_back(note);
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
    for (auto& n : m_holdNotes) {
        if (auto* hd = std::get_if<HoldData>(&n.data)) {
            int lane = static_cast<int>(std::lround(hd->laneX));
            if (lane >= m_laneCount) m_laneCount = lane + 1;
        }
    }

    // Build ground plane mesh
    m_groundMesh = buildGroundMesh(renderer);
    m_tapMesh    = buildTapMesh(renderer);

    // Gate — split into four independently-materialized bars. Geometry
    // identical to the old single-mesh version so the near-edge alignment with
    // the ground is preserved.
    {
        const float xL    = -LANE_HALF_WIDTH;
        const float xR    =  LANE_HALF_WIDTH;
        const float y0    = GROUND_Y;
        const float y1    = GROUND_Y + m_skyHeight;
        const float tMain = 0.08f;
        const float tRef  = 0.035f;
        m_gateBottom    = buildGateBar(renderer, xL,           y0,               xR,           y0 + tMain);
        m_gateSky       = buildGateBar(renderer, xL,           y1 - tRef * 0.5f, xR,           y1 + tRef * 0.5f);
        m_gateLeftPost  = buildGateBar(renderer, xL,           y0,               xL + tRef,    y1);
        m_gateRightPost = buildGateBar(renderer, xR - tRef,    y0,               xR,           y1);
    }

    m_arcTapMesh       = buildArcTapMesh(renderer);
    m_arcTapShadowMesh = buildArcTapShadowMesh(renderer);

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
        writeArcShadowVertices(am, tClip);
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
    //
    // Hexagonal prism: (ARC_SEGMENTS + 1) rings, each with ARC_SIDES vertices
    // around the cross-section. Each segment joins ring i to ring i+1 with
    // ARC_SIDES side quads (2 triangles per quad).
    const size_t vertCount = static_cast<size_t>(ARC_SEGMENTS + 1) * ARC_SIDES;
    const size_t idxCount  = static_cast<size_t>(ARC_SEGMENTS) * ARC_SIDES * 6;

    Mesh mesh;
    mesh.indexCount   = static_cast<uint32_t>(idxCount);
    mesh.vertexBuffer = renderer.buffers().createDynamicBuffer(
        sizeof(MeshVertex) * vertCount, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    mesh.indexBuffer  = renderer.buffers().createDeviceBuffer(
        sizeof(uint32_t) * idxCount, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    // Per segment i, per side s: emit the quad between ring[i][s],
    // ring[i][s+1], ring[i+1][s], ring[i+1][s+1]. Winding CCW viewed from
    // outside so backface culling (if enabled upstream) keeps the outer
    // hull visible.
    std::vector<uint32_t> indices;
    indices.reserve(idxCount);
    for (int i = 0; i < ARC_SEGMENTS; ++i) {
        uint32_t ringA = static_cast<uint32_t>(i)     * ARC_SIDES;
        uint32_t ringB = static_cast<uint32_t>(i + 1) * ARC_SIDES;
        for (int s = 0; s < ARC_SIDES; ++s) {
            uint32_t sNext = (s + 1) % ARC_SIDES;
            uint32_t a0 = ringA + s;
            uint32_t a1 = ringA + sNext;
            uint32_t b0 = ringB + s;
            uint32_t b1 = ringB + sNext;
            indices.insert(indices.end(), {a0, a1, b1, a0, b1, b0});
        }
    }
    renderer.buffers().uploadToBuffer(renderer.context(), mesh.indexBuffer,
                                      indices.data(), sizeof(uint32_t) * idxCount);
    return mesh;
}

void ArcaeaRenderer::writeArcVertices(ArcMesh& am, float tClip) {
    // Vertex colour is white — actual arc colour comes from the Material
    // selected in onRender (SlotArcBlue or SlotArcRed) via Material.tint.
    const glm::vec4 color{1.f, 1.f, 1.f, 1.f};

    // Precompute hexagon cross-section offsets (local xy, unit radius).
    glm::vec2 ringDir[ARC_SIDES];
    for (int s = 0; s < ARC_SIDES; ++s) {
        float a = (static_cast<float>(s) / ARC_SIDES) * 6.2831853f;
        ringDir[s] = {std::cos(a), std::sin(a)};
    }

    std::vector<MeshVertex> verts;
    verts.reserve(static_cast<size_t>(ARC_SEGMENTS + 1) * ARC_SIDES);
    for (int i = 0; i <= ARC_SEGMENTS; ++i) {
        float frac = static_cast<float>(i) / ARC_SEGMENTS;
        float t    = tClip + (1.f - tClip) * frac;
        float z    = -t * static_cast<float>(am.data.duration) * (SCROLL_SPEED * m_noteSpeedMul);
        glm::vec2 xy = evalArc(am.data, t);

        for (int s = 0; s < ARC_SIDES; ++s) {
            MeshVertex v{};
            v.pos = {xy.x + ringDir[s].x * ARC_RADIUS,
                     xy.y + ringDir[s].y * ARC_RADIUS,
                     z};
            // Outward radial normal — Glow kind's rim-glow term picks up every
            // side face so the tube reads as a glowing 3D column instead of a
            // flat ribbon.
            v.normal = {ringDir[s].x, ringDir[s].y, 0.f};
            v.color  = color;
            v.uv     = {static_cast<float>(s) / ARC_SIDES, t};
            verts.push_back(v);
        }
    }

    std::memcpy(am.mesh.vertexBuffer.mapped, verts.data(),
                sizeof(MeshVertex) * verts.size());
}

Mesh ArcaeaRenderer::buildDynamicArcShadowMesh(Renderer& renderer) {
    // Flat 2-vertex-per-ring ribbon on the ground, same segment count as the
    // arc so it can track the arc's xy projection sample-for-sample.
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

void ArcaeaRenderer::writeArcShadowVertices(ArcMesh& am, float tClip) {
    // Vertex colour is white — shadow tint comes from the Material chosen in
    // onRender (SlotArcBlueShdw or SlotArcRedShdw). Camera-facing normal keeps
    // any Glow rim at 0 so the shadow reads flat even if a user picks Glow.
    const glm::vec4 color{1.f, 1.f, 1.f, 1.f};
    const glm::vec3 n{0.f, 0.f, 1.f};
    const float y      = GROUND_Y + 0.02f;          // tiny lift to dodge z-fight
    const float hw     = ARC_RADIUS;                 // half-width of shadow band

    std::vector<MeshVertex> verts;
    verts.reserve(static_cast<size_t>(ARC_SEGMENTS + 1) * 2);
    for (int i = 0; i <= ARC_SEGMENTS; ++i) {
        float frac = static_cast<float>(i) / ARC_SEGMENTS;
        float t    = tClip + (1.f - tClip) * frac;
        float z    = -t * static_cast<float>(am.data.duration) * (SCROLL_SPEED * m_noteSpeedMul);
        glm::vec2 xy = evalArc(am.data, t);

        MeshVertex vL{}, vR{};
        vL.pos = {xy.x - hw, y, z};
        vR.pos = {xy.x + hw, y, z};
        vL.normal = vR.normal = n;
        vL.color  = vR.color  = color;
        vL.uv     = {0.f, t};
        vR.uv     = {1.f, t};
        verts.push_back(vL);
        verts.push_back(vR);
    }

    std::memcpy(am.shadow.vertexBuffer.mapped, verts.data(),
                sizeof(MeshVertex) * verts.size());
}

Mesh ArcaeaRenderer::buildGroundMesh(Renderer& renderer) {
    // Ground quad in XZ plane, spanning the full lane width at the near edge
    // and the full lane depth from JUDGMENT_Z (near) to LANE_FAR_Z (far).
    // UV v runs 0 at the near edge → 1 at the far edge so the Gradient default
    // material fades from tint (near) to params.rgb (far).
    const float w = LANE_HALF_WIDTH;
    const glm::vec4 white{1.f, 1.f, 1.f, 1.f};
    std::vector<MeshVertex> verts = {
        {{-w, GROUND_Y, JUDGMENT_Z}, {0,1,0}, {0,0}, white},
        {{ w, GROUND_Y, JUDGMENT_Z}, {0,1,0}, {1,0}, white},
        {{ w, GROUND_Y, LANE_FAR_Z}, {0,1,0}, {1,1}, white},
        {{-w, GROUND_Y, LANE_FAR_Z}, {0,1,0}, {0,1}, white},
    };
    std::vector<uint32_t> indices = {0,1,2, 2,3,0};
    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

Mesh ArcaeaRenderer::buildGateBar(Renderer& renderer,
                                  float x0, float y0, float x1, float y1) {
    // Single flat quad on the judgment plane (z = JUDGMENT_Z) spanning
    // (x0,y0)→(x1,y1). Vertex colour is white — the caller's Material provides
    // the actual bar colour. Camera-facing normal so Glow's rim stays at 0
    // even if a user picks Glow for a bar.
    const float z = JUDGMENT_Z;
    const glm::vec3 n{0.f, 0.f, 1.f};
    const glm::vec4 white{1.f, 1.f, 1.f, 1.f};
    std::vector<MeshVertex> verts = {
        {{x0, y0, z}, n, {0.f, 0.f}, white},
        {{x1, y0, z}, n, {1.f, 0.f}, white},
        {{x1, y1, z}, n, {1.f, 1.f}, white},
        {{x0, y1, z}, n, {0.f, 1.f}, white},
    };
    std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

Mesh ArcaeaRenderer::buildArcTapMesh(Renderer& renderer) {
    // Flat rectangular prism suspended in the sky — matches the Arcaea
    // reference where the ArcTap reads as a thin horizontal slab (wide in x,
    // thin in y, shallow in z). Centered on (0,0,0) so the draw-time translate
    // places it at (arcX, arcY, z). Per-face outward normals give each face
    // its own rim-glow contribution so the slab reads as 3D under the Glow
    // material kind.
    const float hw = 0.32f;   // half width  (x)
    const float hh = 0.05f;   // half height (y)
    const float hd = 0.12f;   // half depth  (z) — shallow so it stays flat
    const glm::vec4 col{1.0f, 1.0f, 1.0f, 1.0f};

    std::vector<MeshVertex> verts;
    std::vector<uint32_t>   indices;
    auto addFace = [&](glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                       glm::vec3 n) {
        uint32_t b = static_cast<uint32_t>(verts.size());
        verts.push_back({p0, n, {0,0}, col});
        verts.push_back({p1, n, {1,0}, col});
        verts.push_back({p2, n, {1,1}, col});
        verts.push_back({p3, n, {0,1}, col});
        indices.insert(indices.end(), {b, b+1, b+2, b+2, b+3, b});
    };

    // Front (+z) and Back (-z)
    addFace({-hw,-hh, hd},{ hw,-hh, hd},{ hw, hh, hd},{-hw, hh, hd}, { 0, 0, 1});
    addFace({ hw,-hh,-hd},{-hw,-hh,-hd},{-hw, hh,-hd},{ hw, hh,-hd}, { 0, 0,-1});
    // Top (+y) and Bottom (-y)
    addFace({-hw, hh, hd},{ hw, hh, hd},{ hw, hh,-hd},{-hw, hh,-hd}, { 0, 1, 0});
    addFace({-hw,-hh,-hd},{ hw,-hh,-hd},{ hw,-hh, hd},{-hw,-hh, hd}, { 0,-1, 0});
    // Right (+x) and Left (-x)
    addFace({ hw,-hh, hd},{ hw,-hh,-hd},{ hw, hh,-hd},{ hw, hh, hd}, { 1, 0, 0});
    addFace({-hw,-hh,-hd},{-hw,-hh, hd},{-hw, hh, hd},{-hw, hh,-hd}, {-1, 0, 0});

    return renderer.meshes().createMesh(renderer.context(), renderer.buffers(), verts, indices);
}

Mesh ArcaeaRenderer::buildArcTapShadowMesh(Renderer& renderer) {
    // Flat quad on the ground directly below where the arctap renders. Sized
    // to roughly match the arctap's x/z footprint (not its thin y height).
    // Vertex colour is white; the "ArcTap Shadow" Material provides the tint.
    const float hw = 0.36f;
    const float hd = 0.14f;
    const float y  = GROUND_Y + 0.02f;
    const glm::vec3 n{0.f, 0.f, 1.f};
    const glm::vec4 col{1.f, 1.f, 1.f, 1.f};
    std::vector<MeshVertex> verts = {
        {{-hw, y, -hd}, n, {0,0}, col},
        {{ hw, y, -hd}, n, {1,0}, col},
        {{ hw, y,  hd}, n, {1,1}, col},
        {{-hw, y,  hd}, n, {0,1}, col},
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
    // Vertex colour is white — the per-note Material (Click or Flick slot)
    // provides the tint. Camera-facing normal keeps any Glow rim at 0 so
    // bright base colours don't saturate to pure white.
    const glm::vec4 col = {1.f, 1.f, 1.f, 1.f};
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

    auto& ctx  = renderer.context();
    auto& desc = renderer.descriptors();
    VkImageView whiteV = renderer.whiteView();
    VkSampler   whiteS = renderer.whiteSampler();
    auto withWhite = [&](Material m) {
        m.texture = whiteV;
        m.sampler = whiteS;
        return m;
    };

    // Ground — defaults to Gradient (near→far fade).
    {
        Material groundDefault;
        groundDefault.kind   = MaterialKind::Gradient;
        groundDefault.tint   = {0.15f, 0.15f, 0.25f, 1.f};
        groundDefault.params = {0.05f, 0.05f, 0.15f, 0.f};
        Material mat = withWhite(slotOrFallback(SlotGround, groundDefault));
        renderer.meshes().drawMesh(m_groundMesh, glm::mat4(1.f), mat, ctx, desc);
    }

    // Judgment gate — four bars, each with its own slot.
    {
        Material bottomDefault;
        bottomDefault.kind = MaterialKind::Unlit;
        bottomDefault.tint = {1.f, 0.95f, 0.55f, 1.f};
        Material skyDefault;
        skyDefault.kind = MaterialKind::Unlit;
        skyDefault.tint = {0.55f, 0.80f, 1.f, 0.55f};
        Material postDefault;
        postDefault.kind = MaterialKind::Unlit;
        postDefault.tint = {0.80f, 0.82f, 0.92f, 0.50f};
        Material bottomMat = withWhite(slotOrFallback(SlotJudgmentBar, bottomDefault));
        Material skyMat    = withWhite(slotOrFallback(SlotSkyLine,     skyDefault));
        Material postMat   = withWhite(slotOrFallback(SlotSidePosts,   postDefault));
        renderer.meshes().drawMesh(m_gateBottom,    glm::mat4(1.f), bottomMat, ctx, desc);
        renderer.meshes().drawMesh(m_gateSky,       glm::mat4(1.f), skyMat,    ctx, desc);
        renderer.meshes().drawMesh(m_gateLeftPost,  glm::mat4(1.f), postMat,   ctx, desc);
        renderer.meshes().drawMesh(m_gateRightPost, glm::mat4(1.f), postMat,   ctx, desc);
    }

    // Tap/Flick notes — cull the moment they cross the judgment plane so they
    // never render past the lane's near edge.
    Material clickDefault;
    clickDefault.kind = MaterialKind::Unlit;
    clickDefault.tint = {1.f, 0.9f, 0.5f, 1.f};
    Material flickDefault;
    flickDefault.kind = MaterialKind::Unlit;
    flickDefault.tint = {1.f, 0.35f, 0.35f, 1.f};
    Material clickMat = withWhite(slotOrFallback(SlotClickNote, clickDefault));
    Material flickMat = withWhite(slotOrFallback(SlotFlickNote, flickDefault));
    for (auto& note : m_tapNotes) {
        float z = static_cast<float>(note.time - m_songTime) * (SCROLL_SPEED * m_noteSpeedMul);
        if (z < 0.f || z > 30.f) continue;
        float laneX = 0.f;
        if (auto* tap = std::get_if<TapData>(&note.data))       laneX = tap->laneX;
        else if (auto* fl = std::get_if<FlickData>(&note.data)) laneX = fl->laneX;
        else continue;

        float laneSpacing = (2.f * LANE_HALF_WIDTH) / static_cast<float>(std::max(1, m_laneCount));
        float wx = (laneX - (m_laneCount - 1) * 0.5f) * laneSpacing;
        glm::mat4 model = glm::translate(glm::mat4(1.f), {wx, 0.f, JUDGMENT_Z - z});
        const Material& mat = (note.type == NoteType::Flick) ? flickMat : clickMat;
        renderer.meshes().drawMesh(m_tapMesh, model, mat, ctx, desc);
    }

    // Hold notes — Z-stretched tap mesh spanning [head, head+duration]. The
    // tap mesh is centered at z=0 with depth 2*hd (hd ≈ 0.4); we scale Z
    // by len/(2*hd) and translate so the mesh spans [-zTail, -zHead] in
    // world space (head closer to the player = less negative).
    Material holdDefault;
    holdDefault.kind = MaterialKind::Glow;
    holdDefault.tint = {0.3f, 0.8f, 1.f, 0.95f};
    Material holdMat = withWhite(holdDefault);
    constexpr float kTapHd = 0.4f;  // matches buildTapMesh()'s hd cap
    for (auto& note : m_holdNotes) {
        auto* hd = std::get_if<HoldData>(&note.data);
        if (!hd || hd->duration <= 0.f) continue;
        float zHead = static_cast<float>(note.time - m_songTime) * (SCROLL_SPEED * m_noteSpeedMul);
        float zTail = zHead + hd->duration * (SCROLL_SPEED * m_noteSpeedMul);
        if (zTail < 0.f || zHead > 30.f) continue;
        float laneSpacing = (2.f * LANE_HALF_WIDTH) / static_cast<float>(std::max(1, m_laneCount));
        float wx = (hd->laneX - (m_laneCount - 1) * 0.5f) * laneSpacing;
        float midZ = JUDGMENT_Z - 0.5f * (zHead + zTail);
        float lenZ = zTail - zHead;
        float sz   = lenZ / (2.f * kTapHd);
        glm::mat4 model = glm::translate(glm::mat4(1.f), {wx, 0.f, midZ})
                        * glm::scale(glm::mat4(1.f), {1.f, 1.f, sz});
        renderer.meshes().drawMesh(m_tapMesh, model, holdMat, ctx, desc);
    }

    // ArcTaps — flat rectangular prisms floating in the sky at (arcX, arcY)
    // in normalized [0,1] space. Shadow drops directly below on the ground.
    Material arcTapTileDefault;
    arcTapTileDefault.kind = MaterialKind::Glow;
    arcTapTileDefault.tint = {1.f, 1.f, 1.f, 1.f};
    Material arcTapShadowDefault;
    arcTapShadowDefault.kind = MaterialKind::Unlit;
    arcTapShadowDefault.tint = {0.05f, 0.08f, 0.15f, 0.55f};
    Material arcTapTileMat   = withWhite(slotOrFallback(SlotArcTapTile,   arcTapTileDefault));
    Material arcTapShadowMat = withWhite(slotOrFallback(SlotArcTapShadow, arcTapShadowDefault));
    for (auto& note : m_arcTaps) {
        float z = static_cast<float>(note.time - m_songTime) * (SCROLL_SPEED * m_noteSpeedMul);
        if (z < 0.f || z > 30.f) continue;
        auto* tap = std::get_if<TapData>(&note.data);
        if (!tap) continue;
        float wx = (tap->laneX * 2.f - 1.f) * LANE_HALF_WIDTH;
        float wy = GROUND_Y + tap->scanY * m_skyHeight;

        glm::mat4 shadowModel = glm::translate(glm::mat4(1.f),
            {wx, 0.f, JUDGMENT_Z - z});
        renderer.meshes().drawMesh(m_arcTapShadowMesh, shadowModel, arcTapShadowMat, ctx, desc);

        glm::mat4 model = glm::translate(glm::mat4(1.f), {wx, wy, JUDGMENT_Z - z});
        renderer.meshes().drawMesh(m_arcTapMesh, model, arcTapTileMat, ctx, desc);
    }

    // Arc ribbons — head vertex already sits on the judgment line (z=0) thanks
    // to the per-frame rebuild in onUpdate. Void arcs are invisible carriers
    // for ArcTaps (per Arcaea convention) — skip them.
    Material blueArcDefault;
    blueArcDefault.kind = MaterialKind::Glow;
    blueArcDefault.tint = {0.4f, 0.8f, 1.f, 0.9f};
    Material redArcDefault;
    redArcDefault.kind = MaterialKind::Glow;
    redArcDefault.tint = {1.f, 0.4f, 0.7f, 0.9f};
    Material blueShadowDefault;
    blueShadowDefault.kind = MaterialKind::Unlit;
    blueShadowDefault.tint = {0.08f, 0.22f, 0.35f, 0.55f};
    Material redShadowDefault;
    redShadowDefault.kind = MaterialKind::Unlit;
    redShadowDefault.tint = {0.30f, 0.08f, 0.22f, 0.55f};
    Material blueArcMat    = withWhite(slotOrFallback(SlotArcBlue,     blueArcDefault));
    Material redArcMat     = withWhite(slotOrFallback(SlotArcRed,      redArcDefault));
    Material blueShadowMat = withWhite(slotOrFallback(SlotArcBlueShdw, blueShadowDefault));
    Material redShadowMat  = withWhite(slotOrFallback(SlotArcRedShdw,  redShadowDefault));
    for (auto& am : m_arcs) {
        if (am.data.isVoid) continue;
        double songRel = m_songTime - am.startTime;
        if (songRel >= am.data.duration) continue;
        float zOffset = static_cast<float>(am.startTime - m_songTime) * (SCROLL_SPEED * m_noteSpeedMul);
        if (zOffset > 30.f) continue;
        glm::mat4 model = glm::translate(glm::mat4(1.f), {0.f, 0.f, -zOffset});
        const bool isRed = am.data.color != 0;
        const Material& arcMat    = isRed ? redArcMat    : blueArcMat;
        const Material& shadowMat = isRed ? redShadowMat : blueShadowMat;
        renderer.meshes().drawMesh(am.shadow, model, shadowMat, ctx, desc);
        renderer.meshes().drawMesh(am.mesh,   model, arcMat,    ctx, desc);
    }
}

void ArcaeaRenderer::onShutdown(Renderer& renderer) {
    renderer.meshes().destroyMesh(renderer.buffers(), m_groundMesh);
    renderer.meshes().destroyMesh(renderer.buffers(), m_tapMesh);
    renderer.meshes().destroyMesh(renderer.buffers(), m_gateBottom);
    renderer.meshes().destroyMesh(renderer.buffers(), m_gateSky);
    renderer.meshes().destroyMesh(renderer.buffers(), m_gateLeftPost);
    renderer.meshes().destroyMesh(renderer.buffers(), m_gateRightPost);
    renderer.meshes().destroyMesh(renderer.buffers(), m_arcTapMesh);
    renderer.meshes().destroyMesh(renderer.buffers(), m_arcTapShadowMesh);
    for (auto& am : m_arcs) {
        renderer.meshes().destroyMesh(renderer.buffers(), am.mesh);
        renderer.meshes().destroyMesh(renderer.buffers(), am.shadow);
    }
    m_arcs.clear();
    m_tapNotes.clear();
    m_holdNotes.clear();
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
