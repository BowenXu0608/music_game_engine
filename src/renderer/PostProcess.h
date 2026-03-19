#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;
class BufferManager;

struct BloomImage {
    VkImage        image      = VK_NULL_HANDLE;
    VkImageView    view       = VK_NULL_HANDLE;
    VkDeviceMemory memory     = VK_NULL_HANDLE;
    VkSampler      sampler    = VK_NULL_HANDLE;
    uint32_t       width      = 0;
    uint32_t       height     = 0;
};

class PostProcess {
public:
    static constexpr int BLOOM_MIPS = 5;

    void init(VulkanContext& ctx, uint32_t width, uint32_t height,
              VkFormat sceneFormat, VkRenderPass swapRenderPass, const std::string& shaderDir);
    void shutdown(VulkanContext& ctx);
    void resize(VulkanContext& ctx, uint32_t width, uint32_t height);

    // Run bloom compute passes on the scene image, result in m_bloomMips[0]
    void render(VkCommandBuffer cmd, VulkanContext& ctx,
                VkImageView sceneView, VkSampler sceneSampler);

    // Offscreen scene render target
    VkImage        sceneImage()   const { return m_sceneImage; }
    VkImageView    sceneView()    const { return m_sceneImageView; }
    VkFramebuffer  sceneFramebuffer() const { return m_sceneFramebuffer; }
    VkRenderPass   sceneRenderPass()  const { return m_sceneRenderPass; }

    // Bloom result for composite
    VkImageView    bloomView()    const { return m_bloomMips[0].view; }
    VkSampler      bloomSampler() const { return m_bloomMips[0].sampler; }

    // Composite descriptor set (scene + bloom)
    VkDescriptorSet compositeSet() const { return m_compositeSet; }
    VkPipeline      compositePipeline() const { return m_compositePipeline; }
    VkPipelineLayout compositePipelineLayout() const { return m_compositePipelineLayout; }

    float bloomStrength = 0.8f;

private:
    void createSceneTarget(VulkanContext& ctx, uint32_t w, uint32_t h, VkFormat fmt);
    void createBloomMips(VulkanContext& ctx, uint32_t w, uint32_t h);
    void createComputePipelines(VulkanContext& ctx, const std::string& shaderDir);
    void createCompositePipeline(VulkanContext& ctx, VkRenderPass swapRenderPass,
                                  const std::string& shaderDir);
    void createDescriptorPool(VulkanContext& ctx);
    void updateDescriptors(VulkanContext& ctx);
    void destroySceneTarget(VulkanContext& ctx);
    void destroyBloomMips(VulkanContext& ctx);

    BloomImage createBloomImage(VulkanContext& ctx, uint32_t w, uint32_t h);
    void destroyBloomImage(VulkanContext& ctx, BloomImage& img);
    VkShaderModule loadShader(VulkanContext& ctx, const std::string& path);

    // Scene render target
    VkImage        m_sceneImage       = VK_NULL_HANDLE;
    VkImageView    m_sceneImageView   = VK_NULL_HANDLE;
    VkDeviceMemory m_sceneMemory      = VK_NULL_HANDLE;
    VkSampler      m_sceneSampler     = VK_NULL_HANDLE;
    VkFramebuffer  m_sceneFramebuffer = VK_NULL_HANDLE;
    VkRenderPass   m_sceneRenderPass  = VK_NULL_HANDLE;
    VkFormat       m_sceneFormat      = VK_FORMAT_UNDEFINED;

    // Bloom mip chain
    BloomImage m_bloomMips[BLOOM_MIPS];

    // Compute pipelines
    VkPipeline       m_downsamplePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_computeLayout      = VK_NULL_HANDLE;
    VkPipeline       m_upsamplePipeline   = VK_NULL_HANDLE;

    // Compute descriptor sets (one per mip transition)
    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_downsampleSets;  // BLOOM_MIPS sets
    std::vector<VkDescriptorSet> m_upsampleSets;    // BLOOM_MIPS-1 sets
    VkDescriptorSetLayout m_computeSetLayout = VK_NULL_HANDLE;

    // Composite pipeline
    VkPipeline       m_compositePipeline       = VK_NULL_HANDLE;
    VkPipelineLayout m_compositePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_compositeSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_compositeSet       = VK_NULL_HANDLE;

    uint32_t m_width = 0, m_height = 0;
    std::string m_shaderDir;
};
