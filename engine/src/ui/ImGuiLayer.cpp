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

    // Editor palette: near-black canvas, cyan primary, magenta active, purple
    // for headers/selectables. Tuned to stay readable with per-widget button
    // colors that SongEditor pushes (green/blue/amber accents).
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.FrameRounding    = 4.f;
        style.GrabRounding     = 4.f;
        style.WindowRounding   = 6.f;
        style.PopupRounding    = 6.f;
        style.ScrollbarRounding= 8.f;
        style.TabRounding      = 4.f;
        style.FrameBorderSize  = 0.f;
        style.WindowBorderSize = 1.f;

        ImVec4* c = style.Colors;
        c[ImGuiCol_WindowBg]           = ImVec4(0.03f, 0.03f, 0.05f, 1.00f);
        c[ImGuiCol_ChildBg]            = ImVec4(0.04f, 0.04f, 0.06f, 1.00f);
        c[ImGuiCol_PopupBg]            = ImVec4(0.05f, 0.05f, 0.09f, 0.98f);
        c[ImGuiCol_Border]             = ImVec4(0.16f, 0.16f, 0.22f, 1.00f);
        c[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        c[ImGuiCol_FrameBg]            = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
        c[ImGuiCol_FrameBgHovered]     = ImVec4(0.14f, 0.17f, 0.24f, 1.00f);
        c[ImGuiCol_FrameBgActive]      = ImVec4(0.18f, 0.22f, 0.32f, 1.00f);

        c[ImGuiCol_TitleBg]            = ImVec4(0.06f, 0.06f, 0.10f, 1.00f);
        c[ImGuiCol_TitleBgActive]      = ImVec4(0.09f, 0.09f, 0.16f, 1.00f);
        c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.04f, 0.04f, 0.06f, 1.00f);
        c[ImGuiCol_MenuBarBg]          = ImVec4(0.06f, 0.06f, 0.10f, 1.00f);

        c[ImGuiCol_ScrollbarBg]        = ImVec4(0.02f, 0.02f, 0.03f, 1.00f);
        c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.20f, 0.22f, 0.28f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 0.75f, 1.00f, 0.80f);
        c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.00f, 0.55f, 0.90f, 1.00f);

        c[ImGuiCol_CheckMark]          = ImVec4(0.00f, 0.90f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrab]         = ImVec4(0.00f, 0.75f, 1.00f, 1.00f);
        c[ImGuiCol_SliderGrabActive]   = ImVec4(0.95f, 0.30f, 0.75f, 1.00f);

        c[ImGuiCol_Button]             = ImVec4(0.00f, 0.55f, 0.85f, 0.85f);
        c[ImGuiCol_ButtonHovered]      = ImVec4(0.00f, 0.75f, 1.00f, 1.00f);
        c[ImGuiCol_ButtonActive]       = ImVec4(0.95f, 0.30f, 0.75f, 1.00f);

        c[ImGuiCol_Header]             = ImVec4(0.22f, 0.23f, 0.28f, 0.75f);
        c[ImGuiCol_HeaderHovered]      = ImVec4(0.55f, 0.58f, 0.65f, 0.55f);
        c[ImGuiCol_HeaderActive]       = ImVec4(0.70f, 0.74f, 0.82f, 0.75f);

        c[ImGuiCol_Separator]          = ImVec4(0.15f, 0.15f, 0.22f, 1.00f);
        c[ImGuiCol_SeparatorHovered]   = ImVec4(0.00f, 0.75f, 1.00f, 0.80f);
        c[ImGuiCol_SeparatorActive]    = ImVec4(0.00f, 0.55f, 0.90f, 1.00f);

        c[ImGuiCol_ResizeGrip]         = ImVec4(0.00f, 0.55f, 0.85f, 0.35f);
        c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.00f, 0.75f, 1.00f, 0.85f);
        c[ImGuiCol_ResizeGripActive]   = ImVec4(0.95f, 0.30f, 0.75f, 0.95f);

        c[ImGuiCol_Tab]                = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
        c[ImGuiCol_TabHovered]         = ImVec4(0.00f, 0.60f, 0.90f, 0.80f);
        c[ImGuiCol_TabActive]          = ImVec4(0.00f, 0.55f, 0.85f, 1.00f);
        c[ImGuiCol_TabUnfocused]       = ImVec4(0.07f, 0.07f, 0.10f, 1.00f);
        c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.20f, 1.00f);

        c[ImGuiCol_Text]               = ImVec4(0.96f, 0.96f, 0.97f, 1.00f);
        c[ImGuiCol_TextDisabled]       = ImVec4(0.54f, 0.56f, 0.62f, 1.00f);
        c[ImGuiCol_TextSelectedBg]     = ImVec4(0.00f, 0.55f, 0.90f, 0.45f);

        c[ImGuiCol_PlotLines]          = ImVec4(0.00f, 0.75f, 1.00f, 1.00f);
        c[ImGuiCol_PlotLinesHovered]   = ImVec4(0.95f, 0.30f, 0.75f, 1.00f);
        c[ImGuiCol_PlotHistogram]      = ImVec4(0.00f, 0.75f, 1.00f, 1.00f);
        c[ImGuiCol_PlotHistogramHovered]= ImVec4(0.95f, 0.30f, 0.75f, 1.00f);
    }

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
