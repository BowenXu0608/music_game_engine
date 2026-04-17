#include "ProjectHub.h"
#include "engine/Engine.h"
#include <imgui.h>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <cctype>
#include <cstdlib>
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <shellapi.h>
#endif

namespace fs = std::filesystem;

// ── helpers ──────────────────────────────────────────────────────────────────

static std::string sanitizeName(const char* src) {
    std::string out;
    for (; *src; ++src) {
        char c = *src;
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
            out += c;
        else if (c == ' ')
            out += '_';
    }
    return out;
}

// ── scan ─────────────────────────────────────────────────────────────────────

void ProjectHub::scanProjects() {
    if (m_scanned) return;
    m_projects.clear();

    fs::path projectsDir = "../../Projects";
    if (!fs::exists(projectsDir)) return;

    for (const auto& entry : fs::directory_iterator(projectsDir)) {
        if (!entry.is_directory()) continue;
        auto projectJson = entry.path() / "project.json";
        if (!fs::exists(projectJson)) continue;
        try {
            std::ifstream f(projectJson);
            auto j = nlohmann::json::parse(f);
            ProjectInfo info;
            info.name         = j["name"];
            info.version      = j.value("version", "1.0.0");
            info.path         = fs::absolute(entry.path()).string();
            info.defaultChart = j.value("defaultChart", "");
            info.shaderPath   = j["paths"].value("shaders", "../../build/shaders");
            m_projects.push_back(std::move(info));
        } catch (...) {}
    }
    m_scanned = true;
}

// ── project creation ─────────────────────────────────────────────────────────

bool ProjectHub::createProject(const std::string& name) {
    std::string safe = sanitizeName(name.c_str());
    if (safe.empty()) {
        m_createError = "Name contains no valid characters.";
        return false;
    }

    fs::path projectDir = fs::path("../../Projects") / safe;
    if (fs::exists(projectDir)) {
        m_createError = "A project named '" + safe + "' already exists.";
        return false;
    }

    try {
        fs::create_directories(projectDir / "assets" / "charts");
        fs::create_directories(projectDir / "assets" / "audio");
        fs::create_directories(projectDir / "assets" / "textures");

        // project.json
        nlohmann::json proj;
        proj["name"]             = name;
        proj["version"]          = "1.0.0";
        proj["engineVersion"]    = "1.0.0";
        proj["window"]["width"]  = 1280;
        proj["window"]["height"] = 720;
        proj["window"]["title"]  = name;
        proj["paths"]["charts"]  = "assets/charts";
        proj["paths"]["audio"]   = "assets/audio";
        proj["paths"]["shaders"] = "../../build/shaders";
        proj["defaultChart"]     = "assets/charts/demo.json";
        std::ofstream(projectDir / "project.json") << proj.dump(2);

        // start_screen.json — new nested format
        nlohmann::json ss;
        ss["background"]["file"]           = "";
        ss["background"]["type"]           = "none";
        ss["logo"]["type"]                 = "text";
        ss["logo"]["text"]                 = name;
        ss["logo"]["fontSize"]             = 48.f;
        ss["logo"]["color"]                = {1.f, 1.f, 1.f, 1.f};
        ss["logo"]["bold"]                 = false;
        ss["logo"]["italic"]               = false;
        ss["logo"]["imageFile"]            = "";
        ss["logo"]["glow"]                 = false;
        ss["logo"]["glowColor"]            = {1.f, 0.8f, 0.2f, 0.8f};
        ss["logo"]["glowRadius"]           = 8.f;
        ss["logo"]["position"]             = {{"x", 0.5f}, {"y", 0.3f}};
        ss["logo"]["scale"]                = 1.f;
        ss["tapText"]                      = "Tap to Start";
        ss["tapTextPosition"]["x"]         = 0.5f;
        ss["tapTextPosition"]["y"]         = 0.8f;
        ss["tapTextSize"]                  = 24;
        ss["transition"]["effect"]         = "fade";
        ss["transition"]["duration"]       = 0.5f;
        ss["transition"]["customScript"]   = "";
        std::ofstream(projectDir / "start_screen.json") << ss.dump(2);

        // stub demo chart
        nlohmann::json chart;
        chart["format"]       = "UCF";
        chart["version"]      = "1.0";
        chart["title"]        = name;
        chart["artist"]       = "Unknown";
        chart["offset"]       = 0.0f;
        chart["timingPoints"] = nlohmann::json::array();
        chart["notes"]        = nlohmann::json::array();
        std::ofstream(projectDir / "assets" / "charts" / "demo.json") << chart.dump(2);

    } catch (const std::exception& e) {
        m_createError = std::string("Failed: ") + e.what();
        return false;
    }

    m_createError.clear();
    m_scanned = false; // force rescan
    return true;
}

