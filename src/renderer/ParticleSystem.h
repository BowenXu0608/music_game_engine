#pragma once
#include "RenderTypes.h"
#include "vulkan/BufferManager.h"
#include "vulkan/DescriptorManager.h"
#include "vulkan/Pipeline.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>

class VulkanContext;

static constexpr uint32_t MAX_PARTICLES = 2048;

struct Particle {
    glm::vec2 pos;
    glm::vec2 vel;
    glm::vec4 color;
    float     size;
    float     life;      // remaining lifetime in seconds
    float     maxLife;
};

class ParticleSystem {
public:
    void init(VulkanContext& ctx, BufferManager& bufMgr,
              DescriptorManager& descMgr, VkRenderPass renderPass,
              const std::string& shaderDir);
    void shutdown(VulkanContext& ctx, BufferManager& bufMgr);

    void emit(glm::vec2 pos, glm::vec2 vel, glm::vec4 color,
              float size, float lifetime);

    // Convenience: burst of particles at a position (hit effect)
    void emitBurst(glm::vec2 pos, glm::vec4 color, int count = 12,
                   float speed = 200.f, float size = 8.f, float lifetime = 0.5f);

    void update(float dt);
    void flush(VkCommandBuffer cmd, int frameIndex, VkDescriptorSet whiteTexSet);
    void updateFrameUBO(const glm::mat4& viewProj, float time, int frameIndex);

private:
    std::array<Particle, MAX_PARTICLES> m_pool;
    uint32_t m_head = 0;   // ring buffer head

    Pipeline         m_pipeline;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    std::vector<Buffer>          m_vertexBuffers;
    std::vector<Buffer>          m_ubos;
    std::vector<VkDescriptorSet> m_frameSets;
    int m_currentFrame = 0;
};
