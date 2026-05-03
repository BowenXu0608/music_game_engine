#pragma once
#include "game/screens/MusicSelectionView.h"  // SongInfo, MusicSetInfo, Difficulty
#include "AssetBrowser.h"
#include "ImageEditor.h"
#include "game/PlayerSettings.h"
#include "renderer/vulkan/TextureManager.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <unordered_map>

class Engine;
class VulkanContext;
class BufferManager;
class ImGuiLayer;

class MusicSelectionEditor : public MusicSelectionView {
public:
    void render(Engine* engine);
    void load(const std::string& projectPath) override;

    void initVulkan(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui,
                    GLFWwindow* window = nullptr);
    void shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr);

    void importFiles(const std::vector<std::string>& srcPaths);

protected:
    void onSongCardDoubleClick(int songIdx) override;

private:
    // ── Panel rendering ──────────────────────────────────────────────────────
    void renderPreview(float width, float height);
    void renderHierarchy(float width, float height);

    GLFWwindow* m_window = nullptr;

    // Modal flag for the preview overlay (shows both badges with a
    // spinning/glow effect so the author can judge them in context).
    bool        m_showAchievementPreview = false;

    // ── Panel split ──────────────────────────────────────────────────────────
    float m_hSplit = 0.70f;   // Preview / Hierarchy horizontal split
    float m_vSplit = 0.75f;   // Top (preview+hierarchy) / Bottom (assets) vertical split
    bool  m_assetsBarOpen = true;  // Bottom Assets strip expanded?
    float m_assetsBarH   = 200.f;  // Expanded height

    // ── Add-new dialogs ──────────────────────────────────────────────────────
    bool m_showAddSetDialog  = false;
    bool m_showAddSongDialog = false;
    char m_newSetName[128]   = {};
    char m_newSongName[128]  = {};
    char m_newSongArtist[128] = {};

    // ── Thumbnail cache (for asset panel) ───────────────────────────────────
    struct ThumbEntry { Texture tex; VkDescriptorSet desc = VK_NULL_HANDLE; };
    std::unordered_map<std::string, ThumbEntry> m_thumbCache;
    void clearThumbnails();
    VkDescriptorSet getThumb(const std::string& relPath);

    // ── Asset browser ────────────────────────────────────────────────────────
    AssetList m_assets;
    bool      m_assetsScanned = false;
    // ImageEditor m_imageEditor;
    void renderAssets();

    // ── Preview tab ──────────────────────────────────────────────────────────
    int m_previewTab = 0;  // 0=Editor, 1=Game Preview

    // ── Engine pointer (set each frame in render()) ────────────────────────
    Engine* m_engine = nullptr;

    // ── Status ───────────────────────────────────────────────────────────────
    std::string m_statusMsg;
    float       m_statusTimer = 0.f;

    // ── Settings overlay (player-facing page opened from the gear button) ────
    bool           m_showSettings = false;
    PlayerSettings m_previewSettings;
};
