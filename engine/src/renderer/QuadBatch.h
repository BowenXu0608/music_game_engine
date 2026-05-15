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
#include <map>
#include <utility>

class VulkanContext;

static constexpr uint32_t MAX_QUADS = 8192;
static constexpr uint32_t QUAD_VERTS = MAX_QUADS * 4;
static constexpr uint32_t QUAD_INDICES = MAX_QUADS * 6;

class QuadBatch {
public:
    void init(VulkanContext& ctx, BufferManager& bufMgr,
              DescriptorManager& descMgr, VkRenderPass renderPass,
              const std::string& shaderDir,
              VkImageView whiteView, VkSampler whiteSampler,
              VkImageView flatNormalView, VkSampler flatNormalSampler);
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

    void updateFrameUBO(const FrameUBO& ubo, int frameIndex);

    // Access the pipeline for a given material kind (for integration/debug).
    Pipeline& pipeline(MaterialKind k = MaterialKind::Unlit) {
        return m_pipelines[(size_t)k];
    }

    // Compile (if needed) and build a pipeline for a user-authored fragment
    // shader. The .frag source is passed; the method caches by that path.
    // Returns VK_NULL_HANDLE when the shader fails to compile — caller should
    // gracefully fall back to Unlit. `errorOut` (when non-null) is filled
    // with glslc's stderr so the editor can surface compile errors.
    VkPipeline getOrBuildCustomPipeline(VulkanContext& ctx,
                                         const std::string& fragPath,
                                         std::string* errorOut = nullptr);

private:
    struct Batch {
        MaterialClass   cls = MaterialClass::SpecialEffect;
        MaterialKind    kind;
        VkPipeline      customPipe = VK_NULL_HANDLE;   // overrides kind when set
        VkImageView     texture;
        VkSampler       sampler;
        VkImageView     normalTex = VK_NULL_HANDLE;    // PBR only
        VkDescriptorSet texSet = VK_NULL_HANDLE;
        glm::vec4       tint;        // PBR: baseColor
        glm::vec4       params;      // PBR: emissive rgb
        glm::vec4       uvTransform; // PBR: mrp
        uint32_t        indexStart;
        uint32_t        indexCount;
    };

    void buildIndexBuffer(VulkanContext& ctx, BufferManager& bufMgr);
    void pushBatch(const Material& mat, glm::vec4 uvTransform,
                   uint32_t quadIdx,
                   VulkanContext& ctx, DescriptorManager& descMgr);
    VkDescriptorSet resolvePbrTexSet(VkImageView baseV, VkSampler baseS,
                                     VkImageView normV, VkSampler normS,
                                     VulkanContext& ctx, DescriptorManager& descMgr);

    std::array<Pipeline, (size_t)MaterialKind::Count> m_pipelines{};
    Pipeline         m_pbrPipeline{};
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // Template bits captured at init() so custom pipelines can be rebuilt
    // lazily with the same layout + vertex inputs + render pass as the
    // built-in pipelines.
    VkRenderPass     m_renderPass      = VK_NULL_HANDLE;
    std::string      m_shaderDir;     // for quad.vert.spv lookup
    // Cache of custom fragment shaders → pipeline. Keyed by the user's .frag
    // path (not .spv) so we can re-resolve through the compiler on changes.
    std::unordered_map<std::string, Pipeline> m_customPipelines;

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
    // PBR 2-image sets keyed by (baseColor view, normal view).
    std::map<std::pair<VkImageView, VkImageView>, VkDescriptorSet> m_pbrTexSetCache;

    VkImageView m_whiteView         = VK_NULL_HANDLE;
    VkSampler   m_whiteSampler      = VK_NULL_HANDLE;
    VkImageView m_flatNormalView    = VK_NULL_HANDLE;
    VkSampler   m_flatNormalSampler = VK_NULL_HANDLE;

    int m_currentFrame = 0;
};