// ── create dialog ─────────────────────────────────────────────────────────────

void ProjectHub::renderCreateDialog(Engine* engine) {
    if (!m_showCreateDialog) return;

    ImVec2 center{ImGui::GetIO().DisplaySize.x * 0.5f,
                  ImGui::GetIO().DisplaySize.y * 0.5f};
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440, 220), ImGuiCond_Always);
    ImGui::Begin("Create New Game", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);

    ImGui::Spacing();
    ImGui::Text("Project Name");
    ImGui::SetNextItemWidth(-1);
    bool hitEnter = ImGui::InputText("##name", m_newProjectName,
                                     sizeof(m_newProjectName),
                                     ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Spacing();
    std::string safe = sanitizeName(m_newProjectName);
    ImGui::TextDisabled("Folder: Projects/%s", safe.empty() ? "..." : safe.c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!m_createError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.35f, 0.35f, 1.f));
        ImGui::TextWrapped("%s", m_createError.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    bool canCreate = !safe.empty();
    if (!canCreate) ImGui::BeginDisabled();
    bool doCreate = ImGui::Button("Create", ImVec2(110, 32)) || (hitEnter && canCreate);
    if (!canCreate) ImGui::EndDisabled();

    if (doCreate) {
        if (createProject(m_newProjectName)) {
            // open the new project immediately
            fs::path projectDir = fs::absolute(fs::path("../../Projects") / safe);
            if (engine) {
                engine->startScreenEditor().load(projectDir.string());
                engine->switchLayer(EditorLayer::StartScreen);
            }
            m_showCreateDialog = false;
            memset(m_newProjectName, 0, sizeof(m_newProjectName));
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 32))) {
        m_showCreateDialog = false;
        m_createError.clear();
        memset(m_newProjectName, 0, sizeof(m_newProjectName));
    }

    ImGui::End();
}

// ── main render ───────────────────────────────────────────────────────────────

void ProjectHub::render(Engine* engine) {
    scanProjects();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Project Hub", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    ImGui::SetCursorPosY(50);
    ImGui::Text("Music Game Engine - Project Hub");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("+ Create Game", ImVec2(150, 36)))
        m_showCreateDialog = true;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_projects.empty()) {
        ImGui::TextDisabled("No projects found. Create one to get started.");
    } else {
        for (const auto& proj : m_projects) {
            ImGui::PushID(proj.name.c_str());
            if (ImGui::Button(proj.name.c_str(), ImVec2(300, 50))) {
                m_selectedProject = proj;
                m_projectSelected = true;
                if (engine) {
                    engine->startScreenEditor().load(proj.path);
                    engine->switchLayer(EditorLayer::StartScreen);
                }
                if (m_launchCallback) m_launchCallback(proj);
            }
            ImGui::SameLine();
            ImGui::Text("v%s", proj.version.c_str());
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(20, 0));
            ImGui::SameLine();
            bool disableApk = m_apkRunning;
            if (disableApk) ImGui::BeginDisabled();
            if (ImGui::Button("Build APK", ImVec2(110, 28))) {
                startApkBuild(proj);
            }
            if (disableApk) ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    ImGui::End();

    renderCreateDialog(engine);
    renderApkDialog();
}

