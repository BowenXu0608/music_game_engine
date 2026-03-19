#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>

class VulkanContext;

class Swapchain {
public:
    void init(VulkanContext& ctx, GLFWwindow* window);
    void shutdown(VulkanContext& ctx);
    void recreate(VulkanContext& ctx, GLFWwindow* window);

    VkSwapchainKHR           handle()       const { return m_swapchain; }
    VkFormat                 imageFormat()  const { return m_imageFormat; }
    VkExtent2D               extent()       const { return m_extent; }
    uint32_t                 imageCount()   const { return static_cast<uint32_t>(m_images.size()); }
    VkImageView              imageView(uint32_t i) const { return m_imageViews[i]; }
    VkFramebuffer            framebuffer(uint32_t i) const { return m_framebuffers[i]; }

    void createFramebuffers(VulkanContext& ctx, VkRenderPass renderPass);

private:
    void create(VulkanContext& ctx, GLFWwindow* window);
    void createImageViews(VulkanContext& ctx);
    void cleanup(VulkanContext& ctx);

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
    VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* window);

    VkSwapchainKHR           m_swapchain   = VK_NULL_HANDLE;
    VkFormat                 m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_extent      = {};
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkFramebuffer> m_framebuffers;
};
