#pragma once
#include "MusicSelectionEditor.h"
#include "AssetBrowser.h"
#include "ImageEditor.h"
#include "engine/AudioEngine.h"
#include "engine/AudioAnalyzer.h"
#include "game/chart/ChartTypes.h"
#include "game/chart/ScanPageUtils.h"
#include "renderer/MaterialSlots.h"
#include "renderer/vulkan/TextureManager.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>

// ── Editor note types (separate from gameplay ChartTypes) ───────────────────
enum class EditorNoteType { Tap, Hold, Slide, Flick, Arc, ArcTap };

// Lane-change transition style for a Hold note. Mirrors ChartTypes::HoldTransition.
enum class EditorHoldTransition { Straight = 0, Angle90 = 1, Curve = 2, Rhomboid = 3 };

// One waypoint along an authored Hold's lane path. Mirrors HoldWaypoint.
struct EditorHoldWaypoint {
    float                tOffset       = 0.f;
    int                  lane          = 0;
    float                transitionLen = 0.f;
    EditorHoldTransition style         = EditorHoldTransition::Curve;
};

// One waypoint along an authored Arc's path. Arcs with >=2 waypoints use
// this list; the old 2-endpoint fields are kept for backward compat.
struct ArcWaypoint {
    float time  = 0.f;   // absolute time in seconds
    float x     = 0.f;   // normalized X [0..1] (lane position)
    float y     = 0.f;   // normalized Y [0..1] (height: 0=ground, 1=sky)
    float easeX = 0.f;   // X easing TO this point from previous (0=linear)
    float easeY = 0.f;   // Y easing TO this point from previous
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
    int   scanHoldSweeps = 0; // extra sweeps the hold crosses (0 = single sweep)
    std::vector<std::pair<float,float>> scanPath; // slide drag path

    // ── Arc fields (Arcaea 3D mode only) ──────────────────────────────────
    // Multi-waypoint arc path. When >=2 entries, this is the authoritative
    // arc shape and the legacy 2-endpoint fields below are ignored.
    std::vector<ArcWaypoint> arcWaypoints;

    // Legacy 2-endpoint fields (still used when arcWaypoints is empty).
    float arcStartX  = 0.f;   // normalized X [0..1] at start time
    float arcEndX    = 0.f;   // normalized X [0..1] at end time
    float arcStartY  = 0.f;   // height [0..1] at start (0=ground, 1=sky)
    float arcEndY    = 0.f;   // height [0..1] at end
    float arcEaseX   = 0.f;   // X easing power (matches ArcData convention)
    float arcEaseY   = 0.f;   // Y easing power
    int   arcColor   = 0;     // 0=cyan, 1=pink
    bool  arcIsVoid  = false;  // void arc = visual only, no input
    int   arcTapParent = -1;   // ArcTap only: index into notes() of parent Arc

    int effectiveEndTrack() const {
        if (!waypoints.empty()) return waypoints.back().lane;
        return endTrack < 0 ? track : endTrack;
    }
    bool hasCrossLane() const {
        return !waypoints.empty() || (endTrack >= 0 && endTrack != track);
    }
};

enum class NoteTool { None, Tap, Hold, Slide, Flick, Arc, ArcTap };

class Engine;
class VulkanContext;
class BufferManager;
class ImGuiLayer;

class SongEditor {
public:
    SongEditor();
    ~SongEditor();

    void render(Engine* engine);

    // Draw the Copilot panel as a floating right-side overlay window. Used by
    // the Engine so the chat sidebar is reachable from every editor page
    // (ProjectHub, Start Screen, Music Selection, Settings). SongEditor's own
    // render() already hosts Copilot in its sidebar, so the Engine skips this
    // when it is the active layer.
    void renderCopilotOverlay(Engine* engine);

    // Width (pixels) the Copilot overlay currently occupies on the right side
    // of the display. Other editor pages subtract this from their full-width
    // window so their content never renders underneath the sidebar.
    float copilotOverlayWidth() const;

