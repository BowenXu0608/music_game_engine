#pragma once
#include "ui/ProjectHub.h"  // GameModeConfig, GameModeType, DropDimension
#include "renderer/vulkan/TextureManager.h"
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>

class IPlayerEngine;
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
    // clears a chart.
    int         scoreEasy        = 0;
    int         scoreMedium      = 0;
    int         scoreHard        = 0;
    std::string achievementEasy;
    std::string achievementMedium;
    std::string achievementHard;

    // Music-selection preview clip (seconds into the audio).
    float       previewStart    = -1.f;
    float       previewDuration = 30.f;

    GameModeConfig gameMode;          // per-song game mode config
};

struct MusicSetInfo {
    std::string name;
    std::string coverImage;   // relative path from project root
    std::vector<SongInfo> songs;
};

// Player-facing music-selection scene. Owns the sets/songs hierarchy, scroll
// animation, cover-texture cache, audio-preview dwell timer, and the
// rhombus-carousel rendering. Compiled into both desktop (where
// MusicSelectionEditor wraps it) and Android (where AndroidEngine drives it).
class MusicSelectionView {
public:
    MusicSelectionView() = default;
    virtual ~MusicSelectionView() = default;

    // Pass nullptr for `imgui` on Android. Texture registration uses
    // ImGui_ImplVulkan_AddTexture directly internally.
    void initVulkan(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer* imgui);
    void shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr);

    virtual void load(const std::string& projectRoot);
    void save();

    void update(float dt, IPlayerEngine* engine);
    void renderGamePreview(ImVec2 origin, ImVec2 size, IPlayerEngine* engine);

    SongInfo* getSelectedSong();
    const std::vector<MusicSetInfo>& sets() const { return m_sets; }
    const std::string& projectPath() const { return m_projectPath; }

protected:
    // Editor hook: double-click on a song card. View leaves it blank; the
    // editor overrides to open SongEditor for that song. Phone player has
    // no concept of opening an editor, so the default no-op is correct.
    virtual void onSongCardDoubleClick(int /*songIdx*/) {}

    // ── Sub-element render helpers ───────────────────────────────────────────
    void renderSetWheel(ImVec2 origin, float width, float height);
    void renderSongWheel(ImVec2 origin, float width, float height);
    void renderCoverPhoto(ImVec2 origin, float size);
    void renderDifficultyButtons(ImVec2 origin, float width);
    void renderPlayButton(ImVec2 origin, float width, IPlayerEngine* engine);

    // ── Texture cache ────────────────────────────────────────────────────────
    VkDescriptorSet getCoverDesc(const std::string& relPath);
    void clearCovers();

    // ── Audio preview ────────────────────────────────────────────────────────
    void updateAudioPreview(float dt, IPlayerEngine* engine);

    // ── Project ──────────────────────────────────────────────────────────────
    std::string m_projectPath;
    bool        m_loaded = false;

    // ── Vulkan handles (set by initVulkan) ───────────────────────────────────
    VulkanContext* m_ctx    = nullptr;
    BufferManager* m_bufMgr = nullptr;
    ImGuiLayer*    m_imgui  = nullptr;  // optional — null on Android

    // ── Data ─────────────────────────────────────────────────────────────────
    std::vector<MusicSetInfo> m_sets;
    int  m_selectedSet  = -1;
    int  m_selectedSong = -1;
    Difficulty m_selectedDifficulty = Difficulty::Hard;
    bool       m_autoPlay = false;

    // Page-level background, frosted-glass overlay drawn over it.
    std::string m_pageBackground;
    std::string m_fcImage;  // Full Combo badge
    std::string m_apImage;  // All Perfect badge

    // Audio preview state (dwell-then-play 30 s clip).
    int         m_previewSetIdx  = -1;
    int         m_previewSongIdx = -1;
    float       m_previewDwellT  = 0.f;
    bool        m_previewPlaying = false;
    std::string m_previewPath;
    float       m_previewStopT   = 0.f;

    // ── Wheel scroll state (smooth animation) ────────────────────────────────
    float m_setScrollTarget   = 0.f;
    float m_setScrollCurrent  = 0.f;
    float m_songScrollTarget  = 0.f;
    float m_songScrollCurrent = 0.f;

    // ── Cover texture cache (also used for page background) ──────────────────
    struct CoverEntry { Texture tex; VkDescriptorSet desc = VK_NULL_HANDLE; };
    std::unordered_map<std::string, CoverEntry> m_coverCache;
};
