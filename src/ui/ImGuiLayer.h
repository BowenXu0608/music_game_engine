#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

class VulkanContext;
class Swapchain;

class ImGuiLayer {
public:
    void init(GLFWwindow* window, VulkanContext& ctx, VkRenderPass renderPass);
    void shutdown();

    void beginFrame();
    void endFrame();
    void render(VkCommandBuffer cmd);

private:
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
};