    // Each frame, non-SongEditor pages tell the overlay how many pixels of
    // vertical space to leave free at the bottom (their Assets strip + nav
    // bar height) so the overlay behaves like the inline Copilot on
    // SongEditor: top body column only, Assets strip spans full width below.
    void setOverlayBottomReserve(float px) { m_overlayBottomReserve = px; }

    void initVulkan(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui,
                    GLFWwindow* window = nullptr);
    void shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr);

    void setSong(SongInfo* song, const std::string& projectPath);

    const std::string& projectPath() const { return m_projectPath; }
    void importFiles(const std::vector<std::string>& srcPaths);

    // Public flush hook for the Engine's auto-save / window-close / crash
    // path. Writes per-difficulty chart JSONs but skips the "Saved!" status
    // toast so autosave doesn't visually compete with foreground edits.
    // Safe to call when no song is loaded — becomes a no-op.
    void flushChartsForAutoSave();

private:
    std::string browseFile(const wchar_t* filter, const std::string& destSubdir);
    void renderProperties();
    void renderGameModeConfig(Engine* engine, bool structureOnly = false,
                              bool presentationOnly = false);
    // Left-sidebar "Note" tab: per-note-type sections (Click / Hold / Flick
    // / Slide / Arc / ArcTap depending on game mode). Each section owns the
    // settings for that note type — material-slot pickers, Hold-corner
    // default, plus the lane-layout knobs at the top.
    void renderNotePage(Engine* engine);
    // Left-sidebar "Material" tab: project-wide material asset CRUD.
    // Defers rendering to StartScreenEditor::renderMaterials which already
    // owns the asset-library UI.
    void renderMaterialBuilderPage(Engine* engine);
    // Shared one-slot material picker used by both the Basic tab's full
    // Materials block and the Note tab's per-type sections.
    void renderMaterialSlotPicker(Engine* engine, MaterialModeKey modeKey,
                                  const MaterialSlotInfo& slot);
    void renderAiPanels();
    void renderGameModePreview(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void renderChartTimeline(ImDrawList* dl, ImVec2 origin, ImVec2 size, Engine* engine);
    void renderWaveform(ImDrawList* dl, ImVec2 origin, ImVec2 size, Engine* engine);
    void loadWaveformIfNeeded(Engine* engine);
    void renderAssets();
    ChartData buildChartFromNotes() const;
    void exportAllCharts();
    void launchTestProcess();
    void reloadChartsForCurrentMode();
    void loadChartFile(Difficulty diff, const std::string& chartRel);

    // ── AI Place-All inference ───────────────────────────────────────────────
    // Pick note type + duration from a marker's audio features.
    //   flickThreshold — strength >= this becomes Flick (pass 1e9 to disable)
    //   supportsHold   — soft + sustained onsets become Hold
    //   holdMinSec     — minimum sustain (s) for a marker to become a Hold
    // Returns type in .type, Hold duration in .duration (0 for non-Hold).
    struct InferredType { EditorNoteType type = EditorNoteType::Tap; float duration = 0.f; };
    InferredType inferNoteType(const MarkerFeature& f,
                               float flickThreshold, bool supportsHold,
                               float holdMinSec) const;

    // Song-adaptive Flick threshold: the pct-th percentile of |features|
    // strengths. Returns a very large value when supportsFlick is false.
    float computeFlickThreshold(const std::vector<MarkerFeature>& feats,
                                bool supportsFlick, float pct) const;

    // Centroid → lane. If antiJack is true and the mapped lane equals
    // prevLane, shift by ±1 (wraps at edges). Returns -1 if laneCount <= 0.
    int inferLaneFromCentroid(float centroid, int laneCount, int prevLane,
                              bool antiJack) const;

    // Run Chart Audit on the current difficulty's markers (notes intentionally
    // empty so the marker-side density block always fires) and drop weakest-
    // strength markers inside each flagged hotspot until the rate falls to
    // m_autoMarkerThinNps. Snapshots into m_thinUndo* for one-shot undo.
    void thinMarkersInDensityHotspots();

    SongInfo*      m_song        = nullptr;
    std::string    m_projectPath;

    VulkanContext* m_ctx    = nullptr;
    BufferManager* m_bufMgr = nullptr;
    ImGuiLayer*    m_imgui  = nullptr;
    GLFWwindow*    m_window = nullptr;
    Engine*        m_engineCached = nullptr;  // refreshed each frame in render()

    // ── Panel split ──────────────────────────────────────────────────────────
    float m_sidebarW  = 280.f;  // Left sidebar width in pixels (draggable)
    float m_sceneSplit = 0.35f; // Scene / Timeline vertical split in center area
    bool  m_assetsBarOpen = true;  // Bottom Assets strip expanded?
    float m_assetsBarH   = 200.f;  // Height when expanded
    bool  m_copilotBarOpen = true; // Right Copilot sidebar expanded?
    float m_copilotBarW   = 300.f; // Width when expanded (draggable)

    // Right-sidebar page selector. Copilot is the default; Audit hosts the
    // chart-audit metrics + issue list that used to live in a toolbar popup.
    // m_rightSidebarTab tracks the currently-active tab; it's updated by
    // BeginTabItem returning true (i.e., the user's click). The "pending"
    // flag is set by external triggers (e.g. the toolbar Audit button) to
    // force the matching tab on the next frame via ImGuiTabItemFlags_SetSelected.
    // Without this gate, SetSelected would fire every frame and override the
    // user's tab clicks, causing both tabs' content to appear to fight.
    enum class RightSidebarTab { Copilot, Audit };
    RightSidebarTab m_rightSidebarTab        = RightSidebarTab::Copilot;
    bool            m_rightSidebarTabPending = false;

    // Overlay (docked sidebar) state - shared across all non-SongEditor pages.
    bool  m_overlayOpen   = true;
    float m_overlayFullW  = 320.f;
    float m_overlayStripW = 28.f;
    // Reserved pixels at the bottom of the viewport so the overlay leaves
    // room for each page's Assets strip + nav bar. Set every frame by the
    // active page before Engine.cpp calls renderCopilotOverlay().
    float m_overlayBottomReserve = 0.f;

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
    // AI per-marker features — parallel to m_diffMarkers. Populated by
    // the analyzer, consumed by Place All to infer note type + lane.
    std::unordered_map<int, std::vector<MarkerFeature>> m_diffFeatures;

    // Per-difficulty material-slot overrides. Keyed by slot id; values carry
    // the kind/tint/params/texture the user has set for that slot.
    std::unordered_map<int, std::unordered_map<uint16_t, ChartData::MaterialData>> m_diffMaterials;

    // Convenience accessors for the current difficulty's data
    std::vector<EditorNote>& notes()         { return m_diffNotes[(int)m_currentDifficulty]; }
    std::vector<float>&      markers()       { return m_diffMarkers[(int)m_currentDifficulty]; }
    std::vector<MarkerFeature>& features()   { return m_diffFeatures[(int)m_currentDifficulty]; }
    const std::vector<EditorNote>& notes()   const { return const_cast<SongEditor*>(this)->m_diffNotes[(int)m_currentDifficulty]; }
    const std::vector<float>&      markers() const { return const_cast<SongEditor*>(this)->m_diffMarkers[(int)m_currentDifficulty]; }
    const std::vector<MarkerFeature>& features() const { return const_cast<SongEditor*>(this)->m_diffFeatures[(int)m_currentDifficulty]; }
    std::unordered_map<uint16_t, ChartData::MaterialData>& materialsForDiff() {
        return m_diffMaterials[(int)m_currentDifficulty];
    }

    // Default laneSpan for newly placed notes (Circle mode). 1, 2, or 3 lanes.
    int       m_defaultLaneSpan = 1;

    // Default per-segment transition style for newly authored Hold waypoints.
    // Picked in the Note tab's Hold section; applied when the drag-to-record
    // gesture pushes a new waypoint. "Apply to All Holds" rewrites every
    // existing hold waypoint's style to this value.
    EditorHoldTransition m_defaultHoldTransition = EditorHoldTransition::Curve;

    // ── Circle-mode disk animation authoring ─────────────────────────────
    // Per-difficulty keyframe lists, matching the m_diffNotes convention.
    // Copied into ChartData::diskAnimation on export, and read back on load
    // via importDiskAnimationFromChart() when a chart is opened.
    std::unordered_map<int, std::vector<DiskRotationEvent>> m_diffDiskRot;
    std::unordered_map<int, std::vector<DiskMoveEvent>>     m_diffDiskMove;
    std::unordered_map<int, std::vector<DiskScaleEvent>>    m_diffDiskScale;

public:  // Phase 7: Copilot extended apply path mutates these.
    std::vector<DiskRotationEvent>& diskRot()       { return m_diffDiskRot  [(int)m_currentDifficulty]; }
    std::vector<DiskMoveEvent>&     diskMove()      { return m_diffDiskMove [(int)m_currentDifficulty]; }
    std::vector<DiskScaleEvent>&    diskScale()     { return m_diffDiskScale[(int)m_currentDifficulty]; }
    const std::vector<DiskRotationEvent>& diskRot() const {
        return const_cast<SongEditor*>(this)->m_diffDiskRot  [(int)m_currentDifficulty];
    }
    const std::vector<DiskMoveEvent>&     diskMove()  const {
        return const_cast<SongEditor*>(this)->m_diffDiskMove [(int)m_currentDifficulty];
    }
    const std::vector<DiskScaleEvent>&    diskScale() const {
        return const_cast<SongEditor*>(this)->m_diffDiskScale[(int)m_currentDifficulty];
    }
private:

    // Per-difficulty scan-line speed events (Cytus mode only).
    std::unordered_map<int, std::vector<ScanSpeedEvent>> m_diffScanSpeed;
public:  // Phase 7: Copilot extended apply path mutates these.
    std::vector<ScanSpeedEvent>& scanSpeed() { return m_diffScanSpeed[(int)m_currentDifficulty]; }
    const std::vector<ScanSpeedEvent>& scanSpeed() const {
        return const_cast<SongEditor*>(this)->m_diffScanSpeed[(int)m_currentDifficulty];
    }
private:
    int m_selectedScanSpeedKf = -1;

    // Phase table for variable-speed scan line (rebuilt when speed events change).
    struct ScanPhaseEntry { double time, phase, speed; };
    std::vector<ScanPhaseEntry> m_scanPhaseTable;
    bool m_scanPhaseDirty = true;
    void rebuildScanPhaseTable();
    double interpolateScanPhase(double t) const;

    // ── Scan-line page-based editor model ──────────────────────────────────
    // A "page" is one sweep of the scan line. The scene window shows exactly
    // one page at a time with Prev/Next navigation. Pages default to
    // 240/BPM seconds (one bar @ 4/4); m_diffScanPages holds sparse per-page
    // speed overrides. m_scanPageTable is rebuilt lazily from timingPoints +
    // overrides + song end time.
    std::unordered_map<int, std::vector<ScanPageOverride>> m_diffScanPages;
public:  // Phase 7: Copilot extended apply path mutates these.
    std::vector<ScanPageOverride>& scanPages() {
        return m_diffScanPages[(int)m_currentDifficulty];
    }
    const std::vector<ScanPageOverride>& scanPages() const {
        return const_cast<SongEditor*>(this)->m_diffScanPages[(int)m_currentDifficulty];
    }
private:

    int                        m_scanCurrentPage    = 0;
    std::vector<ScanPageInfo>  m_scanPageTable;
    bool                       m_scanPageTableDirty = true;
    void  rebuildScanPageTable();
    int    scanPageForTime(double t) const;
    double scanPageYToTime(int pageIdx, float y01) const;
    float  scanPageTimeToY(int pageIdx, double t) const;

    // Cross-page slide draft: parallel to m_scanSlideDraft.scanPath, one
    // page-index per node. Populated while m_scanSlideDragging is true.
    std::vector<int>           m_scanSlidePathPages;

    // Pending-navigation state for cross-page holds: while m_scanHoldAwaitEnd
    // is true and the user clicks Prev/Next, we remember the start page to
    // compute the final scanHoldSweeps count.
    int                        m_scanHoldStartPage  = 0;

    // Edge-driven auto page turning during hold/slide authoring. Armed when
    // the cursor first enters an edge zone; re-armed when it leaves. Prevents
    // rapid-fire flipping on a stationary cursor.
    bool                       m_scanPageEdgeArmed  = false;

    // Which disk-FX track the config panel is currently editing.
    enum class DiskKfTrack { Rotation, Scale, Move };
    DiskKfTrack m_diskKfTrack   = DiskKfTrack::Rotation;
    int         m_selectedDiskKf = -1; // index into the current track's list; -1 = none

    // Lane-enable mask timeline: piecewise-constant, one entry per sample.
    // Rebuilt whenever a disk keyframe is edited. Each entry's mask applies
    // from its startTime until the next entry's startTime.
    struct LaneMaskSample { double startTime; uint32_t mask; };
    std::vector<LaneMaskSample> m_laneMaskTimeline;
    bool m_laneMaskDirty = true;
    void rebuildLaneMaskTimeline();
    uint32_t laneMaskAt(double songTime) const;
    bool     isLaneEnabledAt(int lane, double songTime) const;
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
    // Hold tool: click-start captures head, mouse-wheel / Prev-Next navigation
    // extends the span in page units, second click commits end.
    bool  m_scanHoldAwaitEnd    = false;
    float m_scanHoldStartX      = 0.f;  // normalized [0..1]
    float m_scanHoldStartY      = 0.f;
    float m_scanHoldStartT      = 0.f;  // head time in seconds
    bool  m_scanHoldGoingUp     = false; // cached page.goingUp at start
    int   m_scanHoldExtraSweeps = 0;    // extra pages crossed

    // Slide tool: drag-to-record with per-page monotonicity.
    bool       m_scanSlideDragging = false;
    EditorNote m_scanSlideDraft{};
    bool       m_scanSlideGoingUp  = false;

    // Driven by the sidebar's "Preview Clip" group: true while the cursor
    // is inside the group OR while a click has pinned it. The pin sticks
    // through cursor-out and is only cleared by a click landing outside the
    // group, mirroring a "sticky popover" pattern.
    bool       m_showPreviewLabel = false;
    bool       m_pinPreviewLabel  = false;

    // ── Chart-audit problem-range highlight ─────────────────────────────────
    // Same hover-or-pin logic as the Preview Clip overlay: hovering an audit
    // issue button paints a translucent band on the waveform spanning that
    // issue's [timeStart, timeEnd] in a category-specific color. Clicking
    // the button pins it through cursor-out; clicking anywhere else clears
    // the pin. Both the inline toolbar audit popup and the right-sidebar
    // Chart Audit panel feed this state.
    struct AuditHighlight {
        bool  active    = false;
        float timeStart = 0.f;
        float timeEnd   = 0.f;
        unsigned int fillColor = 0;  // ImU32
        unsigned int edgeColor = 0;
    };
    AuditHighlight m_auditHover;
    AuditHighlight m_auditPin;

    // ── Arc editing state (Arcaea 3D mode) ──────────────────────────────────
    bool       m_arcPlacing     = false;  // click-to-place in progress
    EditorNote m_arcDraft;                // in-progress arc with waypoints
    int        m_arcDraftColor  = 0;      // 0=cyan, 1=pink (toolbar pick)
    float      m_heightCurveH   = 120.f;  // height editor panel pixel height
    int        m_heightDragArc  = -1;     // index of arc being height-edited
    int        m_heightDragWp   = -1;     // which waypoint is being dragged

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

    // ── Arc editing helpers ─────────────────────────────────────────────────
    void handleArcPlacement(ImVec2 origin, ImVec2 size, float startTime,
                            int trackCount, float trackH, float regionTop);
    void handleArcTapPlacement(ImVec2 origin, ImVec2 size, float startTime,
                               int trackCount, float trackH, float regionTop);
    void renderArcNotes(ImDrawList* dl, ImVec2 origin, ImVec2 size, float startTime,
                        int trackCount, float trackH, float regionTop);
    void renderArcHeightEditor(ImDrawList* dl, ImVec2 origin, ImVec2 size);
    void fixupArcTapParents(int deletedIdx);
    // Evaluate arc position at normalized time t [0..1]. Supports multi-waypoint.
    static glm::vec2 evalArcEditor(const EditorNote& arc, float t);
    static float trackToArcX(int track, int trackCount);
    static int   arcXToTrack(float arcX, int trackCount);
    // Migrate legacy 2-endpoint arc to multi-waypoint representation.
    static void ensureArcWaypoints(EditorNote& arc);

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
    // Page-based scan-line authoring. Called from renderSceneView's ScanLine
    // branch. Replaces the older full-song single-scan-line handler.
    void renderScanPageNav(ImVec2 origin, float width, class Engine* engine);
    void handleScanLinePageInput(ImVec2 origin, ImVec2 size, float curTime,
                                 bool hovered, Engine* engine);
    // Find the nearest AI-detected beat marker to `time`; return that marker
    // if within `tolerance` seconds, else return `time` unchanged.
    float snapToScanMarker(float time, float tolerance) const;

    // ── Beat analysis ────────────────────────────────────────────────────────
    AudioAnalyzer m_analyzer;
    bool m_showAnalysisError = false;
    std::string m_analysisErrorMsg;

    // ── Dynamic BPM map (from audio analysis) ────────────────────────────────
    std::vector<BpmChange> m_bpmChanges;   // tempo sections detected by AI
    float m_dominantBpm = 0.f;             // most common BPM

    // ── Editor Copilot (natural-language chart edits, pimpl-hidden) ──────────
    // Implementation in SongEditor.cpp — see CopilotState.
    struct CopilotState;
    std::unique_ptr<CopilotState> m_copilot;
    void renderCopilotPanel();
    // Right-sidebar Audit tab — same content as the old toolbar popup
    // (metrics + clickable issue buttons that paint colored bands on the
    // waveform via m_auditHover/m_auditPin).
    void renderAuditSidebarTab();
    void pollCopilot();

    // ── Chart Audit (read-only quality review, pimpl-hidden) ────────────────
    // Implementation in SongEditor.cpp — see AuditState.
    struct AuditState;
    std::unique_ptr<AuditState> m_audit;
    void renderAuditPanel();
    void pollAudit();

    // ── Style Transfer (reference-driven rebalance, pimpl-hidden) ───────────
    // Implementation in SongEditor.cpp — see StyleState.
    struct StyleState;
    std::unique_ptr<StyleState> m_style;
    void renderStylePanel();
    void pollStyle();

    // ── AI Place-All tuning (session-local; not persisted) ───────────────────
    float m_autoFlickPct      = 0.88f;  // 0.5..1.0 — strength percentile → Flick
    float m_autoHoldMin       = 0.20f;  // 0.05..0.80 — sustain seconds → Hold
    bool  m_autoAntiJack      = true;   // nudge same-lane repeats
    float m_autoLaneCooldownMs = 80.f;  // 0..400 — min ms between notes on same lane
    float m_autoScanTimeGapMs  = 60.f;  // 0..300 — min ms between any two ScanLine notes
    float m_autoMarkerThinNps  = 4.f;   // 1..10 — target NPS inside flagged density hotspots

    // Single-shot Thin Markers undo. Snapshot is per-difficulty (the difficulty
    // active at click time); Undo button only enables when this matches the
    // currently selected difficulty so we don't restore into the wrong stream.
    std::vector<float>           m_thinUndoMarkers;
    std::vector<MarkerFeature>   m_thinUndoFeatures;
    int                          m_thinUndoDifficulty = -1;

    // ── Test Game popup ──────────────────────────────────────────────────────
    bool m_showTestError = false;
    std::string m_testErrorMsg;
};
