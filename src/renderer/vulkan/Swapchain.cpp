#include "Swapchain.h"
#include "VulkanContext.h"
#include <stdexcept>
#include <algorithm>
#include <limits>

void Swapchain::init(VulkanContext& ctx, GLFWwindow* window) {
    create(ctx, window);
    createImageViews(ctx);
}

void Swapchain::shutdown(VulkanContext& ctx) {
    cleanup(ctx);
}

void Swapchain::recreate(VulkanContext& ctx, GLFWwindow* window) {
    // Handle minimization
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(window, &w, &h);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(ctx.device());
    cleanup(ctx);
    create(ctx, window);
    createImageViews(ctx);
}

void Swapchain::create(VulkanContext& ctx, GLFWwindow* window) {
    VkPhysicalDevice pd = ctx.physicalDevice();
    VkSurfaceKHR     surface = ctx.surface();

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps);

    uint32_t fmtCount = 0, modeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &modeCount, nullptr);

    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    std::vector<VkPresentModeKHR>   modes(modeCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtCount, formats.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &modeCount, modes.data());

    auto surfFmt  = chooseSurfaceFormat(formats);
    auto presMode = choosePresentMode(modes);
    m_extent      = chooseExtent(caps, window);
    m_imageFormat = surfFmt.format;

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
        imgCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface;
    ci.minImageCount    = imgCount;
    ci.imageFormat      = surfFmt.format;
    ci.imageColorSpace  = surfFmt.colorSpace;
    ci.imageExtent      = m_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    auto qf = ctx.queueFamilies();
    uint32_t queueIndices[] = { qf.graphics.value(), qf.present.value() };
    if (qf.graphics != qf.present) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = queueIndices;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform   = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = presMode;
    ci.clipped        = VK_TRUE;

    if (vkCreateSwapchainKHR(ctx.device(), &ci, nullptr, &m_swapchain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain");

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &count, nullptr);
    m_images.resize(count);
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &count, m_images.data());
}

void Swapchain::createImageViews(VulkanContext& ctx) {
    m_imageViews.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image                           = m_images[i];
        ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ci.format                          = m_imageFormat;
        ci.components                      = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                              VK_COMPONENT_SWIZZLE_IDENTITY,
                                              VK_COMPONENT_SWIZZLE_IDENTITY,
                                              VK_COMPONENT_SWIZZLE_IDENTITY};
        ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.baseMipLevel   = 0;
        ci.subresourceRange.levelCount     = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(ctx.device(), &ci, nullptr, &m_imageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image view");
    }
}

void Swapchain::createFramebuffers(VulkanContext& ctx, VkRenderPass renderPass) {
    m_framebuffers.resize(m_imageViews.size());
    for (size_t i = 0; i < m_imageViews.size(); ++i) {
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = renderPass;
        ci.attachmentCount = 1;
        ci.pAttachments    = &m_imageViews[i];
        ci.width           = m_extent.width;
        ci.height          = m_extent.height;
        ci.layers          = 1;

        if (vkCreateFramebuffer(ctx.device(), &ci, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create framebuffer");
    }
}

void Swapchain::cleanup(VulkanContext& ctx) {
    for (auto fb : m_framebuffers)
        vkDestroyFramebuffer(ctx.device(), fb, nullptr);
    m_framebuffers.clear();

    for (auto iv : m_imageViews)
        vkDestroyImageView(ctx.device(), iv, nullptr);
    m_imageViews.clear();

    vkDestroySwapchainKHR(ctx.device(), m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
}

VkSurfaceFormatKHR Swapchain::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return formats[0];
}

VkPresentModeKHR Swapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* window) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    VkExtent2D ext = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    ext.width  = std::clamp(ext.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return ext;
}
