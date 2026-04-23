#pragma once
#include "ProjectHub.h"  // GameModeConfig, GameModeType, DropDimension
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

enum class Difficulty { Easy, Medium, Hard };

struct SongInfo {
    std::string name;
    std::string artist;
    std::string coverImage;   // relative path from project root
    std::string audioFile;    // relative path
    std::string chartEasy;    // relative path to chart file
    std::string chartMedium;
    std::string chartHard;
    int         score = 0;            // legacy per-song field (kept for migration)
    std::string achievement;          // legacy per-song field (kept for migration)

    // Per-difficulty stats. Populated by the results screen when the player
    // clears a chart; each difficulty records its own score + achievement
    // badge so the music card reflects the currently-selected difficulty.
    int         scoreEasy        = 0;
    int         scoreMedium      = 0;
    int         scoreHard        = 0;
    std::string achievementEasy;
    std::string achievementMedium;
    std::string achievementHard;

    // Music-selection preview clip (seconds into the audio). Populated by
    // the AudioAnalyzer's peak-energy detector; editable in SongEditor.
    // previewStart < 0 means "auto-detect on next chance".
    float       previewStart    = -1.f;
    float       previewDuration = 30.f;

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
    const std::vector<MusicSetInfo>& sets() const { return m_sets; }
    void renderGamePreview(ImVec2 origin, ImVec2 size);
    void importFiles(const std::vector<std::string>& srcPaths);

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
    bool       m_autoPlay = false;

    // Page-level background — drawn behind the wheels/cover and then
    // overlaid with a frosted-glass gradient (heavy on sides, light
    // middle) so foreground text stays readable.
    std::string m_pageBackground;   // project-relative path

    // Achievement badge images — one pair per game, configured here
    // instead of per-song. Read by the results screen when a chart
    // reaches FC or AP. Stored as top-level keys in music_selection.json.
    std::string m_fcImage;          // Full Combo badge
    std::string m_apImage;          // All Perfect badge

    // Modal flag for the preview overlay (shows both badges with a
    // spinning/glow effect so the author can judge them in context).
    bool        m_showAchievementPreview = false;

    // Audio preview: when the selected song dwells for a short moment,
    // load and play a 30 s clip from the song's previewStart. Reset every
    // time the selection changes.
    int    m_previewSetIdx     = -1;
    int    m_previewSongIdx    = -1;
    float  m_previewDwellT     = 0.f;   // seconds the selection has dwelt
    bool   m_previewPlaying    = false; // audio engine currently plays our clip
    std::string m_previewPath;          // currently-loaded clip's audio path
    float  m_previewStopT      = 0.f;   // seconds left before we stop playback

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
    // ImageEditor m_imageEditor;
    void renderAssets();
    void updateAudioPreview(float dt);

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
