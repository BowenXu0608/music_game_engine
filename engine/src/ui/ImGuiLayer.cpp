#include "ImGuiLayer.h"
#include "renderer/vulkan/VulkanContext.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>
#include <cmath>

void ImGuiLayer::init(GLFWwindow* window, VulkanContext& ctx, VkRenderPass renderPass) {
    m_device = ctx.device();

    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 256;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create ImGui descriptor pool");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Load Roboto at multiple sizes for smooth logo text rendering
    const char* robotoPath = "../../third_party/imgui/misc/fonts/Roboto-Medium.ttf";
    for (int i = 0; i < 4; ++i) {
        m_logoFonts[i] = io.Fonts->AddFontFromFileTTF(robotoPath, k_logoFontSizes[i]);
        // Falls back to nullptr if file not found; getLogoFont() handles that
    }

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = ctx.instance();
    init_info.PhysicalDevice = ctx.physicalDevice();
    init_info.Device = ctx.device();
    init_info.QueueFamily = ctx.queueFamilies().graphics.value();
    init_info.Queue = ctx.graphicsQueue();
    init_info.DescriptorPool = m_descriptorPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.RenderPass = renderPass;

    ImGui_ImplVulkan_Init(&init_info);

    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();
    ImGui_ImplVulkan_CreateFontsTexture();
    ctx.endSingleTimeCommands(cmd);
}

void ImGuiLayer::shutdown() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_descriptorPool && m_device) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    }
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    ImGui::Render();
}

void ImGuiLayer::render(VkCommandBuffer cmd) {
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

VkDescriptorSet ImGuiLayer::addTexture(VkImageView view, VkSampler sampler) {
    return ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

ImFont* ImGuiLayer::getLogoFont(float targetSize) const {
    int best = 0;
    float bestDiff = std::fabsf(k_logoFontSizes[0] - targetSize);
    for (int i = 1; i < 4; ++i) {
        float diff = std::fabsf(k_logoFontSizes[i] - targetSize);
        if (diff < bestDiff) { bestDiff = diff; best = i; }
    }
    return (m_logoFonts[best] != nullptr) ? m_logoFonts[best] : ImGui::GetFont();
}
