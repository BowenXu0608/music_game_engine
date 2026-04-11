#pragma once
#include "RenderTypes.h"
#include "vulkan/BufferManager.h"
#include "vulkan/DescriptorManager.h"
#include "vulkan/Pipeline.h"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

class VulkanContext;

static constexpr uint32_t MAX_QUADS = 8192;
static constexpr uint32_t QUAD_VERTS = MAX_QUADS * 4;
static constexpr uint32_t QUAD_INDICES = MAX_QUADS * 6;

class QuadBatch {
public:
    void init(VulkanContext& ctx, BufferManager& bufMgr,
              DescriptorManager& descMgr, VkRenderPass renderPass,
              const std::string& shaderDir);
    void shutdown(VulkanContext& ctx, BufferManager& bufMgr);

    // Submit a quad for batched rendering
    void drawQuad(glm::vec2 pos, glm::vec2 size, float rotation,
                  glm::vec4 color, glm::vec4 uvTransform,
                  VkImageView texture, VkSampler sampler,
                  VulkanContext& ctx, DescriptorManager& descMgr);

    // Submit a quad defined by 4 explicit screen-space corners (any winding;
    // vertex order is p0,p1,p2,p3 → triangles {p0,p1,p2} and {p2,p3,p0}).
    // Use this to draw perspective-warped quads (e.g. ground-plane notes).
    void drawQuadCorners(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                         glm::vec4 color, glm::vec4 uvTransform,
                         VkImageView texture, VkSampler sampler,
                         VulkanContext& ctx, DescriptorManager& descMgr);

    // Flush all pending quads — call once per frame
    void flush(VkCommandBuffer cmd, VulkanContext& ctx, DescriptorManager& descMgr);

    void updateFrameUBO(const glm::mat4& viewProj, float time, int frameIndex);

    Pipeline& pipeline() { return m_pipeline; }

private:
    struct Batch {
        VkImageView     texture;
        VkSampler       sampler;
        VkDescriptorSet texSet = VK_NULL_HANDLE;
        uint32_t        indexStart;
        uint32_t        indexCount;
    };

    void buildIndexBuffer(VulkanContext& ctx, BufferManager& bufMgr);

    Pipeline    m_pipeline;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // Per-frame dynamic VBOs (MAX_FRAMES_IN_FLIGHT)
    std::vector<Buffer> m_vertexBuffers;
    Buffer              m_indexBuffer;

    // Per-frame UBOs
    std::vector<Buffer>          m_ubos;
    std::vector<VkDescriptorSet> m_frameSets;

    std::vector<QuadVertex> m_vertices;
    std::vector<Batch>      m_batches;

    // Texture → descriptor set cache
    std::unordered_map<VkImageView, VkDescriptorSet> m_texSetCache;

    int m_currentFrame = 0;
};
