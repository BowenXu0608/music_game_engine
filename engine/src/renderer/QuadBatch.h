#pragma once
#include "RenderTypes.h"
#include "Material.h"
#include "vulkan/BufferManager.h"
#include "vulkan/DescriptorManager.h"
#include "vulkan/Pipeline.h"
#include <glm/glm.hpp>
#include <vector>
#include <array>
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

    // ── New Material-aware overloads ────────────────────────────────────────
    void drawQuad(glm::vec2 pos, glm::vec2 size, float rotation,
                  const Material& mat, glm::vec4 uvTransform,
                  VulkanContext& ctx, DescriptorManager& descMgr);

    void drawQuadCorners(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                         const Material& mat, glm::vec4 uvTransform,
                         VulkanContext& ctx, DescriptorManager& descMgr);

    // ── Legacy overloads — forward to Material-aware versions as Unlit ──────
    void drawQuad(glm::vec2 pos, glm::vec2 size, float rotation,
                  glm::vec4 color, glm::vec4 uvTransform,
                  VkImageView texture, VkSampler sampler,
                  VulkanContext& ctx, DescriptorManager& descMgr);

    void drawQuadCorners(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                         glm::vec4 color, glm::vec4 uvTransform,
                         VkImageView texture, VkSampler sampler,
                         VulkanContext& ctx, DescriptorManager& descMgr);

    // Flush all pending quads — call once per frame
    void flush(VkCommandBuffer cmd, VulkanContext& ctx, DescriptorManager& descMgr);

    void updateFrameUBO(const glm::mat4& viewProj, float time, int frameIndex);

    // Access the pipeline for a given material kind (for integration/debug).
    Pipeline& pipeline(MaterialKind k = MaterialKind::Unlit) {
        return m_pipelines[(size_t)k];
    }

private:
    struct Batch {
        MaterialKind    kind;
        VkImageView     texture;
        VkSampler       sampler;
        VkDescriptorSet texSet = VK_NULL_HANDLE;
        glm::vec4       tint;
        glm::vec4       params;
        glm::vec4       uvTransform;
        uint32_t        indexStart;
        uint32_t        indexCount;
    };

    void buildIndexBuffer(VulkanContext& ctx, BufferManager& bufMgr);
    void pushBatch(const Material& mat, glm::vec4 uvTransform,
                   uint32_t quadIdx,
                   VulkanContext& ctx, DescriptorManager& descMgr);

    std::array<Pipeline, (size_t)MaterialKind::Count> m_pipelines{};
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
