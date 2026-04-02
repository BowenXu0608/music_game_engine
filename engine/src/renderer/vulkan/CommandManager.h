#pragma once
#include <vulkan/vulkan.h>
#include <vector>

class VulkanContext;

class CommandManager {
public:
    void init(VulkanContext& ctx, uint32_t count);
    void shutdown(VulkanContext& ctx);

    VkCommandBuffer buffer(uint32_t index) const { return m_buffers[index]; }

    VkCommandBuffer begin(uint32_t index);
    void            end(uint32_t index);

private:
    std::vector<VkCommandBuffer> m_buffers;
};
