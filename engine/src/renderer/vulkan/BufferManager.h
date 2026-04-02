#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstddef>

class VulkanContext;

struct Buffer {
    VkBuffer      handle     = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void*         mapped     = nullptr;   // non-null if persistently mapped
    VkDeviceSize  size       = 0;
};

class BufferManager {
public:
    void init(VulkanContext& ctx);
    void shutdown();

    // GPU-only (device local) — upload via staging
    Buffer createDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage);

    // CPU→GPU (persistently mapped, for dynamic VBOs / UBOs)
    Buffer createDynamicBuffer(VkDeviceSize size, VkBufferUsageFlags usage);

    // Staging helper — creates host-visible buffer, copies data, destroys it
    void uploadToBuffer(VulkanContext& ctx, Buffer& dst,
                        const void* data, VkDeviceSize size);

    void destroyBuffer(Buffer& buf);

    VmaAllocator allocator() const { return m_allocator; }

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
};
