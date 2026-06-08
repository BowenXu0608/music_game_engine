#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <future>

class Engine;

// ── Per-song game mode configuration ─────────────────────────────────────────

enum class GameModeType { DropNotes, Circle, ScanLine };
enum class DropDimension { TwoD, ThreeD };
enum class CameraProjection { Perspective, Orthographic };

// Per-dimension default free-camera values for drop modes. Single source of
// truth for the JSON loader's fallbacks AND the editor's per-control Reset
// buttons, so they can never drift. Defaults reproduce the legacy framing.
struct CameraDefaults {
    CameraProjection projection = CameraProjection::Perspective;
    float position[3]{};
    float rotationDeg[3]{};
    float fovYDeg{};
    float orthoSize{};
    float nearClip{};
    float farClip{};
    float playfieldWidth{};
    float playfieldLength{};
};

inline CameraDefaults cameraDefaultsFor(DropDimension dim) {
    bool is3D = (dim == DropDimension::ThreeD);
    CameraDefaults d;
    d.projection      = CameraProjection::Perspective;
    d.position[0]     = 0.f;
    d.position[1]     = is3D ? 3.f : 5.f;
    d.position[2]     = is3D ? 10.f : 8.f;
    d.rotationDeg[0]  = is3D ? -16.699f : -8.882f;
    d.rotationDeg[1]  = 0.f;
    d.rotationDeg[2]  = 0.f;
    d.fovYDeg         = is3D ? 45.f : 55.f;
    d.orthoSize       = 6.f;
    d.nearClip        = 0.f;     // fixed; not author-exposed
    d.farClip         = 300.f;   // fixed; not author-exposed
    d.playfieldWidth  = is3D ? 6.f : 14.f;
    d.playfieldLength = is3D ? 60.f : 50.f;
    return d;
}

// ── HUD text element (logo-style text rendering config) ─────────────────────
struct HudTextConfig {
    float pos[2]     = {0.5f, 0.5f};   // normalized position (0..1)
    float fontSize   = 24.f;
    float color[4]   = {1.f, 1.f, 1.f, 1.f};
    float scale      = 1.f;
    bool  bold       = false;
    bool  glow       = false;
    float glowColor[4] = {1.f, 0.8f, 0.2f, 0.8f};
    float glowRadius   = 6.f;
};

// Floating judgment-text labels + placement. Engine users can change the
// wording (e.g. localize, or use "PURE/FAR/LOST") and where the text sits.
struct JudgmentLabels {
    std::string perfect   = "PERFECT";
    std::string goodEarly = "EARLY";
    std::string goodLate  = "LATE";
    std::string badEarly  = "TOO EARLY";
    std::string badLate   = "TOO LATE";
    std::string miss      = "MISS";
    float yOffset  = 0.05f;   // normalized height ABOVE the judgment line
    float fontSize = 22.f;    // base px (auto-shrunk to fit one lane width)
    bool  enabled  = true;
};

struct GameModeConfig {
    GameModeType  type       = GameModeType::DropNotes;
    DropDimension dimension  = DropDimension::TwoD;
    int           trackCount = 7;

    // Floating judgment text (PERFECT / EARLY-LATE / TOO EARLY-LATE / MISS).
    JudgmentLabels judgmentLabels;

    // Judgment windows (in milliseconds, +/- from note center)
    float perfectMs = 50.f;   // +/- 50ms
    float goodMs    = 100.f;  // +/- 100ms
    float badMs     = 150.f;  // +/- 150ms
    // Beyond badMs = Miss

    // Score per judgment (usually auto-derived from totalScore / event count,
    // but still stored so users can manually override).
    int perfectScore = 1000;
    int goodScore    = 600;
    int badScore     = 200;
    // Miss = 0

    // Total max score for an All-Perfect run. Editor derives per-judgment
    // values from this and the current note/sample-point count.
    int totalScore = 1000000;

    // Achievement images (relative paths from project root)
    std::string fcImage;   // Full Combo badge image
    std::string apImage;   // All Perfect badge image

    // HUD text elements
    HudTextConfig scoreHud  = {{0.85f, 0.06f}, 24.f, {1.f,1.f,1.f,1.f}, 1.f, true, false, {}, 6.f};
    HudTextConfig comboHud  = {{0.50f, 0.18f}, 32.f, {1.f,0.9f,0.3f,1.f}, 1.f, true, true, {1.f,0.8f,0.2f,0.6f}, 8.f};

    // Audio offset: delay (seconds) before notes start to sync with audio
    float audioOffset = 0.f;

