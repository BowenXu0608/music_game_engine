#include "GifPlayer.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/BufferManager.h"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <stb_image.h>
#include <cstring>

bool GifPlayer::load(const std::string& path, VulkanContext& ctx,
                     BufferManager& bufMgr) {
    // Read file into memory
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<stbi_uc> fileData(fileSize);
    fread(fileData.data(), 1, fileSize, f);
    fclose(f);

    // Decode all GIF frames
    int w = 0, h = 0, frameCount = 0, comp = 0;
    int* delays = nullptr;
    stbi_uc* pixels = stbi_load_gif_from_memory(
        fileData.data(), static_cast<int>(fileSize),
        &delays, &w, &h, &frameCount, &comp, STBI_rgb_alpha);

    if (!pixels || frameCount <= 0) {
        stbi_image_free(pixels);
        return false;
    }

    m_width  = static_cast<uint32_t>(w);
    m_height = static_cast<uint32_t>(h);
    size_t frameBytes = static_cast<size_t>(w) * h * 4;

    m_frames.reserve(frameCount);
    for (int i = 0; i < frameCount; ++i) {
        Frame frame;
        // stbi_load_gif_from_memory lays frames sequentially in memory
        const stbi_uc* framePixels = pixels + i * frameBytes;

        // Upload as Vulkan texture via staging
        VkDeviceSize imageSize = frameBytes;
        Buffer staging = bufMgr.createDynamicBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        memcpy(staging.mapped, framePixels, imageSize);

        VkImageCreateInfo ici{};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.extent        = {m_width, m_height, 1};
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

        frame.tex.width  = m_width;
        frame.tex.height = m_height;
        vmaCreateImage(bufMgr.allocator(), &ici, &aci,
                       &frame.tex.image, &frame.tex.allocation, nullptr);

        VkCommandBuffer cmd = ctx.beginSingleTimeCommands();

        VkImageMemoryBarrier barrier{};
        barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                       = frame.tex.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask               = 0;
        barrier.dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent                 = {m_width, m_height, 1};
        vkCmdCopyBufferToImage(cmd, staging.handle, frame.tex.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        ctx.endSingleTimeCommands(cmd);
        bufMgr.destroyBuffer(staging);

        VkImageViewCreateInfo vci{};
        vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image                           = frame.tex.image;
        vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        vci.format                          = VK_FORMAT_R8G8B8A8_SRGB;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.layerCount     = 1;
        vkCreateImageView(ctx.device(), &vci, nullptr, &frame.tex.view);

        VkSamplerCreateInfo si{};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_LINEAR;
        si.minFilter    = VK_FILTER_LINEAR;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        vkCreateSampler(ctx.device(), &si, nullptr, &frame.tex.sampler);

        frame.desc  = ImGui_ImplVulkan_AddTexture(frame.tex.sampler, frame.tex.view,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        // GIF delays are in centiseconds; convert to seconds
        frame.delay = delays ? (delays[i] * 0.01f) : 0.1f;
        if (frame.delay <= 0.f) frame.delay = 0.1f;

        m_frames.push_back(std::move(frame));
    }

    stbi_image_free(pixels);
    m_currentFrame = 0;
    m_elapsed      = 0.f;
    return true;
}

void GifPlayer::update(float dt) {
    if (m_frames.empty()) return;
    m_elapsed += dt;
    while (m_elapsed >= m_frames[m_currentFrame].delay) {
        m_elapsed -= m_frames[m_currentFrame].delay;
        m_currentFrame = (m_currentFrame + 1) % static_cast<int>(m_frames.size());
    }
}

VkDescriptorSet GifPlayer::currentFrame() const {
    if (m_frames.empty()) return VK_NULL_HANDLE;
    return m_frames[m_currentFrame].desc;
}

void GifPlayer::unload(VulkanContext& ctx, BufferManager& bufMgr) {
    for (auto& frame : m_frames) {
        vkDestroySampler(ctx.device(), frame.tex.sampler, nullptr);
        vkDestroyImageView(ctx.device(), frame.tex.view, nullptr);
        vmaDestroyImage(bufMgr.allocator(), frame.tex.image, frame.tex.allocation);
    }
    m_frames.clear();
    m_currentFrame = 0;
    m_elapsed      = 0.f;
}
