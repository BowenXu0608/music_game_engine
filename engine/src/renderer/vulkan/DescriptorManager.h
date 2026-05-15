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
    // Set 1 has 2 bindings: 0 = baseColor, 1 = normal. The single-image
    // overload writes binding 0 only (legacy/effect/line/particle shaders
    // never sample binding 1, so it stays validation-clean). PBR draws use
    // the 2-image overload.
    VkDescriptorSet allocateTextureSet(VulkanContext& ctx, VkImageView view, VkSampler sampler);
    VkDescriptorSet allocateTextureSet(VulkanContext& ctx,
                                       VkImageView baseView, VkSampler baseSamp,
                                       VkImageView normView, VkSampler normSamp);

    void freeAll(VulkanContext& ctx);

private:
    VkDescriptorPool      m_pool            = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_frameUBOLayout  = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureLayout   = VK_NULL_HANDLE;
};
