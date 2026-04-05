#pragma once
#include "MusicSelectionEditor.h"
#include "AssetBrowser.h"
#include "engine/AudioEngine.h"
#include "engine/AudioAnalyzer.h"
#include "game/chart/ChartTypes.h"
#include "renderer/vulkan/TextureManager.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <unordered_map>

// ── Editor note types (separate from gameplay ChartTypes) ───────────────────
enum class EditorNoteType { Tap, Hold, Slide };

struct EditorNote {
    EditorNoteType type = EditorNoteType::Tap;
    float    time    = 0.f;   // start time in seconds (snapped to marker)
    float    endTime = 0.f;   // end time for Press notes (0 for Click/Slide)
    int      track   = 0;     // 0-based track index
    bool     isSky   = false; // true = sky lane (3D mode only)
};

enum class NoteTool { None, Tap, Hold, Slide };

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
    void renderGameModeConfig();
    void renderGameModePreview(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void renderChartTimeline(ImDrawList* dl, ImVec2 origin, ImVec2 size, Engine* engine);
    void renderWaveform(ImDrawList* dl, ImVec2 origin, ImVec2 size, Engine* engine);
    void loadWaveformIfNeeded(Engine* engine);
    void renderAssets();
    void importFiles(const std::vector<std::string>& srcPaths);
    ChartData buildChartFromNotes() const;
    void exportAllCharts();
    void launchTestProcess();

    SongInfo*      m_song        = nullptr;
    std::string    m_projectPath;

    VulkanContext* m_ctx    = nullptr;
    BufferManager* m_bufMgr = nullptr;
    ImGuiLayer*    m_imgui  = nullptr;
    GLFWwindow*    m_window = nullptr;

    // ── Panel split ──────────────────────────────────────────────────────────
    float m_sidebarW  = 280.f;  // Left sidebar width in pixels (draggable)
    float m_sceneSplit = 0.35f; // Scene / Timeline vertical split in center area

    // ── Chart timeline state ─────────────────────────────────────────────────
    float m_timelineScrollX  = 0.f;    // horizontal scroll offset in seconds
    float m_timelineZoom     = 100.f;  // pixels per second
    // (removed: m_timelineVSplit — waveform is now fixed height at bottom)
    float m_hoverTime        = -1.f;   // time under mouse cursor (-1 = none)
    float m_sceneTime        = 0.f;    // persisted cursor time for Scene view

    // ── Waveform cache ───────────────────────────────────────────────────────
    WaveformData  m_waveform;
    std::string   m_waveformAudioPath;
    bool          m_waveformLoaded = false;

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

    // ── Difficulty ────────────────────────────────────────────────────────────
    Difficulty m_currentDifficulty = Difficulty::Hard;

    // ── Note editing (per-difficulty) ────────────────────────────────────────
    std::unordered_map<int, std::vector<EditorNote>> m_diffNotes;   // key = (int)Difficulty
    std::unordered_map<int, std::vector<float>>      m_diffMarkers; // key = (int)Difficulty

    // Convenience accessors for the current difficulty's data
    std::vector<EditorNote>& notes()         { return m_diffNotes[(int)m_currentDifficulty]; }
    std::vector<float>&      markers()       { return m_diffMarkers[(int)m_currentDifficulty]; }
    const std::vector<EditorNote>& notes()   const { return const_cast<SongEditor*>(this)->m_diffNotes[(int)m_currentDifficulty]; }
    const std::vector<float>&      markers() const { return const_cast<SongEditor*>(this)->m_diffMarkers[(int)m_currentDifficulty]; }

    NoteTool  m_noteTool        = NoteTool::None;
    bool      m_pressFirstClick = false;   // waiting for second click to finish a Press note
    float     m_pressStartTime  = 0.f;     // start time of the in-progress Press note
    int       m_pressStartTrack = 0;       // track of the in-progress Press note
    bool      m_pressStartSky   = false;   // sky flag of the in-progress Press note

    void renderDifficultySelector();
    void renderSceneView(ImDrawList* dl, ImVec2 origin, ImVec2 size, Engine* engine);
    void renderNoteToolbar();
    void handleNotePlacement(ImVec2 origin, ImVec2 size, float startTime, int trackCount,
                             bool is3DSky, float trackH, float regionTop,
                             Engine* engine = nullptr);
    void renderNotes(ImDrawList* dl, ImVec2 origin, ImVec2 size, float startTime,
                     int trackCount, float trackH, float regionTop, bool skyOnly);
    float snapToMarker(float time) const;
    int   trackFromY(float mouseY, float regionTop, float trackH, int trackCount) const;

    // ── Beat analysis ────────────────────────────────────────────────────────
    AudioAnalyzer m_analyzer;
    bool m_showAnalysisError = false;
    std::string m_analysisErrorMsg;

    // ── Test Game popup ──────────────────────────────────────────────────────
    bool m_showTestError = false;
    std::string m_testErrorMsg;
};
