#pragma once
#include <string>
#include <vector>
#include <functional>
#include <future>

class Engine;

// ── Per-song game mode configuration ─────────────────────────────────────────

enum class GameModeType { DropNotes, Circle, ScanLine };
enum class DropDimension { TwoD, ThreeD };

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

struct GameModeConfig {
    GameModeType  type       = GameModeType::DropNotes;
    DropDimension dimension  = DropDimension::TwoD;
    int           trackCount = 7;

    // Judgment windows (in milliseconds, +/- from note center)
    float perfectMs = 50.f;   // +/- 50ms
    float goodMs    = 100.f;  // +/- 100ms
    float badMs     = 150.f;  // +/- 150ms
    // Beyond badMs = Miss

    // Score per judgment
    int perfectScore = 1000;
    int goodScore    = 600;
    int badScore     = 200;
    // Miss = 0

    // Achievement images (relative paths from project root)
    std::string fcImage;   // Full Combo badge image
    std::string apImage;   // All Perfect badge image

    // HUD text elements
    HudTextConfig scoreHud  = {{0.85f, 0.06f}, 24.f, {1.f,1.f,1.f,1.f}, 1.f, true, false, {}, 6.f};
    HudTextConfig comboHud  = {{0.50f, 0.18f}, 32.f, {1.f,0.9f,0.3f,1.f}, 1.f, true, true, {1.f,0.8f,0.2f,0.6f}, 8.f};

    // Audio offset: delay (seconds) before notes start to sync with audio
    float audioOffset = 0.f;

    // Camera settings for gameplay view
    float cameraEye[3]    = {0.f, 12.f, 14.f};
    float cameraTarget[3] = {0.f, 0.f, -20.f};
    float cameraFov       = 55.f;

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
};

// ── Project info ─────────────────────────────────────────────────────────────

struct ProjectInfo {
    std::string name;
    std::string path;
    std::string version;
    std::string defaultChart;
    std::string shaderPath;
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
    bool createProject(const std::string& name);
    void startApkBuild(const ProjectInfo& proj);
    void renderApkDialog();

    std::vector<ProjectInfo> m_projects;
    ProjectInfo              m_selectedProject;
    LaunchCallback           m_launchCallback;
    bool                     m_scanned         = false;
    bool                     m_projectSelected = false;

    bool        m_showCreateDialog = false;
    char        m_newProjectName[128] = {};
    std::string m_createError;

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
