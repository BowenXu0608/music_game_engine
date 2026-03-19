#pragma once
#include <vulkan/vulkan.h>
#include <vector>

class VulkanContext;

class DescriptorManager {
public:
    void init(VulkanContext& ctx);
    void shutdown(VulkanContext& ctx);

    // Set 0: per-frame UBO
    VkDescriptorSetLayout frameUBOLayout()    const { return m_frameUBOLayout; }
    // Set 1: combined image sampler
    VkDescriptorSetLayout textureLayout()     const { return m_textureLayout; }

    VkDescriptorSet allocateFrameSet(VulkanContext& ctx, VkBuffer ubo, VkDeviceSize size);
    VkDescriptorSet allocateTextureSet(VulkanContext& ctx, VkImageView view, VkSampler sampler);

    void freeAll(VulkanContext& ctx);

private:
    VkDescriptorPool      m_pool            = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_frameUBOLayout  = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureLayout   = VK_NULL_HANDLE;
};
