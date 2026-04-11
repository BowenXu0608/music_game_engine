#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>

class VulkanContext;
class BufferManager;
struct VmaAllocator_T;

struct Texture {
    VkImage       image      = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView   view       = VK_NULL_HANDLE;
    VkSampler     sampler    = VK_NULL_HANDLE;
    uint32_t      width      = 0;
    uint32_t      height     = 0;
};

class TextureManager {
public:
    void init(VulkanContext& ctx, BufferManager& bufMgr);
    void shutdown(VulkanContext& ctx);

    Texture loadFromFile(VulkanContext& ctx, BufferManager& bufMgr,
                         const std::string& path);
    Texture createWhite1x1(VulkanContext& ctx, BufferManager& bufMgr);
    Texture createFromPixels(VulkanContext& ctx, BufferManager& bufMgr,
                             const uint8_t* rgba, uint32_t w, uint32_t h);
    void    destroyTexture(VulkanContext& ctx, Texture& tex);

    VmaAllocator allocator() const { return m_allocator; }

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    Texture createTexture(VulkanContext& ctx, BufferManager& bufMgr,
                          const uint8_t* pixels, uint32_t w, uint32_t h);
    VkSampler createSampler(VulkanContext& ctx);
};
