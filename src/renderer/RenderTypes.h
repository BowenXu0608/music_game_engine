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

struct FrameUBO {
    glm::mat4 viewProj;
    float     time;
    float     _pad[3];
};

// ── Push constants (96 bytes max) ────────────────────────────────────────────

struct QuadPushConstants {
    glm::vec4 tint;         // rgba multiplier
    glm::vec4 uvTransform;  // xy=offset, zw=scale
    glm::mat4 model;        // 64 bytes — total = 96
};

// ── Draw call descriptor ─────────────────────────────────────────────────────

struct DrawCall {
    uint32_t    firstIndex;
    uint32_t    indexCount;
    uint32_t    vertexOffset;
    VkImageView texture;        // nullptr = white 1x1
    RenderLayer layer;
};
