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

    // `srgb` chooses the image/view format. Color content (baseColor,
    // backgrounds, UI) must stay sRGB so the hardware linearizes on sample.
    // Data textures (normal maps, metallic-roughness) MUST be linear
    // (srgb=false → R8G8B8A8_UNORM) or the encoded vectors get gamma-warped.
    Texture loadFromFile(VulkanContext& ctx, BufferManager& bufMgr,
                         const std::string& path, bool srgb = true);
    Texture createWhite1x1(VulkanContext& ctx, BufferManager& bufMgr);
    // 1x1 (128,128,255,255) linear texture = a flat tangent-space normal
    // (0,0,1). Bound at Set 1 binding 1 when a PBR material has no normal map.
    Texture createFlatNormal1x1(VulkanContext& ctx, BufferManager& bufMgr);
    Texture createFromPixels(VulkanContext& ctx, BufferManager& bufMgr,
                             const uint8_t* rgba, uint32_t w, uint32_t h,
                             bool srgb = true);
    void    destroyTexture(VulkanContext& ctx, Texture& tex);

    VmaAllocator allocator() const { return m_allocator; }

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    Texture createTexture(VulkanContext& ctx, BufferManager& bufMgr,
                          const uint8_t* pixels, uint32_t w, uint32_t h,
                          bool srgb = true);
    VkSampler createSampler(VulkanContext& ctx);
};
