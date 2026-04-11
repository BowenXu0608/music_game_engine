#define VMA_IMPLEMENTATION
#include "BufferManager.h"
#include "VulkanContext.h"
#include <stdexcept>
#include <cstring>

void BufferManager::init(VulkanContext& ctx) {
    VmaAllocatorCreateInfo ai{};
    ai.physicalDevice = ctx.physicalDevice();
    ai.device         = ctx.device();
    ai.instance       = ctx.instance();
    // VMA_VULKAN_VERSION is set per-target in CMake. On Android we restrict
    // it to 1000000 (Vulkan 1.0); on desktop it stays at the VMA default
    // (1003000 = Vulkan 1.3). The vulkanApiVersion field passed to VMA must
    // not exceed VMA_VULKAN_VERSION or the allocator constructor asserts.
    // Note: VK_API_VERSION_1_2 expands to a (uint32_t) cast, which is not a
    // valid expression in #if, so we compare against the raw value 4198400.
#if defined(VMA_VULKAN_VERSION) && VMA_VULKAN_VERSION < 4198400
    ai.vulkanApiVersion = VK_API_VERSION_1_0;
#else
    ai.vulkanApiVersion = VK_API_VERSION_1_2;
#endif

    if (vmaCreateAllocator(&ai, &m_allocator) != VK_SUCCESS)
        throw std::runtime_error("Failed to create VMA allocator");
}

void BufferManager::shutdown() {
    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
}

Buffer BufferManager::createDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    Buffer buf;
    buf.size = size;
    if (vmaCreateBuffer(m_allocator, &bci, &aci, &buf.handle, &buf.allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("Failed to create device buffer");
    return buf;
}

Buffer BufferManager::createDynamicBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocInfo{};
    Buffer buf;
    buf.size = size;
    if (vmaCreateBuffer(m_allocator, &bci, &aci, &buf.handle, &buf.allocation, &allocInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to create dynamic buffer");
    buf.mapped = allocInfo.pMappedData;
    return buf;
}

void BufferManager::uploadToBuffer(VulkanContext& ctx, Buffer& dst,
                                   const void* data, VkDeviceSize size) {
    // Create staging buffer
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer      stagingBuf;
    VmaAllocation stagingAlloc;
    VmaAllocationInfo stagingInfo{};
    vmaCreateBuffer(m_allocator, &bci, &aci, &stagingBuf, &stagingAlloc, &stagingInfo);

    memcpy(stagingInfo.pMappedData, data, size);

    // Copy staging → device
    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, stagingBuf, dst.handle, 1, &region);
    ctx.endSingleTimeCommands(cmd);

    vmaDestroyBuffer(m_allocator, stagingBuf, stagingAlloc);
}

void BufferManager::destroyBuffer(Buffer& buf) {
    if (buf.handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, buf.handle, buf.allocation);
        buf.handle     = VK_NULL_HANDLE;
        buf.allocation = VK_NULL_HANDLE;
        buf.mapped     = nullptr;
    }
}
