#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>

class VulkanContext;

struct PipelineConfig {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    std::string vertShaderPath;
    std::string fragShaderPath;

    VkVertexInputBindingDescription                vertexBinding{};
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;

    VkPrimitiveTopology topology    = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool                primitiveRestart = false;
    bool                depthTest   = false;
    bool                depthWrite  = false;

    // Blend mode
    enum class Blend { None, Alpha, Additive } blend = Blend::Alpha;
};

class Pipeline {
public:
    void init(VulkanContext& ctx, const PipelineConfig& cfg);
    void shutdown(VulkanContext& ctx);

    VkPipeline       handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }

private:
    static std::vector<char> readSpirv(const std::string& path);
    VkShaderModule createShaderModule(VulkanContext& ctx, const std::vector<char>& code);

    VkPipeline       m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
    bool             m_ownsLayout = false;
};
