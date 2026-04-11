#pragma once
#include "MusicSelectionEditor.h"
#include "AssetBrowser.h"
#include "ImageEditor.h"
#include "engine/AudioEngine.h"
#include "engine/AudioAnalyzer.h"
#include "game/chart/ChartTypes.h"
#include "renderer/vulkan/TextureManager.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

// ── Editor note types (separate from gameplay ChartTypes) ───────────────────
enum class EditorNoteType { Tap, Hold, Slide, Flick };

// Lane-change transition style for a Hold note. Mirrors ChartTypes::HoldTransition.
enum class EditorHoldTransition { Straight = 0, Angle90 = 1, Curve = 2, Rhomboid = 3 };

// One waypoint along an authored Hold's lane path. Mirrors HoldWaypoint.
struct EditorHoldWaypoint {
    float                tOffset       = 0.f;
    int                  lane          = 0;
    float                transitionLen = 0.f;
    EditorHoldTransition style         = EditorHoldTransition::Curve;
};

struct EditorNote {
    EditorNoteType type = EditorNoteType::Tap;
    float    time    = 0.f;   // start time in seconds (snapped to marker)
    float    endTime = 0.f;   // end time for Hold notes (0 for Click/Slide)
    int      track   = 0;     // starting 0-based track index
    int      endTrack = -1;   // legacy single-transition end track (-1 = unused)
    bool     isSky   = false; // true = sky lane (3D mode only)
    int      laneSpan = 1;    // width in lanes: 1, 2, or 3 (Circle mode only)

    // ── Multi-waypoint hold path (new authoring model) ─────────────────────
    // When non-empty, drives gameplay/rendering and overrides the legacy
    // single-transition fields below. Recorded by dragging the mouse during
    // the Hold tool gesture: every lane crossing pushes a new waypoint.
    std::vector<EditorHoldWaypoint> waypoints;

    // ── Legacy single-transition fields (still parsed from old chart files) ─
    EditorHoldTransition transition = EditorHoldTransition::Straight;
    float    transitionLen   = 0.f;
    float    transitionStart = -1.f;

    // Sample-point checkpoints (slide-tick scoring). Seconds from note start.
    std::vector<float> samplePoints;

    // ── ScanLine-mode fields (ignored by other modes) ─────────────────────
    // All values are normalized [0..1] within the scene rect. See the
    // Scan Line authoring flow in SongEditor::handleScanLineInput.
    float scanX    = 0.f;
    float scanY    = 0.f;
    float scanEndY = -1.f;  // hold only; -1 = unused
    std::vector<std::pair<float,float>> scanPath; // slide drag path

    int effectiveEndTrack() const {
        if (!waypoints.empty()) return waypoints.back().lane;
        return endTrack < 0 ? track : endTrack;
    }
    bool hasCrossLane() const {
        return !waypoints.empty() || (endTrack >= 0 && endTrack != track);
    }
};

enum class NoteTool { None, Tap, Hold, Slide, Flick };

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

    const std::string& projectPath() const { return m_projectPath; }
    void importFiles(const std::vector<std::string>& srcPaths);

private:
    std::string browseFile(const wchar_t* filter, const std::string& destSubdir);
    void renderProperties();
    void renderGameModeConfig();
    void renderGameModePreview(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void renderChartTimeline(ImDrawList* dl, ImVec2 origin, ImVec2 size, Engine* engine);
    void renderWaveform(ImDrawList* dl, ImVec2 origin, ImVec2 size, Engine* engine);
    void loadWaveformIfNeeded(Engine* engine);
    void renderAssets();
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
    // ImageEditor m_imageEditor;

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

    // Default laneSpan for newly placed notes (Circle mode). 1, 2, or 3 lanes.
    int       m_defaultLaneSpan = 1;
    // Index into notes() of the currently selected note (-1 = none). Set by
    // left-clicking a note in the timeline; opens the Note Properties popup.
    int       m_selectedNoteIdx = -1;

    NoteTool  m_noteTool        = NoteTool::None;

    // ── Drag-to-record state for the Hold tool ──────────────────────────────
    // Replaces the old two-click flow: press LMB at start, drag through the
    // timeline (the editor samples track-under-cursor each frame and pushes
    // a waypoint whenever the lane changes), release LMB to commit.
    bool      m_holdDragging    = false;
    EditorNote m_holdDraft;                // the in-progress note being recorded
    int       m_holdLastTrack   = -1;      // last track sampled while dragging

    // ── Scan Line authoring state ────────────────────────────────────────────
    // Hold tool: two-click flow. First click captures start, second click
    // commits end. m_scanHoldAwaitEnd gates the flow.
    bool  m_scanHoldAwaitEnd = false;
    float m_scanHoldStartX   = 0.f;  // normalized [0..1]
    float m_scanHoldStartY   = 0.f;
    float m_scanHoldStartT   = 0.f;  // head time in seconds
    float m_scanHoldTurnCap  = 0.f;  // time of next scan-line turn after start
    bool  m_scanHoldGoingUp  = false;

    // Slide tool: drag-to-record with monotonic-direction gate.
    bool       m_scanSlideDragging = false;
    EditorNote m_scanSlideDraft{};
    bool       m_scanSlideGoingUp  = false;
    float      m_scanSlideLastY    = 0.f;
    float      m_scanSlideTurnCap  = 0.f;

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

    // ── Scan Line helpers ────────────────────────────────────────────────────
    // Schedule: 1 bar (4 beats) per sweep at the dominant BPM (fallback 120).
    // Period returned is the time for ONE sweep (up or down, not full cycle).
    float scanLinePeriod() const;
    // Normalized Y fraction [0..1] (0 = top). At t=0 the scan line starts at
    // the bottom moving up.
    float scanLineFrac(float t) const;
    bool  scanLineGoingUp(float t) const;
    // Time of the next scan-line direction reversal strictly greater than t.
    float scanLineNextTurn(float t) const;
    // Inverse: earliest time >= t at which the scan line reaches frac, during
    // the same sweep (bounded by scanLineNextTurn(t)).
    float scanLineTimeForFrac(float t, float frac) const;

    void renderScanLineToolbar();
    void handleScanLineInput(ImVec2 origin, ImVec2 size, float curTime,
                             bool hovered, Engine* engine);

    // ── Beat analysis ────────────────────────────────────────────────────────
    AudioAnalyzer m_analyzer;
    bool m_showAnalysisError = false;
    std::string m_analysisErrorMsg;

    // ── Dynamic BPM map (from audio analysis) ────────────────────────────────
    std::vector<BpmChange> m_bpmChanges;   // tempo sections detected by AI
    float m_dominantBpm = 0.f;             // most common BPM

    // ── Test Game popup ──────────────────────────────────────────────────────
    bool m_showTestError = false;
    std::string m_testErrorMsg;
};
