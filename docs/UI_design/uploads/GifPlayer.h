#pragma once
#include "renderer/vulkan/TextureManager.h"
#include "ui/ImGuiLayer.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>

class VulkanContext;
class BufferManager;

// Decodes an animated GIF into per-frame Vulkan textures and advances
// the frame timer each update(). Call currentFrame() to get the ImGui
// descriptor set for the frame that should be displayed right now.
class GifPlayer {
public:
    // Load all frames from a GIF file. Returns false on failure.
    bool load(const std::string& path, VulkanContext& ctx,
              BufferManager& bufMgr, ImGuiLayer& imgui);

    // Advance the frame timer by dt seconds.
    void update(float dt);

    // ImGui descriptor set for the current frame (VK_NULL_HANDLE if not loaded).
    VkDescriptorSet currentFrame() const;

    void unload(VulkanContext& ctx, BufferManager& bufMgr);
    bool isLoaded() const { return !m_frames.empty(); }
    uint32_t width()  const { return m_width; }
    uint32_t height() const { return m_height; }

private:
    struct Frame {
        Texture         tex;
        VkDescriptorSet desc  = VK_NULL_HANDLE;
        float           delay = 0.1f; // seconds
    };

    std::vector<Frame> m_frames;
    int   m_currentFrame = 0;
    float m_elapsed      = 0.f;
    uint32_t m_width  = 0;
    uint32_t m_height = 0;
};
