#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "TextureManager.h"
#include "VulkanContext.h"
#include "BufferManager.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <stdexcept>
#include <cstring>

void TextureManager::init(VulkanContext&, BufferManager& bufMgr) {
    m_allocator = bufMgr.allocator();
}

void TextureManager::shutdown(VulkanContext&) {}

Texture TextureManager::loadFromFile(VulkanContext& ctx, BufferManager& bufMgr,
                                     const std::string& path) {
    int w, h, ch;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!pixels) throw std::runtime_error("Failed to load texture: " + path);

    Texture tex = createTexture(ctx, bufMgr, pixels,
                                static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    stbi_image_free(pixels);
    return tex;
}

Texture TextureManager::createWhite1x1(VulkanContext& ctx, BufferManager& bufMgr) {
    uint8_t pixels[4] = {255, 255, 255, 255};
    return createTexture(ctx, bufMgr, pixels, 1, 1);
}

Texture TextureManager::createFromPixels(VulkanContext& ctx, BufferManager& bufMgr,
                                         const uint8_t* rgba, uint32_t w, uint32_t h) {
    return createTexture(ctx, bufMgr, rgba, w, h);
}

Texture TextureManager::createTexture(VulkanContext& ctx, BufferManager& bufMgr,
                                      const uint8_t* pixels, uint32_t w, uint32_t h) {
    VkDeviceSize imageSize = w * h * 4;

    // Staging buffer
    Buffer staging = bufMgr.createDynamicBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    memcpy(staging.mapped, pixels, imageSize);

    // Create image
    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.extent        = {w, h, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.format        = VK_FORMAT_R8G8B8A8_SRGB;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    Texture tex;
    tex.width  = w;
    tex.height = h;
    vmaCreateImage(bufMgr.allocator(), &ici, &aci, &tex.image, &tex.allocation, nullptr);

    // Transition + copy
    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

    // Undefined → TransferDst
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = tex.image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.layerCount     = 1;
    barrier.srcAccessMask                   = 0;
    barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent                 = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging.handle, tex.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // TransferDst → ShaderReadOnly
    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    ctx.endSingleTimeCommands(cmd);
    bufMgr.destroyBuffer(staging);

    // Image view
    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = tex.image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = VK_FORMAT_R8G8B8A8_SRGB;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.layerCount     = 1;
    vkCreateImageView(ctx.device(), &vci, nullptr, &tex.view);

    tex.sampler = createSampler(ctx);
    return tex;
}

VkSampler TextureManager::createSampler(VulkanContext& ctx) {
    VkSamplerCreateInfo si{};
    si.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter               = VK_FILTER_LINEAR;
    si.minFilter               = VK_FILTER_LINEAR;
    si.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.anisotropyEnable        = VK_TRUE;
    si.maxAnisotropy           = 16.f;
    si.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    si.unnormalizedCoordinates = VK_FALSE;
    si.compareEnable           = VK_FALSE;
    si.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VkSampler sampler;
    vkCreateSampler(ctx.device(), &si, nullptr, &sampler);
    return sampler;
}

void TextureManager::destroyTexture(VulkanContext& ctx, Texture& tex) {
    vkDestroySampler(ctx.device(), tex.sampler, nullptr);
    vkDestroyImageView(ctx.device(), tex.view, nullptr);
    vmaDestroyImage(m_allocator, tex.image, tex.allocation);
    tex = {};
}
