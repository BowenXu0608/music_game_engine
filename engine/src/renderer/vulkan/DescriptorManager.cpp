#include "DescriptorManager.h"
#include "VulkanContext.h"
#include <array>
#include <stdexcept>

void DescriptorManager::init(VulkanContext& ctx) {
    // Pool
    // PBR texture sets consume 2 image-sampler descriptors each (baseColor +
    // normal), so the image-sampler budget and maxSets are bumped.
    std::array<VkDescriptorPoolSize, 2> poolSizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         64},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512},
    }};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    pi.pPoolSizes    = poolSizes.data();
    pi.maxSets       = 512;
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

    // Set 1 layout: 2 combined image samplers — binding 0 = baseColor,
    // binding 1 = normal map. Non-PBR pipelines only declare binding 0 in
    // their shaders, so leaving binding 1 unwritten on their sets is fine.
    std::array<VkDescriptorSetLayoutBinding, 2> samplerBindings{};
    samplerBindings[0].binding         = 0;
    samplerBindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBindings[0].descriptorCount = 1;
    samplerBindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerBindings[1].binding         = 1;
    samplerBindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBindings[1].descriptorCount = 1;
    samplerBindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo samplerLCI{};
    samplerLCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    samplerLCI.bindingCount = static_cast<uint32_t>(samplerBindings.size());
    samplerLCI.pBindings    = samplerBindings.data();
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

VkDescriptorSet DescriptorManager::allocateTextureSet(
    VulkanContext& ctx,
    VkImageView baseView, VkSampler baseSamp,
    VkImageView normView, VkSampler normSamp) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_textureLayout;

    VkDescriptorSet set;
    vkAllocateDescriptorSets(ctx.device(), &ai, &set);

    VkDescriptorImageInfo ii[2] = {
        {baseSamp, baseView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {normSamp, normView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkWriteDescriptorSet writes[2]{};
    for (int i = 0; i < 2; ++i) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = set;
        writes[i].dstBinding      = static_cast<uint32_t>(i);
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo      = &ii[i];
    }
    vkUpdateDescriptorSets(ctx.device(), 2, writes, 0, nullptr);
    return set;
}

void DescriptorManager::freeAll(VulkanContext& ctx) {
    vkResetDescriptorPool(ctx.device(), m_pool, 0);
}
