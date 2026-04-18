#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <array>

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
    // Custom = user-authored fragment shader compiled to SPIR-V at author
    // time. `Material::customShaderPath` points at the .frag file; the
    // batcher resolves the compiled .spv into a Pipeline via its custom
    // pipeline cache. Must still conform to the shared push-constant block
    // and set layouts — batcher provides a template shader to start from.
    Custom   = 5,
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
    // Only meaningful when `kind == Custom`. Full path to the author's .frag
    // source; the batcher invokes glslc + caches a pipeline per unique path.
    // Kept as std::string so Material stays copyable across the batcher's
    // per-draw storage.
    std::string  customShaderPath;
};

const char*  kindName(MaterialKind k);
MaterialKind parseKind(const std::string& s);
