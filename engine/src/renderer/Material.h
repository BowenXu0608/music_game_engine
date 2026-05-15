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

// params[] meaning per kind (SpecialEffect class only):
//   Unlit    : unused
//   Glow     : [intensity, falloff, hdrCap, _]
//   Scroll   : [uSpeed, vSpeed, uTile, vTile]
//   Pulse    : [lastHitTime, decay, peakMult, _]
//   Gradient : [bottomR, bottomG, bottomB, mode] (0 = vertical, 1 = radial)

// A Material is now one of two classes:
//   Pbr           — physically-based (metallic-roughness). The real material
//                    system: lit by the renderer's preinstalled directional +
//                    ambient light. `pbr` carries the parameters.
//   SpecialEffect — the legacy stylized fragment shaders selected by `kind`.
//                    Retained so existing charts/decorative draws keep working;
//                    no longer authorable from the material picker.
enum class MaterialClass : uint32_t { Pbr = 0, SpecialEffect = 1 };

// Core PBR parameters (Disney metallic-roughness subset). Mirrors the bytes the
// PBR pipeline reads from the 128 B push-constant block.
struct PbrParams {
    glm::vec4 baseColor         = {1.f, 1.f, 1.f, 1.f}; // rgb + alpha
    float     metallic          = 0.f;
    float     roughness         = 0.5f;
    float     emissiveIntensity = 0.f;
    float     flags             = 0.f;                  // bit0 = hasNormalMap
    glm::vec3 emissiveColor     = {0.f, 0.f, 0.f};
    float     _pad              = 0.f;
};

static constexpr float PBR_FLAG_HAS_NORMAL_MAP = 1.f; // flags bit0

struct Material {
    // Default is SpecialEffect so the many renderer sites that construct a
    // Material and only set legacy `kind`/`tint` keep their exact prior look.
    // resolveMaterial() explicitly sets `cls = Pbr` for PBR-class assets.
    MaterialClass cls   = MaterialClass::SpecialEffect;

    // ── PBR class ───────────────────────────────────────────────────────────
    PbrParams    pbr;
    VkImageView  baseColorTex  = VK_NULL_HANDLE; // null → whiteView()
    VkSampler    baseColorSamp = VK_NULL_HANDLE; // null → whiteSampler()
    VkImageView  normalTex     = VK_NULL_HANDLE; // null → flatNormalView()
    VkSampler    normalSamp    = VK_NULL_HANDLE; // null → flatNormalSampler()
    // Absolute paths (resolveMaterial expands project-relative → absolute).
    // The game-mode renderer loads these into baseColorTex/normalTex in
    // onInit; empty → use the renderer's white / flat-normal fallback.
    std::string  baseColorTexPath;
    std::string  normalTexPath;

    // ── SpecialEffect class (legacy) ────────────────────────────────────────
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

const char*   kindName(MaterialKind k);
MaterialKind  parseKind(const std::string& s);
const char*   classToString(MaterialClass c);
MaterialClass parseClass(const std::string& s);
