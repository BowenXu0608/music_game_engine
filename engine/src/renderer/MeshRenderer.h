#pragma once
#include "RenderTypes.h"
#include "Material.h"
#include "vulkan/BufferManager.h"
#include "vulkan/DescriptorManager.h"
#include "vulkan/Pipeline.h"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>

class VulkanContext;

struct Mesh {
    Buffer vertexBuffer;
    Buffer indexBuffer;
    uint32_t indexCount = 0;
};

class MeshRenderer {
public:
    // whiteView/whiteSampler are the fallback used whenever a Material arrives
    // with a null texture or sampler. Stored so callers don't have to pass them
    // on every drawMesh.
    void init(VulkanContext& ctx, BufferManager& bufMgr,
              DescriptorManager& descMgr, VkRenderPass renderPass,
              const std::string& shaderDir,
              VkImageView whiteView, VkSampler whiteSampler);
    void shutdown(VulkanContext& ctx, BufferManager& bufMgr);

    Mesh createMesh(VulkanContext& ctx, BufferManager& bufMgr,
                    const std::vector<MeshVertex>& verts,
                    const std::vector<uint32_t>& indices);
    void destroyMesh(BufferManager& bufMgr, Mesh& mesh);

    void updateMesh(VulkanContext& ctx, BufferManager& bufMgr, Mesh& mesh,
                    const std::vector<MeshVertex>& verts,
                    const std::vector<uint32_t>& indices);

    // Material-aware draw. Selects the fragment shader from mat.kind and passes
    // mat.tint/params through push constants. mat.texture/sampler fall back to
    // the white 1x1 texture registered at init.
    void drawMesh(const Mesh& mesh, const glm::mat4& model, const Material& mat,
                  VulkanContext& ctx, DescriptorManager& descMgr);

    // Legacy tint-only overload — forwarded as Unlit with the white texture.
    // Kept so existing call sites that don't care about MaterialKind still work.
    void drawMesh(const Mesh& mesh, const glm::mat4& model, glm::vec4 tint);

    void flush(VkCommandBuffer cmd, int frameIndex);

    void updateFrameUBO(const glm::mat4& viewProj, float time, int frameIndex,
                        BufferManager& bufMgr);

    // Compile (if needed) and build a pipeline for a user-authored 3D
    // fragment shader. Mirrors QuadBatch::getOrBuildCustomPipeline — see
    // that doc. Returns VK_NULL_HANDLE if compile fails; caller should fall
    // back to Unlit.
    VkPipeline getOrBuildCustomPipeline(VulkanContext& ctx,
                                         const std::string& fragPath,
                                         std::string* errorOut = nullptr);

private:
    struct DrawEntry {
        const Mesh*     mesh;
        glm::mat4       model;
        MaterialKind    kind;
        VkPipeline      customPipe = VK_NULL_HANDLE;   // overrides kind when set
        glm::vec4       tint;
        glm::vec4       params;
        glm::vec4       uvTransform;
        VkDescriptorSet texSet;
    };

    VkDescriptorSet resolveTexSet(VkImageView view, VkSampler sampler,
                                  VulkanContext& ctx, DescriptorManager& descMgr);

    std::array<Pipeline, (size_t)MaterialKind::Count> m_pipelines{};
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    std::vector<Buffer>          m_ubos;
    std::vector<VkDescriptorSet> m_frameSets;
    std::vector<DrawEntry>       m_queue;

    // Captured at init() so we can lazily build pipelines for user-authored
    // mesh fragment shaders using the same layout + render pass.
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::string  m_shaderDir;
    std::unordered_map<std::string, Pipeline> m_customPipelines;

    VkImageView m_whiteView    = VK_NULL_HANDLE;
    VkSampler   m_whiteSampler = VK_NULL_HANDLE;

    // Cache of (texture view → descriptor set) so repeated draws with the same
    // texture don't keep allocating. Cleared on shutdown.
    std::unordered_map<VkImageView, VkDescriptorSet> m_texSetCache;

    int m_currentFrame = 0;
};
