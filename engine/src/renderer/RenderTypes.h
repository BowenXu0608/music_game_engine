#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#include <array>

// ── Vertex layouts ──────────────────────────────────────────────────────────

struct QuadVertex {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;

    static VkVertexInputBindingDescription binding() {
        return {0, sizeof(QuadVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }
    static std::array<VkVertexInputAttributeDescription, 3> attributes() {
        return {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(QuadVertex, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(QuadVertex, uv)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadVertex, color)},
        }};
    }
};

struct LineVertex {
    glm::vec2 pos;
    glm::vec4 color;

    static VkVertexInputBindingDescription binding() {
        return {0, sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }
    static std::array<VkVertexInputAttributeDescription, 2> attributes() {
        return {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(LineVertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LineVertex, color)},
        }};
    }
};

struct MeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 color;

    static VkVertexInputBindingDescription binding() {
        return {0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }
    static std::array<VkVertexInputAttributeDescription, 4> attributes() {
        return {{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(MeshVertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(MeshVertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(MeshVertex, uv)},
            {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, color)},
        }};
    }
};

// ── Render layers (draw order) ───────────────────────────────────────────────

enum class RenderLayer : uint8_t {
    Background = 0,
    World      = 1,
    Notes      = 2,
    Effects    = 3,
    UI         = 4,
    Count
};

// ── Per-frame UBO (set 0, binding 0) ────────────────────────────────────────
// v2: 128 B std140-clean. Shared by QuadBatch / LineBatch / MeshRenderer /
// ParticleSystem. `time` lives in cameraPos.w (it MOVED here from a dedicated
// field) so the PBR path can also read the camera world position. Every shader
// that used `ubo.time` now reads `ubo.cameraPos.w`.

struct FrameUBO {
    glm::mat4 viewProj;     //   0  64
    glm::vec4 cameraPos;    //  64  16  xyz = eye world pos, w = time
    glm::vec4 lightDir;     //  80  16  xyz = directional light dir (world, normalized)
    glm::vec4 lightColor;   //  96  16  rgb = color, w = intensity
    glm::vec4 ambient;      // 112  16  rgb = ambient color, w = ambient intensity
};                          // 128 B

// ── Push constants (exactly 128 bytes — Vulkan guaranteed minimum) ───────────
// WARNING: do not add fields. 128B is the spec floor (maxPushConstantsSize).
// If more per-draw data is needed, switch to a per-instance SSBO.
//
// The PBR pipelines reinterpret this SAME 128 B block (no field add): they bind
// a distinct pipeline whose GLSL push-constant layout maps the bytes as:
//   offset 64  tint        -> vec4 baseColor   (rgb + alpha)
//   offset 80  uvTransform -> vec4 mrp         (x=metallic y=roughness
//                                               z=emissiveIntensity w=flags)
//   offset 96  params      -> vec4 emissive    (rgb emissive color)
//   offset 112 kind        -> uint kind        (PBR sentinel 0xFFFFFFFF)
// flags bit0 = hasNormalMap. The legacy/effect shaders keep the original
// interpretation below — the batcher binds the matching pipeline so there is
// no runtime branch.

struct QuadPushConstants {
    glm::mat4 model;        // 64 B  — first keeps mat4 16-aligned
    glm::vec4 tint;         // 16 B  — rgba multiplier  (PBR: baseColor)
    glm::vec4 uvTransform;  // 16 B  — xy=offset, zw=scale  (PBR: mrp)
    glm::vec4 params;       // 16 B  — meaning depends on material kind (PBR: emissive)
    uint32_t  kind;         //  4 B  — MaterialKind cast to uint (PBR: 0xFFFFFFFF)
    uint32_t  _pad[3];      // 12 B  → total 128 B
};

// Sentinel written into QuadPushConstants::kind / MeshPushConstants::kind when
// the bound pipeline is the PBR pipeline (so the value is unambiguous in
// captures; the shader does not actually branch on it).
static constexpr uint32_t PBR_KIND_SENTINEL = 0xFFFFFFFFu;

// Mesh push-constant block — byte-identical to QuadPushConstants so both batchers
// can share the same shader push-constant declaration. Kept as a distinct type
// so the intent at the call site is obvious (3D mesh vs 2D quad).
struct MeshPushConstants {
    glm::mat4 model;
    glm::vec4 tint;
    glm::vec4 uvTransform;
    glm::vec4 params;
    uint32_t  kind;
    uint32_t  _pad[3];
};

// ── Draw call descriptor ─────────────────────────────────────────────────────

struct DrawCall {
    uint32_t    firstIndex;
    uint32_t    indexCount;
    uint32_t    vertexOffset;
    VkImageView texture;        // nullptr = white 1x1
    RenderLayer layer;
};
