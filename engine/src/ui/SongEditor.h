#pragma once
#include "MusicSelectionEditor.h"
#include "AssetBrowser.h"
#include "renderer/vulkan/TextureManager.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>

class Engine;
class VulkanContext;
class BufferManager;
class ImGuiLayer;

class SongEditor {
public:
    void render(Engine* engine);

    void initVulkan(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui,
                    GLFWwindow* window = nullptr);
    void shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr);

    void setSong(SongInfo* song, const std::string& projectPath);

private:
    std::string browseFile(const wchar_t* filter, const std::string& destSubdir);
    void renderProperties();
    void renderAssets();
    void importFiles(const std::vector<std::string>& srcPaths);

    SongInfo*      m_song        = nullptr;
    std::string    m_projectPath;

    VulkanContext* m_ctx    = nullptr;
    BufferManager* m_bufMgr = nullptr;
    ImGuiLayer*    m_imgui  = nullptr;
    GLFWwindow*    m_window = nullptr;

    // ── Panel split ──────────────────────────────────────────────────────────
    float m_vSplit = 0.65f;   // Properties / Assets vertical split

    // ── Thumbnail cache ──────────────────────────────────────────────────────
    struct ThumbEntry { Texture tex; VkDescriptorSet desc = VK_NULL_HANDLE; };
    std::unordered_map<std::string, ThumbEntry> m_thumbCache;
    void clearThumbnails();
    VkDescriptorSet getThumb(const std::string& relPath);

    // ── Asset browser ────────────────────────────────────────────────────────
    AssetList m_assets;
    bool      m_assetsScanned = false;

    std::string m_statusMsg;
    float       m_statusTimer = 0.f;
};
