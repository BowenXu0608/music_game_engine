#pragma once
#include "RenderTypes.h"
#include "vulkan/BufferManager.h"
#include "vulkan/DescriptorManager.h"
#include "vulkan/Pipeline.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>

class VulkanContext;

static constexpr uint32_t MAX_PARTICLES = 4096;

// Per-particle vertex. Distinct from QuadVertex so custom particle fragment
// shaders can receive a `data` channel (life01, seed) without disturbing the
// shared quad layout.
struct ParticleVertex {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;
    glm::vec2 data;   // x = life01 (1->0), y = seed

    static VkVertexInputBindingDescription binding() {
        return {0, sizeof(ParticleVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }
    static std::array<VkVertexInputAttributeDescription, 4> attributes() {
        return {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(ParticleVertex, pos)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(ParticleVertex, uv)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ParticleVertex, color)},
            {3, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(ParticleVertex, data)},
        }};
    }
};

struct Particle {
    glm::vec2 pos{};
    glm::vec2 vel{};
    glm::vec2 accel{};                 // gravity / acceleration (units/s^2)
    glm::vec4 color{1.f, 1.f, 1.f, 1.f};      // color at spawn
    glm::vec4 colorEnd{1.f, 1.f, 1.f, 0.f};   // color lerped toward as life -> 0
    float     sizeStart = 8.f;
    float     sizeEnd   = 8.f;
    float     rot       = 0.f;
    float     angVel    = 0.f;
    float     life      = 0.f;         // remaining lifetime in seconds
    float     maxLife   = 1.f;
    float     seed      = 0.f;
    float     drag      = 0.92f;       // per-frame velocity multiplier
    uint16_t  pipeKey   = 0;           // 0 = built-in soft sprite; else custom pipeline
};

// Flat emission parameters. Phase 1 resolves a ParticleEffectAsset into one of
// these; Phase 0 ships built-in defaults and the legacy burst path.
struct ParticleEmit {
    enum class Pattern { Radial, Directional, Ring, Aura };
    Pattern   pattern   = Pattern::Radial;
    int       count     = 16;
    float     speedMin  = 120.f;
    float     speedMax  = 240.f;
    float     sizeStart = 8.f;
    float     sizeEnd   = 2.f;
    float     lifeMin   = 0.35f;
    float     lifeMax   = 0.6f;
    float     spread    = 6.2831853f;  // angular spread (radians); cone for Directional
    glm::vec2 gravity   {0.f, 0.f};
    glm::vec4 color     {1.f, 1.f, 1.f, 1.f};
    glm::vec4 colorEnd  {1.f, 1.f, 1.f, 0.f};
    float     drag      = 0.92f;
    float     rateHz    = 60.f;        // sustained emission rate (Aura)
    uint16_t  pipeKey   = 0;
};

class ParticleSystem {
public:
    void init(VulkanContext& ctx, BufferManager& bufMgr,
              DescriptorManager& descMgr, VkRenderPass renderPass,
              const std::string& shaderDir);
    void shutdown(VulkanContext& ctx, BufferManager& bufMgr);

    // Low-level single-particle spawn (legacy callers).
    void emit(glm::vec2 pos, glm::vec2 vel, glm::vec4 color,
              float size, float lifetime);

    // Legacy convenience burst. Routes to a Radial ParticleEmit so existing
    // per-renderer call sites keep working unchanged.
    void emitBurst(glm::vec2 pos, glm::vec4 color, int count = 12,
                   float speed = 200.f, float size = 8.f, float lifetime = 0.5f);

    // One-shot emission of a resolved effect at `pos`. `dir` orients
    // Directional/Ring patterns (screen space: -y is "up").
    void emitEffect(const ParticleEmit& fx, glm::vec2 pos,
                    glm::vec2 dir = {0.f, -1.f});

    // Sustained emission for a held note. Call every frame while the hold is
    // active; stops automatically once an id is no longer fed. `dt` drives the
    // per-id emission-rate accumulator.
    void emitSustained(uint32_t id, const ParticleEmit& fx,
                       glm::vec2 pos, float dt);

    // Register a custom fragment shader (.frag source or .spv). Returns a
    // pipeKey (>0) to stamp on emitted particles, or 0 (built-in) on failure;
    // `errorOut` receives the glslc / build log.
    uint16_t registerCustomPipeline(const std::string& fragPath,
                                    std::string* errorOut = nullptr);

    void update(float dt);
    void flush(VkCommandBuffer cmd, int frameIndex, VkDescriptorSet whiteTexSet);
    void updateFrameUBO(const FrameUBO& ubo, int frameIndex);

private:
    void spawnOne(glm::vec2 pos, glm::vec2 vel, const ParticleEmit& fx, float t);

    std::array<Particle, MAX_PARTICLES> m_pool;
    uint32_t m_head = 0;   // ring buffer head

    Pipeline         m_builtinPipeline;                    // pipeKey 0
    std::unordered_map<uint16_t, Pipeline> m_customPipelines;   // pipeKey -> pipeline
    std::unordered_map<std::string, uint16_t> m_customPipeKeys; // fragPath -> pipeKey
    uint16_t         m_nextPipeKey = 1;

    std::unordered_map<uint32_t, float> m_sustainAccum;    // hold id -> spawn budget

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    std::vector<Buffer>          m_vertexBuffers;
    std::vector<Buffer>          m_ubos;
    std::vector<VkDescriptorSet> m_frameSets;
    int m_currentFrame = 0;

    // Saved for lazy custom-pipeline builds.
    VulkanContext* m_ctx = nullptr;
    VkRenderPass   m_renderPass = VK_NULL_HANDLE;
    std::string    m_shaderDir;
};
