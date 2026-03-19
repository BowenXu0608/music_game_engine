#include "DescriptorManager.h"
#include "VulkanContext.h"
#include <array>
#include <stdexcept>

void DescriptorManager::init(VulkanContext& ctx) {
    // Pool
    std::array<VkDescriptorPoolSize, 2> poolSizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         64},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256},
    }};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    pi.pPoolSizes    = poolSizes.data();
    pi.maxSets       = 320;
    pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    if (vkCreateDescriptorPool(ctx.device(), &pi, nullptr, &m_pool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");

    // Set 0 layout: UBO
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding         = 0;
    uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo uboLCI{};
    uboLCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    uboLCI.bindingCount = 1;
    uboLCI.pBindings    = &uboBinding;
    vkCreateDescriptorSetLayout(ctx.device(), &uboLCI, nullptr, &m_frameUBOLayout);

    // Set 1 layout: sampler
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding         = 0;
    samplerBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo samplerLCI{};
    samplerLCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    samplerLCI.bindingCount = 1;
    samplerLCI.pBindings    = &samplerBinding;
    vkCreateDescriptorSetLayout(ctx.device(), &samplerLCI, nullptr, &m_textureLayout);
}

void DescriptorManager::shutdown(VulkanContext& ctx) {
    vkDestroyDescriptorSetLayout(ctx.device(), m_frameUBOLayout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device(), m_textureLayout, nullptr);
    vkDestroyDescriptorPool(ctx.device(), m_pool, nullptr);
}

VkDescriptorSet DescriptorManager::allocateFrameSet(VulkanContext& ctx,
                                                     VkBuffer ubo, VkDeviceSize size) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_frameUBOLayout;

    VkDescriptorSet set;
    vkAllocateDescriptorSets(ctx.device(), &ai, &set);

    VkDescriptorBufferInfo bi{ubo, 0, size};
    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo     = &bi;
    vkUpdateDescriptorSets(ctx.device(), 1, &write, 0, nullptr);
    return set;
}

VkDescriptorSet DescriptorManager::allocateTextureSet(VulkanContext& ctx,
                                                       VkImageView view, VkSampler sampler) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_textureLayout;

    VkDescriptorSet set;
    vkAllocateDescriptorSets(ctx.device(), &ai, &set);

    VkDescriptorImageInfo ii{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo      = &ii;
    vkUpdateDescriptorSets(ctx.device(), 1, &write, 0, nullptr);
    return set;
}

void DescriptorManager::freeAll(VulkanContext& ctx) {
    vkResetDescriptorPool(ctx.device(), m_pool, 0);
}
