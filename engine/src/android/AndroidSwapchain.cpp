// ============================================================================
// Android implementation of Swapchain
// Replaces Swapchain.cpp for Android builds.
// Uses ANativeWindow_getWidth/Height instead of glfwGetFramebufferSize.
// ============================================================================
#include "renderer/vulkan/Swapchain.h"
#include "renderer/vulkan/VulkanContext.h"
#include <android/native_window.h>
#include <stdexcept>
#include <algorithm>
#include <limits>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MusicGame", __VA_ARGS__)

// Global ANativeWindow — shared with AndroidVulkanContext.cpp
extern void androidSetNativeWindow(ANativeWindow* win);
static ANativeWindow* g_androidSwapWindow = nullptr;
void androidSetSwapchainWindow(ANativeWindow* win) { g_androidSwapWindow = win; }

void Swapchain::init(VulkanContext& ctx, GLFWwindow* /*ignored*/, bool vsync) {
    m_vsync = vsync;
    create(ctx, nullptr);
    createImageViews(ctx);
}

void Swapchain::shutdown(VulkanContext& ctx) {
    cleanup(ctx);
}

void Swapchain::recreate(VulkanContext& ctx, GLFWwindow* /*ignored*/, bool vsync) {
    m_vsync = vsync;
    // On Android, window is never minimized to 0x0 in the same way
    vkDeviceWaitIdle(ctx.device());
    cleanup(ctx);
    create(ctx, nullptr);
    createImageViews(ctx);
}

void Swapchain::create(VulkanContext& ctx, GLFWwindow* /*ignored*/) {
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
    auto presMode = choosePresentMode(modes, m_vsync);
    m_extent      = chooseExtent(caps, nullptr);
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

    // We swapped the extent in chooseExtent() to match the displayed
    // landscape orientation. To prevent the system from rotating again,
    // request IDENTITY if it's supported; otherwise fall back to the
    // current transform (which means the visual rotation will still be
    // off, but at least the dimensions will be right).
    if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    } else {
        ci.preTransform = caps.currentTransform;
    }
    // Pick a supported composite alpha flag — not all devices support INHERIT
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const VkCompositeAlphaFlagBitsKHR preferred[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    };
    for (auto flag : preferred) {
        if (caps.supportedCompositeAlpha & flag) { compositeAlpha = flag; break; }
    }
    ci.compositeAlpha = compositeAlpha;
    ci.presentMode    = presMode;
    ci.clipped        = VK_TRUE;

    if (vkCreateSwapchainKHR(ctx.device(), &ci, nullptr, &m_swapchain) != VK_SUCCESS)
        throw std::runtime_error("Failed to create swapchain (Android)");

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &count, nullptr);
    m_images.resize(count);
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &count, m_images.data());

    LOGI("Swapchain created: %dx%d, %d images", m_extent.width, m_extent.height, count);
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
    // Prefer SRGB; fall back to R8G8B8A8 which is common on Android
    for (auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    for (auto& f : formats)
        if (f.format == VK_FORMAT_R8G8B8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return formats[0];
}

VkPresentModeKHR Swapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync) {
    if (vsync) return VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : modes)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& caps, GLFWwindow* /*ignored*/) {
    VkExtent2D ext;
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        ext = caps.currentExtent;
    } else {
        // Fallback: get dimensions from ANativeWindow
        int w = g_androidSwapWindow ? ANativeWindow_getWidth(g_androidSwapWindow)  : 1280;
        int h = g_androidSwapWindow ? ANativeWindow_getHeight(g_androidSwapWindow) : 720;
        ext = { static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    }

    LOGI("chooseExtent: caps.currentExtent=%dx%d, currentTransform=0x%x",
         ext.width, ext.height, caps.currentTransform);

    // On Android the surface buffer is allocated in the device's NATIVE
    // orientation (portrait for phones). When the activity is locked to
    // landscape, the system reports a 90 or 270 degree currentTransform
    // and expects the app to either (a) render rotated and pass that
    // transform back via preTransform so the compositor presents it
    // unchanged, or (b) swap the extent so the swapchain images match
    // the displayed orientation. We take approach (b) — swap the extent
    // to match what the user actually sees.
    const VkSurfaceTransformFlagsKHR rotate90or270 =
        VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
        VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    if (caps.currentTransform & rotate90or270) {
        std::swap(ext.width, ext.height);
        LOGI("chooseExtent: swapped for 90/270 rotation -> %dx%d",
             ext.width, ext.height);
    }

    // Belt-and-braces: this app is forced landscape, so width must be >= height.
    if (ext.height > ext.width) {
        std::swap(ext.width, ext.height);
        LOGI("chooseExtent: forced landscape swap -> %dx%d", ext.width, ext.height);
    }

    ext.width  = std::clamp(ext.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return ext;
}