// ── APK build ────────────────────────────────────────────────────────────────

void ProjectHub::startApkBuild(const ProjectInfo& proj) {
    if (m_apkRunning) return;

    // Default output: <Desktop>/<ProjectName>.apk
    fs::path desktop;
#ifdef _WIN32
    if (const char* up = std::getenv("USERPROFILE"))
        desktop = fs::path(up) / "Desktop";
#endif
    if (desktop.empty() || !fs::exists(desktop))
        desktop = fs::current_path();

    std::string safeName = sanitizeName(proj.name.c_str());
    if (safeName.empty()) safeName = "game";
    fs::path outputApk = desktop / (safeName + ".apk");
    fs::path logPath   = fs::temp_directory_path() / (safeName + "_apk_build.log");

    // Resolve script path relative to CWD (build/Release or similar)
    fs::path script = fs::absolute("../../tools/build_apk.bat");
    if (!fs::exists(script)) script = fs::absolute("tools/build_apk.bat");

    m_apkProjectName = proj.name;
    m_apkOutputPath  = outputApk.string();
    m_apkLogPath     = logPath.string();
    m_apkRunning     = true;
    m_showApkDialog  = true;
    m_apkExitCode    = 0;

    std::string projectPath = proj.path;
    std::string scriptStr   = script.string();
    std::string outStr      = outputApk.string();
    std::string logStr      = logPath.string();

    m_apkFuture = std::async(std::launch::async, [scriptStr, projectPath, outStr, logStr]() -> int {
        std::string cmd = "\"\"" + scriptStr + "\" \"" + projectPath +
                          "\" \"" + outStr + "\" > \"" + logStr + "\" 2>&1\"";
        return std::system(cmd.c_str());
    });
}

void ProjectHub::renderApkDialog() {
    if (!m_showApkDialog) return;

    if (m_apkRunning && m_apkFuture.valid() &&
        m_apkFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_apkExitCode = m_apkFuture.get();
        m_apkRunning  = false;
    }

    ImVec2 center{ImGui::GetIO().DisplaySize.x * 0.5f,
                  ImGui::GetIO().DisplaySize.y * 0.5f};
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 240), ImGuiCond_Always);
    ImGui::Begin("Build APK", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);

    ImGui::Text("Project: %s", m_apkProjectName.c_str());
    ImGui::Text("Output:  %s", m_apkOutputPath.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    if (m_apkRunning) {
        ImGui::TextColored(ImVec4(1.f, 0.85f, 0.2f, 1.f),
                           "Building... (running Gradle, this can take a few minutes)");
        ImGui::Spacing();
        ImGui::TextDisabled("Log: %s", m_apkLogPath.c_str());
    } else if (m_apkExitCode == 0) {
        ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "BUILD SUCCESSFUL");
        ImGui::Spacing();
        ImGui::TextWrapped("APK saved to:\n  %s", m_apkOutputPath.c_str());
        ImGui::Spacing();
#ifdef _WIN32
        if (ImGui::Button("Show in Explorer", ImVec2(160, 28))) {
            std::string arg = "/select,\"" + m_apkOutputPath + "\"";
            ShellExecuteA(nullptr, "open", "explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
#endif
    } else {
        ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f),
                           "BUILD FAILED (exit %d)", m_apkExitCode);
        ImGui::Spacing();
        ImGui::TextWrapped("See log for details:\n  %s", m_apkLogPath.c_str());
        ImGui::Spacing();
#ifdef _WIN32
        if (ImGui::Button("Open Log", ImVec2(160, 28))) {
            ShellExecuteA(nullptr, "open", m_apkLogPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
#endif
    }

    bool canClose = !m_apkRunning;
    if (!canClose) ImGui::BeginDisabled();
    if (ImGui::Button("Close", ImVec2(110, 28))) {
        m_showApkDialog = false;
    }
    if (!canClose) ImGui::EndDisabled();

    ImGui::End();
}
