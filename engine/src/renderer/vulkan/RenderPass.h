#pragma once
#include <vulkan/vulkan.h>

class VulkanContext;

class RenderPass {
public:
    void init(VulkanContext& ctx, VkFormat colorFormat);
    void shutdown(VulkanContext& ctx);

    VkRenderPass handle() const { return m_renderPass; }

    void begin(VkCommandBuffer cmd, VkFramebuffer fb,
               VkExtent2D extent, float r = 0.f, float g = 0.f,
               float b = 0.f, float a = 1.f);
    void end(VkCommandBuffer cmd);

private:
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
};
