#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

class VulkanContext;
class Swapchain;

class ImGuiLayer {
public:
    void init(GLFWwindow* window, VulkanContext& ctx, VkRenderPass renderPass);
    void shutdown();

    void beginFrame();
    void endFrame();
    void render(VkCommandBuffer cmd);

    VkDescriptorSet addTexture(VkImageView view, VkSampler sampler);

    // Returns the Roboto font closest to targetSize (24/32/48/64).
    // Falls back to the default ImGui font if Roboto failed to load.
    ImFont* getLogoFont(float targetSize) const;

private:
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;

    // Roboto-Medium loaded at 4 sizes for smooth logo text
    static constexpr float k_logoFontSizes[4] = {24.f, 32.f, 48.f, 64.f};
    ImFont* m_logoFonts[4] = {};
};
