#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

// Material system — lightweight, per-quad rendering configuration.
// A Material selects one of N fragment-shader variants (kind), plus a tint,
// optional texture, and up to 4 free float parameters whose meaning depends
// on the kind (documented below).
//
// Batching: QuadBatch groups draws by (MaterialKind, VkImageView). Switching
// kind mid-frame costs one pipeline bind. Keep per-frame kind diversity small.

enum class MaterialKind : uint32_t {
    Unlit    = 0,  // textureColor * vertexColor * tint
    Glow     = 1,  // Unlit + additive bloom boost
    Scroll   = 2,  // UV scrolls over time
    Pulse    = 3,  // rgb reacts to a trigger time
    Gradient = 4,  // two-color gradient across the quad
    Count
};

// params[] meaning per kind:
//   Unlit    : unused
//   Glow     : [intensity, falloff, hdrCap, _]
//   Scroll   : [uSpeed, vSpeed, uTile, vTile]
//   Pulse    : [lastHitTime, decay, peakMult, _]
//   Gradient : [bottomR, bottomG, bottomB, mode] (0 = vertical, 1 = radial)

struct Material {
    MaterialKind kind    = MaterialKind::Unlit;
    glm::vec4    tint    = {1.f, 1.f, 1.f, 1.f};
    glm::vec4    params  = {0.f, 0.f, 0.f, 0.f};
    VkImageView  texture = VK_NULL_HANDLE;  // null → whiteView()
    VkSampler    sampler = VK_NULL_HANDLE;  // null → whiteSampler()
};

const char*  kindName(MaterialKind k);
MaterialKind parseKind(const std::string& s);
