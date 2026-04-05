#pragma once
#include "ProjectHub.h"  // GameModeConfig, GameModeType, DropDimension
#include "AssetBrowser.h"
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

enum class Difficulty { Easy, Medium, Hard };

struct SongInfo {
    std::string name;
    std::string artist;
    std::string coverImage;   // relative path from project root
    std::string audioFile;    // relative path
    std::string chartEasy;    // relative path to chart file
    std::string chartMedium;
    std::string chartHard;
    int         score = 0;            // 0..1000000
    std::string achievement;          // "", "FC", "AP"
    GameModeConfig gameMode;          // per-song game mode config
};

struct MusicSetInfo {
    std::string name;
    std::string coverImage;   // relative path from project root
    std::vector<SongInfo> songs;
};

class MusicSelectionEditor {
public:
    void render(Engine* engine);
    void load(const std::string& projectPath);
    void save();

    void initVulkan(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui,
                    GLFWwindow* window = nullptr);
    void shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr);

    const std::string& projectPath() const { return m_projectPath; }
    SongInfo* getSelectedSong();
    void renderGamePreview(ImVec2 origin, ImVec2 size);

private:
    // ── Panel rendering ──────────────────────────────────────────────────────
    void renderPreview(float width, float height);
    void renderHierarchy(float width, float height);

    // ── Preview sub-elements ─────────────────────────────────────────────────
    void renderSetWheel(ImVec2 origin, float width, float height);
    void renderSongWheel(ImVec2 origin, float width, float height);
    void renderCoverPhoto(ImVec2 origin, float size);
    void renderDifficultyButtons(ImVec2 origin, float width);
    void renderPlayButton(ImVec2 origin, float width);

    // ── Texture helpers ──────────────────────────────────────────────────────
    VkDescriptorSet getCoverDesc(const std::string& relPath);
    void clearCovers();

    // ── Project ──────────────────────────────────────────────────────────────
    std::string m_projectPath;
    bool        m_loaded = false;

    // ── Vulkan handles ───────────────────────────────────────────────────────
    VulkanContext* m_ctx    = nullptr;
    BufferManager* m_bufMgr = nullptr;
    ImGuiLayer*    m_imgui  = nullptr;
    GLFWwindow*    m_window = nullptr;

    // ── Data ─────────────────────────────────────────────────────────────────
    std::vector<MusicSetInfo> m_sets;
    int  m_selectedSet   = -1;    // index into m_sets, -1 = none
    int  m_selectedSong  = -1;    // index into m_sets[m_selectedSet].songs
    Difficulty m_selectedDifficulty = Difficulty::Hard;

    // ── Wheel scroll state (smooth animation) ────────────────────────────────
    float m_setScrollTarget  = 0.f;
    float m_setScrollCurrent = 0.f;
    float m_songScrollTarget  = 0.f;
    float m_songScrollCurrent = 0.f;

    // ── Cover texture cache ──────────────────────────────────────────────────
    struct CoverEntry { Texture tex; VkDescriptorSet desc = VK_NULL_HANDLE; };
    std::unordered_map<std::string, CoverEntry> m_coverCache;

    // ── Panel split ──────────────────────────────────────────────────────────
    float m_hSplit = 0.70f;   // Preview / Hierarchy horizontal split
    float m_vSplit = 0.75f;   // Top (preview+hierarchy) / Bottom (assets) vertical split

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
    void renderAssets();
    void importFiles(const std::vector<std::string>& srcPaths);

    // ── Preview tab ──────────────────────────────────────────────────────────
    int m_previewTab = 0;  // 0=Editor, 1=Game Preview

    // ── Engine pointer (set each frame in render()) ────────────────────────
    Engine* m_engine = nullptr;

    // ── Status ───────────────────────────────────────────────────────────────
    std::string m_statusMsg;
    float       m_statusTimer = 0.f;
};
