#include "CommandManager.h"
#include "VulkanContext.h"
#include <stdexcept>

void CommandManager::init(VulkanContext& ctx, uint32_t count) {
    m_buffers.resize(count);
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = ctx.commandPool();
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = count;
    if (vkAllocateCommandBuffers(ctx.device(), &ai, m_buffers.data()) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate command buffers");
}

void CommandManager::shutdown(VulkanContext& ctx) {
    vkFreeCommandBuffers(ctx.device(), ctx.commandPool(),
                         static_cast<uint32_t>(m_buffers.size()), m_buffers.data());
    m_buffers.clear();
}

VkCommandBuffer CommandManager::begin(uint32_t index) {
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_buffers[index], &bi);
    return m_buffers[index];
}

void CommandManager::end(uint32_t index) {
    vkEndCommandBuffer(m_buffers[index]);
}