    // ── Free 3D camera (drop modes) ──────────────────────────────────────
    // The game world is a real 3D space; the playfield is a plane in it, and
    // this camera looks at it (Unity-style). Defaults reproduce the legacy 2D
    // framing (lookAt eye{0,5,8} -> target{0,0,-24}, FOV 55). The JSON loader
    // substitutes the 3D defaults when dimension==ThreeD and keys are absent.
    CameraProjection cameraProjection = CameraProjection::Perspective;
    float cameraPosition[3]    = {0.f, 5.f, 8.f};       // world eye position
    float cameraRotationDeg[3] = {-8.882f, 0.f, 0.f};   // Euler XYZ (pitch,yaw,roll)
    float cameraFovYDeg   = 55.f;     // perspective vertical FOV
    float cameraOrthoSize = 6.f;      // ortho half view-height (world units)
    float cameraNearClip  = 0.f;      // fixed (not author-exposed)
    float cameraFarClip   = 300.f;    // fixed (not author-exposed)

    // Playfield plane dimensions in world units (drop modes). 2D defaults are
    // chosen so the default camera frames the highway like the legacy look
    // (~90% screen width, ~50-unit runway); 3D defaults are 6 / 60.
    float playfieldWidth  = 14.f;     // plane X extent (full width)
    float playfieldLength = 50.f;     // plane Z depth toward the vanishing point

    // 3D DropNotes: sky judgment line height (world Y).
    // Arc height [0..1] maps from ground (GROUND_Y) to this value.
    float skyHeight = 1.f;

    // Background image for gameplay (relative path from project root)
    std::string backgroundImage;

    // ── Circle-mode disk defaults ────────────────────────────────────────
    // Used only when type == Circle. These override the renderer's
    // compile-time defaults so each song can tune its own disk layout.
    float diskInnerRadius  = 0.9f;   // inner spawn disk radius (world units)
    float diskBaseRadius   = 2.4f;   // outer hit-ring radius  (world units)
    float diskRingSpacing  = 0.6f;   // spacing between extra rings
    float diskInitialScale = 1.0f;   // initial scale applied before keyframes

    // ── Per-note-type asset overrides ───────────────────────────────────
    // Set from the Song Editor's Note tab (drag-drop or Browse). Keyed by
    // note-type display name (e.g. "Click Note", "Hold Note", "Arc Note").
    // Values are project-relative paths. These are assignment-only today —
    // the renderer/audio system will pick them up once it's wired through.
    struct NoteTypeAssets {
        std::string texturePath;   // image dragged onto the note type
        std::string sfxPath;       // hit-sound audio for the note type
    };
    std::map<std::string, NoteTypeAssets> noteAssets;

    // ── Per-event particle-effect bindings ──────────────────────────────
    // Keyed by particle event-slot slug (see renderer/ParticleSlots.h:
    // "click_hit", "flick_hit", "hold_head", "hold_tick", "hold_aura",
    // "hold_end"). Value = ParticleEffectAsset name resolved at hit time via
    // the project's ParticleEffectLibrary. Empty/absent = use the mode's
    // seeded default (`default_<mode>_<slug>`).
    std::map<std::string, std::string> particleEffects;
};

// ── Project info ─────────────────────────────────────────────────────────────

struct ProjectInfo {
    std::string name;
    std::string path;
    std::string version;
    std::string defaultChart;
    std::string shaderPath;
    std::string lastModified;     // formatted "YYYY-MM-DD HH:MM"
    long long   lastModifiedRaw = 0; // unix seconds — for sorting
};

class ProjectHub {
public:
    using LaunchCallback = std::function<void(const ProjectInfo&)>;

    void setLaunchCallback(LaunchCallback cb) { m_launchCallback = cb; }
    void render(Engine* engine);
    bool hasSelectedProject() const { return m_projectSelected; }
    const ProjectInfo& getSelectedProject() const { return m_selectedProject; }

private:
    void scanProjects();
    void renderCreateDialog(Engine* engine);
    void renderAddFileDialog();
    bool createProject(const std::string& name);
    bool importProject(const std::string& srcPath);
    void startApkBuild(const ProjectInfo& proj);
    void renderApkDialog();

    std::vector<ProjectInfo> m_projects;
    ProjectInfo              m_selectedProject;
    LaunchCallback           m_launchCallback;
    bool                     m_scanned         = false;
    bool                     m_projectSelected = false;
    int                      m_selectedIdx     = -1;  // highlight in hub list

    char        m_searchBuf[128] = {};

    bool        m_showCreateDialog = false;
    char        m_newProjectName[128] = {};
    std::string m_createError;

    bool        m_showAddFileDialog  = false;
    char        m_addFilePath[512]   = {};
    std::string m_addFileError;

    // APK build state
    bool             m_showApkDialog  = false;
    bool             m_apkRunning     = false;
    int              m_apkExitCode    = 0;
    std::string      m_apkProjectName;
    std::string      m_apkOutputPath;
    std::string      m_apkLogPath;
    std::string      m_apkStagingPath;   // pruned project copy used for packaging
    std::future<int> m_apkFuture;
};
