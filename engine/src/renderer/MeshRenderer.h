#pragma once
#include "RenderTypes.h"
#include "vulkan/BufferManager.h"
#include "vulkan/DescriptorManager.h"
#include "vulkan/Pipeline.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;

struct Mesh {
    Buffer vertexBuffer;
    Buffer indexBuffer;
    uint32_t indexCount = 0;
};

class MeshRenderer {
public:
    void init(VulkanContext& ctx, BufferManager& bufMgr,
              DescriptorManager& descMgr, VkRenderPass renderPass,
              const std::string& shaderDir);
    void shutdown(VulkanContext& ctx, BufferManager& bufMgr);

    Mesh createMesh(VulkanContext& ctx, BufferManager& bufMgr,
                    const std::vector<MeshVertex>& verts,
                    const std::vector<uint32_t>& indices);
    void destroyMesh(BufferManager& bufMgr, Mesh& mesh);

    // Re-upload vertex/index data into an existing mesh's buffers.
    void updateMesh(VulkanContext& ctx, BufferManager& bufMgr, Mesh& mesh,
                    const std::vector<MeshVertex>& verts,
                    const std::vector<uint32_t>& indices);

    // Queue a mesh draw — flushed in order
    void drawMesh(const Mesh& mesh, const glm::mat4& model, glm::vec4 tint);

    void flush(VkCommandBuffer cmd, int frameIndex);

    void updateFrameUBO(const glm::mat4& viewProj, float time, int frameIndex,
                        BufferManager& bufMgr);

private:
    struct DrawEntry {
        const Mesh* mesh;
        glm::mat4   model;
        glm::vec4   tint;
    };

    Pipeline         m_pipeline;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    std::vector<Buffer>          m_ubos;
    std::vector<VkDescriptorSet> m_frameSets;
    std::vector<DrawEntry> m_queue;
    int m_currentFrame = 0;
};
