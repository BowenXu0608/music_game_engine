#include "ProjectHub.h"
#include "StyleTokens.h"
#include "Widgets.h"
#include "engine/Engine.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
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

// ── packaging helpers ───────────────────────────────────────────────────────
//
// The editor keeps per-song charts for every game mode that the author ever
// touched (circle, drop2d, drop3d, scan, ...). When we actually package a
// game the user only wants the currently-selected mode to ship — prior modes
// become dead weight in the APK. We solve this without disturbing the live
// project by copying the project into a staging folder and pruning the
// unused charts there, then pointing the build script at the staging copy.

namespace {

// Collect the set of chart basenames referenced by music_selection.json for
// every song. These are the files we must keep in the package. Also emits
// the set of song names that appeared, which lets the prune step know which
// files are "per-song" (vs. demos / shared charts that we never touch).
void collectKeepSet(const fs::path& stagingRoot,
                    std::set<std::string>& keepOut,
                    std::set<std::string>& songNamesOut) {
    fs::path msPath = stagingRoot / "music_selection.json";
    if (!fs::exists(msPath)) return;
    try {
        std::ifstream f(msPath);
        auto j = nlohmann::json::parse(f);
        if (!j.contains("sets")) return;
        for (const auto& st : j["sets"]) {
            if (!st.contains("songs")) continue;
            for (const auto& song : st["songs"]) {
                if (song.contains("name") && song["name"].is_string())
                    songNamesOut.insert(song["name"].get<std::string>());
                for (const char* key : {"chartEasy", "chartMedium", "chartHard"}) {
                    if (!song.contains(key) || !song[key].is_string()) continue;
                    std::string p = song[key].get<std::string>();
                    if (p.empty()) continue;
                    std::replace(p.begin(), p.end(), '\\', '/');
                    auto slash = p.rfind('/');
                    keepOut.insert(slash == std::string::npos ? p : p.substr(slash + 1));
                }
            }
        }
    } catch (...) {
        // music_selection.json missing or malformed → nothing to prune.
    }
}

// Walk `<stagingRoot>/assets/charts/` and delete every `<song>_*.json` that
// isn't referenced by music_selection.json. Files whose name doesn't start
// with any known song prefix are left alone (covers demo.json and any
// charts the author added outside the mode-keyed naming scheme).
void prunePackagedCharts(const fs::path& stagingRoot) {
    std::set<std::string> keepNames;
    std::set<std::string> songNames;
    collectKeepSet(stagingRoot, keepNames, songNames);
    if (songNames.empty()) return;

    fs::path chartsDir = stagingRoot / "assets" / "charts";
    if (!fs::exists(chartsDir)) return;

    for (const auto& entry : fs::directory_iterator(chartsDir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        std::string fname = entry.path().filename().string();

        bool belongsToSong = false;
        for (const auto& sn : songNames) {
            if (fname.size() > sn.size() + 1 &&
                fname.compare(0, sn.size(), sn) == 0 &&
                fname[sn.size()] == '_') {
                belongsToSong = true;
                break;
            }
        }
        if (!belongsToSong) continue;          // not a per-song chart — keep
        if (keepNames.count(fname)) continue;  // currently-active mode — keep

        std::error_code ec;
        fs::remove(entry.path(), ec);
    }
}

// Build a staging copy of the project containing only the files the APK
// build needs, with obsolete mode charts pruned. Returns the staging path,
// or an empty path on failure (in which case the caller should fall back to
// the original project path).
fs::path stageProjectForPackaging(const fs::path& projectRoot,
                                  const std::string& safeName) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto ts  = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    fs::path staging = fs::temp_directory_path() /
                       (safeName + "_apk_stage_" + std::to_string(ts));
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    if (ec) return {};

    // Mirror what build_apk.bat pulls in: top-level JSONs + assets/.
    constexpr const char* topFiles[] = {
        "project.json", "start_screen.json", "music_selection.json",
    };
    for (const char* name : topFiles) {
        fs::path src = projectRoot / name;
        if (fs::exists(src))
            fs::copy_file(src, staging / name, fs::copy_options::overwrite_existing, ec);
    }
    fs::path assetsSrc = projectRoot / "assets";
    if (fs::exists(assetsSrc)) {
        fs::copy(assetsSrc, staging / "assets",
                 fs::copy_options::recursive |
                 fs::copy_options::overwrite_existing, ec);
    }
    if (ec) {
        fs::remove_all(staging, ec);
        return {};
    }

    prunePackagedCharts(staging);
    return staging;
}

} // namespace

// ── scan ─────────────────────────────────────────────────────────────────────

namespace {

// Walk the project folder and return the most-recent file write time.
// Falls back to the folder's own mtime if iteration fails.
fs::file_time_type latestMtime(const fs::path& root) {
    std::error_code ec;
    fs::file_time_type latest = fs::last_write_time(root, ec);
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return latest;
    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        auto t = it->last_write_time(ec);
        if (!ec && t > latest) latest = t;
    }
    return latest;
}

// Convert filesystem file_time to a localtime-formatted string + raw seconds.
// Using duration-offset trick (no MSVC clock_cast required on older runtimes).
void formatMtime(fs::file_time_type ft, std::string& out, long long& rawSec) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    rawSec = static_cast<long long>(tt);
    char buf[64] = {};
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    tmv = *std::localtime(&tt);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
    out = buf;
}

} // namespace

// ── starred persistence ─────────────────────────────────────────────────────
//
// Stored alongside the Projects/ folder so the user's per-machine
// preferences travel with the workspace, not with the engine binary.
// Underscore prefix keeps it from being mistaken for a project folder by
// the scanProjects directory walk (which already requires project.json).

void ProjectHub::loadStarred() {
    if (m_starredLoaded) return;
    m_starredLoaded = true;
    fs::path p = "../../Projects/_hub_state.json";
    if (!fs::exists(p)) return;
    try {
        std::ifstream f(p);
        auto j = nlohmann::json::parse(f, nullptr, false);
        if (!j.is_object() || !j.contains("starred")) return;
        for (const auto& s : j["starred"]) {
            if (s.is_string()) m_starred.insert(s.get<std::string>());
        }
    } catch (...) {
        // Tolerant: malformed file just yields an empty starred set.
    }
}

void ProjectHub::saveStarred() {
    fs::path p = "../../Projects/_hub_state.json";
    nlohmann::json j;
    j["starred"] = nlohmann::json::array();
    for (const auto& s : m_starred) j["starred"].push_back(s);
    // Per the user-data writes fail loud rule: refuse to write on dump
    // throw rather than silently swallow / replace bad bytes.
    try {
        std::string text = j.dump(2);
        std::ofstream(p) << text;
    } catch (...) {
        // Leave the existing file untouched on error.
    }
}

void ProjectHub::revealInExplorer(const std::string& projectPath) {
#ifdef _WIN32
    // Use a sentinel inside the folder so Explorer opens *in* it instead of
    // selecting the folder itself in its parent. project.json is guaranteed
    // to exist (scanProjects requires it).
    fs::path proj = fs::path(projectPath) / "project.json";
    std::string arg = "/select,\"" + proj.string() + "\"";
    ShellExecuteA(nullptr, "open", "explorer.exe",
                  arg.c_str(), nullptr, SW_SHOWNORMAL);
#else
    (void)projectPath;
#endif
}

// ── small primitive icons (no font glyphs — CP936-safe) ───────────────────

namespace {

// Draw a 14×14 gear at (cx,cy) using six trapezoid teeth + center hole.
void drawGearIcon(ImDrawList* dl, ImVec2 c, ImU32 color) {
    constexpr int teeth = 6;
    const float rOuter = 7.f;
    const float rInner = 5.f;
    const float toothHalf = 0.20f; // radians
    for (int i = 0; i < teeth; ++i) {
        const float a = (float)i * (3.14159265f * 2.f / teeth);
        const float a0 = a - toothHalf;
        const float a1 = a + toothHalf;
        ImVec2 p0{c.x + std::cos(a0) * rInner, c.y + std::sin(a0) * rInner};
        ImVec2 p1{c.x + std::cos(a0) * rOuter, c.y + std::sin(a0) * rOuter};
        ImVec2 p2{c.x + std::cos(a1) * rOuter, c.y + std::sin(a1) * rOuter};
        ImVec2 p3{c.x + std::cos(a1) * rInner, c.y + std::sin(a1) * rInner};
        ImVec2 quad[4] = {p0, p1, p2, p3};
        dl->AddConvexPolyFilled(quad, 4, color);
    }
    dl->AddCircleFilled(c, rInner, color, 24);
    // Hub hole — punch with bg color.
    dl->AddCircleFilled(c, 2.0f, IM_COL32(0, 0, 0, 255), 16);
}

// Draw a 14×14 star at center. Filled when `solid`, outline otherwise.
void drawStarIcon(ImDrawList* dl, ImVec2 c, float r, ImU32 color, bool solid) {
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        const float a = -3.14159265f * 0.5f + (float)i * (3.14159265f / 5.f);
        const float rr = (i & 1) ? r * 0.42f : r;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    if (solid) dl->AddConvexPolyFilled(pts, 10, color);
    else       dl->AddPolyline(pts, 10, color, ImDrawFlags_Closed, 1.5f);
}

// Draw a small folder glyph (12×9) for the "All projects" rail row.
void drawFolderIcon(ImDrawList* dl, ImVec2 c, ImU32 color) {
    const ImVec2 a{c.x - 7.f, c.y - 4.f};
    const ImVec2 b{c.x - 1.f, c.y - 4.f};
    const ImVec2 d{c.x,       c.y - 2.f};
    const ImVec2 e{c.x + 7.f, c.y - 2.f};
    const ImVec2 f{c.x + 7.f, c.y + 5.f};
    const ImVec2 g{c.x - 7.f, c.y + 5.f};
    ImVec2 tab[4] = {a, b, d, {c.x - 1.f, c.y - 2.f}};
    dl->AddConvexPolyFilled(tab, 4, color);
    dl->AddRectFilled({a.x, c.y - 2.f}, f, color, 1.5f);
    (void)e; (void)g;
}

// Draw a clock glyph (circle + two hands) for the "Recent" rail row.
void drawClockIcon(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddCircle(c, 6.5f, color, 20, 1.5f);
    dl->AddLine(c, {c.x, c.y - 4.f}, color, 1.5f);
    dl->AddLine(c, {c.x + 3.5f, c.y + 1.f}, color, 1.5f);
}

// Draw a funnel glyph (downward-narrowing chevron with stem).
void drawFunnelIcon(ImDrawList* dl, ImVec2 c, ImU32 color) {
    ImVec2 top0{c.x - 7.f, c.y - 5.f};
    ImVec2 top1{c.x + 7.f, c.y - 5.f};
    ImVec2 mid0{c.x - 1.f, c.y + 1.f};
    ImVec2 mid1{c.x + 1.f, c.y + 1.f};
    ImVec2 quad[4] = {top0, top1, mid1, mid0};
    dl->AddConvexPolyFilled(quad, 4, color);
    dl->AddRectFilled({c.x - 1.f, c.y + 1.f}, {c.x + 1.f, c.y + 6.f}, color);
}

} // namespace

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
            formatMtime(latestMtime(entry.path()), info.lastModified, info.lastModifiedRaw);

            // Read first song's gameMode for the row pill + filter.
            // Tolerates missing/malformed files — the field defaults to Drop2D.
            try {
                auto msPath = entry.path() / "music_selection.json";
                if (fs::exists(msPath)) {
                    std::ifstream msf(msPath);
                    auto ms = nlohmann::json::parse(msf, nullptr, false);
                    int songs = 0;
                    if (ms.contains("sets") && ms["sets"].is_array()) {
                        for (const auto& s : ms["sets"]) {
                            if (s.contains("songs") && s["songs"].is_array())
                                songs += (int)s["songs"].size();
                        }
                        // Pull the first song's gameMode if present.
                        for (const auto& s : ms["sets"]) {
                            if (!s.contains("songs") || !s["songs"].is_array()) continue;
                            if (s["songs"].empty()) continue;
                            const auto& song0 = s["songs"][0];
                            if (!song0.contains("gameMode")) break;
                            const auto& gm = song0["gameMode"];
                            std::string typeStr  = gm.value("type", "DropNotes");
                            std::string dimStr   = gm.value("dimension", "2D");
                            if      (typeStr == "Circle")    info.gameMode = GameModeType::Circle;
                            else if (typeStr == "ScanLine")  info.gameMode = GameModeType::ScanLine;
                            else                              info.gameMode = GameModeType::DropNotes;
                            info.gameDim = (dimStr == "3D") ? DropDimension::ThreeD : DropDimension::TwoD;
                            break;
                        }
                    }
                    info.songCount = songs;
                }
            } catch (...) {}

            m_projects.push_back(std::move(info));
        } catch (...) {}
    }

    // Newest-modified first — users usually want to see what they just touched.
    std::sort(m_projects.begin(), m_projects.end(),
              [](const ProjectInfo& a, const ProjectInfo& b) {
                  return a.lastModifiedRaw > b.lastModifiedRaw;
              });

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

// ── render-side helpers (file-local, ImGui-only typography) ──────────────

namespace {

struct ModeView { const char* label; ImVec4 color; };

ModeView modeViewFor(const ProjectInfo& proj) {
    using namespace ui::tokens;
    switch (proj.gameMode) {
        case GameModeType::DropNotes:
            return (proj.gameDim == DropDimension::ThreeD)
                ? ModeView{"Drop 3D", Magenta}
                : ModeView{"Drop 2D", Cyan};
        case GameModeType::ScanLine: return {"Scan Line", Amber};
        case GameModeType::Circle:   return {"Circle",    Lime};
    }
    return {"Drop 2D", Cyan};
}

int modeFilterIdxFor(const ProjectInfo& proj) {
    switch (proj.gameMode) {
        case GameModeType::DropNotes:
            return (proj.gameDim == DropDimension::ThreeD) ? 1 : 0;
        case GameModeType::ScanLine: return 2;
        case GameModeType::Circle:   return 3;
    }
    return 0;
}

// Clean gear: filled circle + 8 small square teeth at the rim + dark hub.
void drawGearClean(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddCircleFilled(c, 5.f, color, 16);
    for (int i = 0; i < 8; ++i) {
        const float a = (float)i * 3.14159265f * 0.25f;
        const ImVec2 p{c.x + std::cos(a) * 7.f, c.y + std::sin(a) * 7.f};
        dl->AddRectFilled({p.x - 1.5f, p.y - 1.5f}, {p.x + 1.5f, p.y + 1.5f}, color);
    }
    dl->AddCircleFilled(c, 1.8f, IM_COL32(0, 0, 0, 255), 12);
}

} // namespace

// ── render orchestrator ──────────────────────────────────────────────────

namespace {

// Forward-declared inline lambdas would pollute scope; keep these as
// file-local helpers so the render path reads top-down.

void styleGhostButton(bool push) {
    using namespace ui::tokens;
    if (push) {
        ImGui::PushStyleColor(ImGuiCol_Button,        BgPanel2);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BgPanel3);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  BgPanel3);
        ImGui::PushStyleColor(ImGuiCol_Border,        BorderHi);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    } else {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
    }
}

void stylePrimaryCyanButton(bool push) {
    using namespace ui::tokens;
    if (push) {
        ImGui::PushStyleColor(ImGuiCol_Button,        WithAlpha(Cyan, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Cyan);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Cyan);
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0, 0, 1));
    } else {
        ImGui::PopStyleColor(4);
    }
}

// ── Single-glyph icon primitives (CP936-safe — no font glyphs) ──────────

// Music note: small filled head + thin stem + small flag.
void drawMusicNote(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddCircleFilled({c.x - 2.f, c.y + 4.f}, 3.f, color, 16);
    dl->AddRectFilled({c.x + 0.6f, c.y - 6.f}, {c.x + 2.4f, c.y + 4.f}, color);
    dl->AddTriangleFilled({c.x + 2.4f, c.y - 6.f},
                          {c.x + 6.5f, c.y - 4.f},
                          {c.x + 2.4f, c.y - 1.5f}, color);
}

// Disc: filled outer ring + thin centred ring (for the APK card icon).
void drawDisc(ImDrawList* dl, ImVec2 c, float r, ImU32 color) {
    dl->AddCircleFilled(c, r, color, 24);
    dl->AddCircle(c, r * 0.45f, IM_COL32(0, 0, 0, 200), 16, 1.5f);
    dl->AddCircleFilled(c, 1.5f, IM_COL32(0, 0, 0, 220), 8);
}

// Arrow pointing up (used by Add file).
void drawUpArrow(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddTriangleFilled({c.x - 4.f, c.y - 1.f},
                          {c.x + 4.f, c.y - 1.f},
                          {c.x,       c.y - 6.f}, color);
    dl->AddRectFilled({c.x - 1.f, c.y - 1.f}, {c.x + 1.f, c.y + 5.f}, color);
}

// Arrow pointing down (used by Build).
void drawDownArrow(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddTriangleFilled({c.x - 4.f, c.y + 1.f},
                          {c.x + 4.f, c.y + 1.f},
                          {c.x,       c.y + 6.f}, color);
    dl->AddRectFilled({c.x - 1.f, c.y - 5.f}, {c.x + 1.f, c.y + 1.f}, color);
}

// Magnifying glass — circle ring + diagonal handle.
void drawMagnifier(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddCircle({c.x - 1.f, c.y - 1.f}, 4.5f, color, 18, 1.5f);
    dl->AddLine({c.x + 2.2f, c.y + 2.2f}, {c.x + 5.5f, c.y + 5.5f}, color, 1.5f);
}

// Right-pointing chevron (used in Modified column / Open project arrow).
void drawChevronRight(ImDrawList* dl, ImVec2 c, ImU32 color) {
    dl->AddLine({c.x - 2.5f, c.y - 4.f}, {c.x + 2.5f, c.y},      color, 1.5f);
    dl->AddLine({c.x + 2.5f, c.y},       {c.x - 2.5f, c.y + 4.f}, color, 1.5f);
}

// Frameless icon + text clickable. Returns true on click.
bool textActionRow(const char* id,
                   const char* label,
                   void (*icon)(ImDrawList*, ImVec2, ImU32),
                   float width,
                   float height) {
    using namespace ui::tokens;
    const ImVec2 cur = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##t", {width, height});
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    (void)hovered;
    icon(dl, {cur.x + 12.f, cur.y + height * 0.5f}, IM_COL32(255, 255, 255, 255));
    // Use the scaled font (`GetFontSize()` is post-scale) so the label tracks
    // the panel's `SetWindowFontScale` instead of staying at base 17 px.
    const float fs = ImGui::GetFontSize();
    dl->AddText(ImGui::GetFont(), fs,
                {cur.x + 26.f, cur.y + (height - fs) * 0.5f},
                IM_COL32(255, 255, 255, 255), label);
    return clicked;
}

} // namespace

void ProjectHub::render(Engine* engine) {
    scanProjects();
    loadStarred();
    using namespace ui::tokens;

    // Auto-select most-recent project on first frame.
    if (!m_initialSelectDone) {
        if (m_selectedIdx < 0 && !m_projects.empty()) m_selectedIdx = 0;
        m_initialSelectDone = true;
    }

    // No-gray rule: every label/border/glyph is pure white #FFFFFF or a
    // fully saturated accent color. TextHi (#F4F4F7) is OFF-white and reads
    // as gray on black, so we use ImVec4{1,1,1,1} directly.
    const ImVec4 PureWhite     = ImVec4(1.f, 1.f, 1.f, 1.f);
    const ImU32  PureWhiteU32  = IM_COL32(255, 255, 255, 255);
    const ImVec4 SoftWhite     = PureWhite;        // alias for legacy refs
    (void)PureWhiteU32;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Project Hub", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    // ── Top bar: gradient M tile + crumb (no gear — Settings is per-project,
    //   not a Hub-level page).
    ui::TopBar({"Project Hub"}, nullptr);

    // ── Header row: title + subtitle (left) | search + actions (right) ─
    {
        const float hdrH = 80.f;            // breathing room for 2.3× title + subtitle
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
        ImGui::BeginChild("##hdr", ImVec2(0, hdrH), false,
                          ImGuiWindowFlags_NoScrollbar);
        const float fullW = ImGui::GetContentRegionAvail().x;

        // Title — pushed in a 40 px Roboto-Medium font that's pre-rasterized
        // by ImGuiLayer (we use the 48 px slot via getLogoFont). Scaling the
        // base font with SetWindowFontScale produces blurry text because the
        // glyph atlas lacks the higher-res glyph data.
        {
            ImFont* titleFont = engine
                ? engine->imguiLayer().getLogoFont(48.f)
                : nullptr;
            if (titleFont) ImGui::PushFont(titleFont);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::TextUnformatted("Projects");
            ImGui::PopStyleColor();
            if (titleFont) ImGui::PopFont();
        }

        // Subtitle — base size, dim text. Prototype renders this at the
        // standard body size (not the small 0.85× tier).
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.68f, 0.71f, 1.f));
        if (m_projects.empty())
            ImGui::TextUnformatted("0 projects");
        else
            ImGui::Text("%d projects  |  last opened %s",
                        (int)m_projects.size(),
                        m_projects.front().name.c_str());
        ImGui::PopStyleColor();

        // Right cluster: search + Add file + Create game.
        const float searchW = 320.f;        // wider so placeholder fits without overlap
        const float addW    = 130.f;
        const float createW = 180.f;
        const float gap     = 10.f;
        const float rightPad = 20.f;        // breathing room from window edge
        const float groupW  = searchW + addW + createW + gap * 2.f;
        ImGui::SetCursorPos({fullW - groupW - rightPad, 8.f});

        // Search input — magnifier glyph drawn over the left, Ctrl+K chip over
        // the right. Placeholder text starts after the magnifier (extra space
        // padding) so it never collides with either ornament.
        ImDrawList* hdrDl = ImGui::GetWindowDrawList();
        const ImVec2 searchCur = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemWidth(searchW);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        BgVoid);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, BgVoid);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  BgVoid);
        ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(1.f, 1.f, 1.f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Border,         Cyan);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   6.f);
        // Bake the icon into the placeholder padding so the user-typed buffer
        // also offsets cleanly. Two leading spaces ≈ 28 px at default font.
        ImGui::InputTextWithHint("##search", "    Search projects...",
                                 m_searchBuf, sizeof(m_searchBuf));
        const bool searchHovered = ImGui::IsItemHovered();
        const bool searchActive  = ImGui::IsItemActive();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);

        // Magnifier on the left edge of the input.
        drawMagnifier(hdrDl,
                      {searchCur.x + 12.f, searchCur.y + 14.f},
                      IM_COL32(255, 255, 255, 220));

        // Ctrl+K chip — drawn over the right side of the input only when the
        // input is hovered or focused. Hidden otherwise so the placeholder
        // text reads cleanly.
        if (searchHovered || searchActive) {
            const char* hint = "Ctrl+K";
            const ImVec2 hintSz = ImGui::CalcTextSize(hint);
            const float chipW = hintSz.x + 12.f;
            const float chipH = 18.f;
            const ImVec2 chipMin{searchCur.x + searchW - chipW - 6.f,
                                  searchCur.y + (28.f - chipH) * 0.5f};
            const ImVec2 chipMax{chipMin.x + chipW, chipMin.y + chipH};
            hdrDl->AddRectFilled(chipMin, chipMax, ToU32(Cyan), 4.f);
            hdrDl->AddText({chipMin.x + 6.f,
                            chipMin.y + (chipH - hintSz.y) * 0.5f},
                           IM_COL32(0, 0, 0, 255), hint);
        }

        ImGui::SameLine(0.f, gap);
        // ── Add file button — outlined, with up-arrow icon. Use a blank-label
        //   button + manual icon/text draws so the (icon + text) group is
        //   visually centred (leading-space hacks shove text off-centre).
        {
            const ImVec2 bCur = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Cyan);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Cyan);
            ImGui::PushStyleColor(ImGuiCol_Border,        Cyan);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   6.f);
            const bool clicked = ImGui::Button("##addbtn", ImVec2(addW, 28));
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
            if (clicked) { m_showAddFileDialog = true; m_addFileError.clear(); }

            const char* lbl = "Add file";
            const float lblW = ImGui::CalcTextSize(lbl).x;
            const float iconW = 12.f;
            const float gapW  = 6.f;
            const float groupW = iconW + gapW + lblW;
            const float gx = bCur.x + (addW - groupW) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            drawUpArrow(dl, {gx + iconW * 0.5f, bCur.y + 14.f},
                        IM_COL32(255, 255, 255, 255));
            dl->AddText({gx + iconW + gapW,
                         bCur.y + (28.f - ImGui::GetFontSize()) * 0.5f},
                        IM_COL32(255, 255, 255, 255), lbl);
        }

        ImGui::SameLine(0.f, gap);
        // ── Create game button — cyan filled. "+" drawn as a glyph so the
        //   (icon + text) group can be visually centred too.
        {
            const ImVec2 bCur = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Button,        Cyan);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Cyan);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Cyan);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
            const bool clicked = ImGui::Button("##createbtn", ImVec2(createW, 28));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            if (clicked) m_showCreateDialog = true;

            const char* lbl = "Create game";
            const float plusW = ImGui::CalcTextSize("+").x;
            const float lblW  = ImGui::CalcTextSize(lbl).x;
            const float gapW  = 6.f;
            const float groupW = plusW + gapW + lblW;
            const float gx = bCur.x + (createW - groupW) * 0.5f;
            const float gy = bCur.y + (28.f - ImGui::GetFontSize()) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText({gx,                       gy}, IM_COL32(0, 0, 0, 255), "+");
            dl->AddText({gx + plusW + gapW,        gy}, IM_COL32(0, 0, 0, 255), lbl);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();  // ChildBg
    }

    if (m_projects.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, SoftWhite);
        ImGui::TextUnformatted("No projects found. Create one to get started.");
        ImGui::PopStyleColor();
        ImGui::End();
        renderCreateDialog(engine);
        renderAddFileDialog();
        return;
    }

    // ── Body: rail | middle | detail ─────────────────────────────────
    const float bodyH   = std::max(220.f, ImGui::GetContentRegionAvail().y);
    const float railW   = 220.f;
    const float detailW = 280.f;
    const float midW    = std::max(280.f,
        ImGui::GetContentRegionAvail().x - railW - detailW
        - ImGui::GetStyle().ItemSpacing.x * 2.f);

    // ── LEFT RAIL ─────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
    ImGui::BeginChild("##rail", ImVec2(railW, bodyH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        struct Row {
            const char* label;
            HubLeftRail value;
            int         count;
            void (*icon)(ImDrawList*, ImVec2, ImU32);
        };
        const int recentCount = std::min((int)m_projects.size(), 3);
        int starredCount = 0;
        for (const auto& p : m_projects)
            if (m_starred.count(p.name)) ++starredCount;

        const Row rows[] = {
            {"All projects", HubLeftRail::All,     (int)m_projects.size(),
                [](ImDrawList* dl, ImVec2 c, ImU32 col){ drawFolderIcon(dl, c, col); }},
            {"Recent",       HubLeftRail::Recent,  recentCount,
                [](ImDrawList* dl, ImVec2 c, ImU32 col){ drawClockIcon(dl, c, col); }},
            {"Starred",      HubLeftRail::Starred, starredCount,
                [](ImDrawList* dl, ImVec2 c, ImU32 col){ drawStarIcon(dl, c, 7.f, col, false); }},
        };

        for (const auto& r : rows) {
            ImGui::PushID((int)r.value);
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x;
            const float rowH = 40.f;
            const bool active = (m_leftRail == r.value);

            if (ImGui::InvisibleButton("##row", {rowW, rowH}))
                m_leftRail = r.value;
            const bool hovered = ImGui::IsItemHovered();

            ImDrawList* dl = ImGui::GetWindowDrawList();

            if (active) {
                dl->AddRectFilled(cur, {cur.x + rowW, cur.y + rowH},
                                  ToU32(WithAlpha(Cyan, 0.08f)), 6.f);
                dl->AddRectFilled(cur, {cur.x + 2.f, cur.y + rowH}, ToU32(Cyan));
            }
            (void)hovered;

            // Icon (white, brighter when active).
            const ImU32 iconCol = active ? ToU32(Cyan) : IM_COL32(255, 255, 255, 255);
            r.icon(dl, {cur.x + 22.f, cur.y + rowH * 0.5f}, iconCol);

            // Label (white).
            ImGui::SetCursorScreenPos(
                {cur.x + 40.f, cur.y + (rowH - ImGui::GetFontSize()) * 0.5f});
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::TextUnformatted(r.label);
            ImGui::PopStyleColor();

            // Count badge (rounded rect + white text).
            char nb[16];
            snprintf(nb, sizeof(nb), "%d", r.count);
            const float numW = ImGui::CalcTextSize(nb).x;
            const float padX = 8.f;
            const float bW = numW + padX * 2.f;
            const float bH = 18.f;
            const ImVec2 bMin{cur.x + rowW - bW - 12.f, cur.y + (rowH - bH) * 0.5f};
            // No background — count text only, smaller (~0.85×).
            ImGui::SetWindowFontScale(0.85f);
            ImGui::SetCursorScreenPos(
                {bMin.x + padX, bMin.y + (bH - ImGui::GetFontSize()) * 0.5f});
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::TextUnformatted(nb);
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0f);

            ImGui::SetCursorScreenPos({cur.x, cur.y + rowH + 4.f});
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();  // rail ChildBg

    ImGui::SameLine();

    // ── MIDDLE: chip dots + sort + table ──────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
    ImGui::BeginChild("##mid", ImVec2(midW, bodyH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::Dummy(ImVec2(0, 6.f));
        const ImVec2 chipsRowOrigin = ImGui::GetCursorScreenPos();
        const float midFullW = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Mode-filter pills: All | Drop 2D | Drop 3D | Scan Line | Circle
        {
            struct FilterPill { const char* label; int idx; ImVec4 color; };
            const FilterPill pills[] = {
                {"All",       -1, Cyan},
                {"Drop 2D",    0, Cyan},
                {"Drop 3D",    1, Magenta},
                {"Scan Line",  2, Amber},
                {"Circle",     3, Lime},
            };
            for (const auto& fp : pills) {
                const bool active = (m_modeFilter == fp.idx);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        WithAlpha(fp.color, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fp.color);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  fp.color);
                    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0,0,0,1));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        WithAlpha(fp.color, 0.10f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(fp.color, 0.20f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  WithAlpha(fp.color, 0.30f));
                    ImGui::PushStyleColor(ImGuiCol_Text,          fp.color);
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(8.f, 2.f));
                if (ImGui::SmallButton(fp.label))
                    m_modeFilter = fp.idx;
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(4);
                ImGui::SameLine(0.f, 6.f);
            }
        }

        // ── Sort (right-anchored): "Sort:" + "Last modified ▾" frameless ──
        const char* sortLabel =
            (m_sortMode == HubSortMode::LastModified) ? "Last modified" :
            (m_sortMode == HubSortMode::NameAZ)       ? "Name A-Z" : "Song count";
        const float sortLabelW = ImGui::CalcTextSize("Sort:").x + 6.f;
        const float sortValW   = ImGui::CalcTextSize(sortLabel).x + 24.f;
        const float funnelW    = 26.f;
        const float sortGroupW = sortLabelW + sortValW + funnelW + 8.f;

        ImGui::SetCursorScreenPos(
            {chipsRowOrigin.x + midFullW - sortGroupW,
             chipsRowOrigin.y + 4.f});
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, SoftWhite);
        ImGui::TextUnformatted("Sort:");
        ImGui::PopStyleColor();
        ImGui::SameLine();

        // Frameless sort selector.
        {
            const ImVec2 sCur = ImGui::GetCursorScreenPos();
            ImGui::PushID("##sortbtn");
            const bool sClicked = ImGui::InvisibleButton("##s",
                {sortValW, ImGui::GetFontSize() + 6.f});
            const bool sHovered = ImGui::IsItemHovered();
            ImGui::PopID();
            (void)sHovered;
            dl->AddText({sCur.x + 4.f, sCur.y + 3.f},
                        IM_COL32(255, 255, 255, 255), sortLabel);
            // Chevron triangle to the right of the label.
            const float chvX = sCur.x + sortValW - 14.f;
            const float chvY = sCur.y + ImGui::GetFontSize() * 0.5f + 3.f;
            dl->AddTriangleFilled({chvX - 4.f, chvY - 2.f},
                                   {chvX + 4.f, chvY - 2.f},
                                   {chvX,       chvY + 3.f},
                                   IM_COL32(255, 255, 255, 255));
            if (sClicked) ImGui::OpenPopup("##sortpop");
        }
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, 1));
        if (ImGui::BeginPopup("##sortpop")) {
            if (ImGui::MenuItem("Last modified", nullptr,
                                m_sortMode == HubSortMode::LastModified))
                m_sortMode = HubSortMode::LastModified;
            if (ImGui::MenuItem("Name A-Z", nullptr,
                                m_sortMode == HubSortMode::NameAZ))
                m_sortMode = HubSortMode::NameAZ;
            if (ImGui::MenuItem("Song count", nullptr,
                                m_sortMode == HubSortMode::SongCount))
                m_sortMode = HubSortMode::SongCount;
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();  // PopupBg

        ImGui::SameLine();
        // Funnel button (frameless).
        {
            const ImVec2 fCur = ImGui::GetCursorScreenPos();
            ImGui::PushID("##funbtn");
            ImGui::InvisibleButton("##f", {funnelW, 22.f});
            const bool fHovered = ImGui::IsItemHovered();
            ImGui::PopID();
            (void)fHovered;
            drawFunnelIcon(dl, {fCur.x + funnelW * 0.5f, fCur.y + 11.f},
                           IM_COL32(255, 255, 255, 255));
        }

        ImGui::Dummy(ImVec2(0, 12.f));

        // ── Build sort order ──
        std::vector<int> order(m_projects.size());
        for (int i = 0; i < (int)m_projects.size(); ++i) order[i] = i;
        switch (m_sortMode) {
            case HubSortMode::LastModified:
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return m_projects[a].lastModifiedRaw > m_projects[b].lastModifiedRaw;
                });
                break;
            case HubSortMode::NameAZ:
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return m_projects[a].name < m_projects[b].name;
                });
                break;
            case HubSortMode::SongCount:
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return m_projects[a].songCount > m_projects[b].songCount;
                });
                break;
        }

        std::set<int> recentSet;
        if (m_leftRail == HubLeftRail::Recent) {
            std::vector<int> byMod = order;
            std::sort(byMod.begin(), byMod.end(), [&](int a, int b){
                return m_projects[a].lastModifiedRaw > m_projects[b].lastModifiedRaw;
            });
            for (int i = 0; i < (int)byMod.size() && i < 3; ++i)
                recentSet.insert(byMod[i]);
        }

        std::string query = m_searchBuf;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });

        // ── Projects table — no inner row dividers (they read as edges).
        const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_NoBordersInBody
                                     | ImGuiTableFlags_ScrollY;
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight,  ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg,     ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_TableRowBg,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt,     ImVec4(0,0,0,0));

        if (ImGui::BeginTable("##projects", 3, tflags,
                              ImVec2(0, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupColumn("NAME",     ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("SONGS",    ImGuiTableColumnFlags_WidthFixed, 160);
            ImGui::TableSetupColumn("MODIFIED", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupScrollFreeze(0, 1);

            // Manual header row so SONGS / MODIFIED can be CENTRED to match
            // their cell content. NAME stays left-aligned (under its tile+name).
            ImGui::SetWindowFontScale(0.85f);
            ImGui::TableNextRow(0, ImGui::GetFontSize() + 8.f);
            auto centredHeader = [](const char* s) {
                const ImVec2 hTL = ImGui::GetCursorScreenPos();
                const float hW = ImGui::GetContentRegionAvail().x;
                const float tW = ImGui::CalcTextSize(s).x;
                ImGui::SetCursorScreenPos({hTL.x + (hW - tW) * 0.5f, hTL.y});
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.68f, 0.71f, 1.f));
                ImGui::TextUnformatted(s);
                ImGui::PopStyleColor();
            };
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.68f, 0.71f, 1.f));
            ImGui::TextUnformatted("NAME");
            ImGui::PopStyleColor();
            ImGui::TableSetColumnIndex(1); centredHeader("SONGS");
            ImGui::TableSetColumnIndex(2);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.68f, 0.71f, 1.f));
            ImGui::TextUnformatted("MODIFIED");
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0f);

            int visibleCount = 0;
            for (int idx : order) {
                const auto& proj = m_projects[idx];
                if (m_leftRail == HubLeftRail::Recent  && !recentSet.count(idx)) continue;
                if (m_leftRail == HubLeftRail::Starred && !m_starred.count(proj.name)) continue;
                if (!query.empty()) {
                    std::string lower = proj.name;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                                   [](unsigned char c){ return (char)std::tolower(c); });
                    if (lower.find(query) == std::string::npos) continue;
                }
                if (m_modeFilter >= 0 && modeFilterIdxFor(proj) != m_modeFilter) continue;
                ++visibleCount;

                ImGui::PushID(idx);
                ImGui::TableNextRow(0, 64.f);

                const ModeView mv = modeViewFor(proj);
                const bool selected = (idx == m_selectedIdx);
                // Selected: indicated by side-bar + cyan name color, not bg.

                // ── NAME cell ──
                ImGui::TableSetColumnIndex(0);
                const ImVec2 cellTL = ImGui::GetCursorScreenPos();

                ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0,0,0,0));
                bool rowClicked = ImGui::Selectable("##row", selected,
                    ImGuiSelectableFlags_SpanAllColumns |
                    ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, 64.f));
                ImGui::PopStyleColor(3);
                bool dbl = rowClicked && ImGui::IsMouseDoubleClicked(0);

                ImDrawList* dl2 = ImGui::GetWindowDrawList();

                // Selected row: cyan side-bar + thin border across full row.
                if (selected) {
                    dl2->AddRectFilled(cellTL,
                        {cellTL.x + 3.f, cellTL.y + 64.f}, ToU32(Cyan));
                }

                // Mode-bordered icon tile (36×36) — black inside, full
                // saturation border + colored music-note. No alpha fills.
                const ImVec2 tileMin{cellTL.x + 12.f, cellTL.y + 14.f};
                const ImVec2 tileMax{tileMin.x + 36.f, tileMin.y + 36.f};
                dl2->AddRect(tileMin, tileMax, ToU32(mv.color), 8.f, 0, 1.5f);
                drawMusicNote(dl2,
                    {tileMin.x + 18.f, tileMin.y + 18.f}, ToU32(mv.color));

                // Project name — single line, vertically centred against the
                // tile. Path subtitle removed (prototype doesn't show it).
                {
                    const float nameY =
                        cellTL.y + (64.f - ImGui::GetFontSize()) * 0.5f;
                    ImGui::SetCursorScreenPos({cellTL.x + 60.f, nameY});
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                    ImGui::TextUnformatted(proj.name.c_str());
                    ImGui::PopStyleColor();
                }

                if (rowClicked) {
                    m_selectedIdx = idx;
                    if (dbl && engine) {
                        m_selectedProject = proj;
                        m_projectSelected = true;
                        engine->openProject(proj.path);
                        engine->startScreenEditor().load(proj.path);
                        engine->switchLayer(EditorLayer::StartScreen);
                        if (m_launchCallback) m_launchCallback(proj);
                    }
                }

                // ── SONGS cell — numeral horizontally centred in the column. ──
                ImGui::TableSetColumnIndex(1);
                {
                    const ImVec2 sTL = ImGui::GetCursorScreenPos();
                    const float colW = ImGui::GetContentRegionAvail().x;
                    char nbuf[16]; snprintf(nbuf, sizeof(nbuf), "%d", proj.songCount);
                    const float nW = ImGui::CalcTextSize(nbuf).x;
                    ImGui::SetCursorScreenPos(
                        {sTL.x + (colW - nW) * 0.5f,
                         sTL.y + (64.f - ImGui::GetFontSize()) * 0.5f});
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                    ImGui::TextUnformatted(nbuf);
                    ImGui::PopStyleColor();
                }

                // ── MODIFIED cell — vertically centred text + right chevron. ──
                ImGui::TableSetColumnIndex(2);
                {
                    const ImVec2 mTL = ImGui::GetCursorScreenPos();
                    const float colW = ImGui::GetContentRegionAvail().x;
                    ImGui::SetCursorScreenPos(
                        {mTL.x, mTL.y + (64.f - ImGui::GetFontSize()) * 0.5f});
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                    ImGui::TextUnformatted(
                        proj.lastModified.empty() ? "-" : proj.lastModified.c_str());
                    ImGui::PopStyleColor();
                    drawChevronRight(dl2, {mTL.x + colW - 14.f, mTL.y + 32.f},
                                     IM_COL32(255, 255, 255, 200));
                }

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (visibleCount == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, SoftWhite);
                ImGui::TextUnformatted("No projects match the current filter.");
                ImGui::PopStyleColor();
            }
        }
        ImGui::PopStyleColor(5);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();  // mid ChildBg

    ImGui::SameLine();

    // ── DETAIL PANEL ──────────────────────────────────────────────────
    // Subtle border (BorderHi α=0.10) — prototype card has only a hairline,
    // not a saturated cyan outline.
    ImGui::PushStyleColor(ImGuiCol_ChildBg,  ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Border,   BorderHi);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   8.f);
    ImGui::BeginChild("##detail", ImVec2(detailW, bodyH), true,
                      ImGuiWindowFlags_NoScrollbar);
    {
        const bool hasSel = m_selectedIdx >= 0 &&
                            m_selectedIdx < (int)m_projects.size();
        if (!hasSel) {
            ImGui::PushStyleColor(ImGuiCol_Text, SoftWhite);
            ImGui::TextUnformatted("Select a project.");
            ImGui::PopStyleColor();
        } else {
            const ProjectInfo& sel = m_projects[m_selectedIdx];
            const ModeView mv = modeViewFor(sel);
            ImDrawList* dl3 = ImGui::GetWindowDrawList();

            // ── Header tile (44 px square + larger name + body-sized subtitle) ──
            {
                ImGui::Dummy(ImVec2(0, 6.f));
                const ImVec2 tilePos = ImGui::GetCursorScreenPos();
                const float tileSz = 44.f;
                ImGui::InvisibleButton("##headerTile", {tileSz, tileSz});
                dl3->AddRect(tilePos, {tilePos.x + tileSz, tilePos.y + tileSz},
                             ToU32(mv.color), 8.f, 0, 1.5f);
                drawMusicNote(dl3,
                    {tilePos.x + tileSz * 0.5f, tilePos.y + tileSz * 0.5f},
                    ToU32(mv.color));

                // Detail project name — 1.25× over the body base.
                ImGui::SetCursorScreenPos({tilePos.x + tileSz + 12.f, tilePos.y});
                ImGui::SetWindowFontScale(1.25f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                ImGui::TextUnformatted(sel.name.c_str());
                ImGui::PopStyleColor();
                ImGui::SetWindowFontScale(1.0f);
                // Subtitle — body size, dim text.
                ImGui::SetCursorScreenPos(
                    {tilePos.x + tileSz + 12.f, tilePos.y + 26.f});
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.68f, 0.71f, 1.f));
                ImGui::Text("%s | %d songs", mv.label, sel.songCount);
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos({tilePos.x, tilePos.y + tileSz + 8.f});
            }

            ImGui::Dummy(ImVec2(0, 6.f));

            // ── Metadata ──
            if (ImGui::BeginTable("##meta", 2,
                    ImGuiTableFlags_SizingFixedFit |
                    ImGuiTableFlags_NoBordersInBody)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 170.f);
                ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

                auto row = [&](const char* k, const std::string& v) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    std::string up;
                    for (const char* p = k; *p; ++p)
                        up += (char)((*p >= 'a' && *p <= 'z') ? (*p - 32) : *p);
                    // Label — body size, dim-grey (text "VERSION" / "PATH" etc.).
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(0.66f, 0.68f, 0.71f, 1.f));
                    ImGui::TextUnformatted(up.c_str());
                    ImGui::PopStyleColor();

                    ImGui::TableSetColumnIndex(1);
                    // Default font (NOT mono) — the mono font in this build
                    // is intrinsically smaller, which made values look tiny
                    // next to the labels.
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                    const std::string display = v.empty() ? "-" : v;
                    const float availW = ImGui::GetContentRegionAvail().x;
                    if (ImGui::CalcTextSize(display.c_str()).x <= availW) {
                        ImGui::TextUnformatted(display.c_str());
                    } else {
                        std::string fit;
                        for (size_t i = 0; i < display.size(); ++i) {
                            std::string trial = display.substr(0, i + 1) + "...";
                            if (ImGui::CalcTextSize(trial.c_str()).x > availW)
                                break;
                            fit = display.substr(0, i + 1);
                        }
                        std::string out = fit + "...";
                        ImGui::TextUnformatted(out.c_str());
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s", display.c_str());
                    }
                    ImGui::PopStyleColor();
                };
                row("Version",       sel.version);
                row("Default chart", sel.defaultChart);
                row("Shader path",   sel.shaderPath);
                row("Last opened",   sel.lastModified);
                row("Path",          sel.path);
                ImGui::EndTable();
            }

            ImGui::Dummy(ImVec2(0, 14.f));

            // ── Open Project (cyan filled, play triangle + label) ──
            {
                const ImVec2 btnPos = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_Button,        Cyan);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Cyan);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Cyan);
                ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0.06f, 0.10f, 1.f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
                const float btnH = 44.f;
                const bool openClicked =
                    ImGui::Button("    Open Project", ImVec2(-1, btnH));
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
                // Play triangle drawn over the left of the button.
                const float cx = btnPos.x + 22.f;
                const float cy = btnPos.y + btnH * 0.5f;
                dl3->AddTriangleFilled({cx - 6.f, cy - 7.f},
                                        {cx - 6.f, cy + 7.f},
                                        {cx + 6.f, cy},
                                        IM_COL32(0, 0, 0, 255));
                if (openClicked) {
                    m_selectedProject = sel;
                    m_projectSelected = true;
                    if (engine) {
                        engine->openProject(sel.path);
                        engine->startScreenEditor().load(sel.path);
                        engine->switchLayer(EditorLayer::StartScreen);
                    }
                    if (m_launchCallback) m_launchCallback(sel);
                }
            }

            ImGui::Dummy(ImVec2(0, 8.f));

            // ── Reveal + Add file (frameless icon+text) ──
            const float halfW = (ImGui::GetContentRegionAvail().x - 8.f) * 0.5f;
            if (textActionRow("##rev", "Reveal", drawFolderIcon, halfW, 24.f))
                revealInExplorer(sel.path);
            ImGui::SameLine(0.f, 8.f);
            if (textActionRow("##addd", "Add file", drawUpArrow, halfW, 24.f)) {
                m_showAddFileDialog = true;
                m_addFileError.clear();
            }

            ImGui::Dummy(ImVec2(0, 10.f));

            // ── PACKAGE APK card ──
            const float cardH = m_apkProjectName.empty() ? 48.f
                              : (m_apkRunning ? 100.f : 134.f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Border,  Amber);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   8.f);
            ImGui::BeginChild("##apk_card", ImVec2(0, cardH), true,
                              ImGuiWindowFlags_NoScrollbar);
            {
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImDrawList* dlc = ImGui::GetWindowDrawList();
                // Amber music-disc icon (22 px).
                drawDisc(dlc, {origin.x + 12.f, origin.y + 12.f}, 11.f,
                         ToU32(Amber));
                // "PACKAGE APK" white bold text.
                ImGui::SetCursorScreenPos(
                    {origin.x + 30.f,
                     origin.y + (24.f - ImGui::GetFontSize()) * 0.5f});
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                ImGui::TextUnformatted("PACKAGE APK");
                ImGui::PopStyleColor();

                // Build button (outlined, with download icon). Right-anchored
                // with breathing room from the card's right border. Blank-label
                // button + manual icon/text so the group is visually centred.
                const float buildBtnW = 100.f;
                const float buildBtnH = 24.f;
                ImGui::SetCursorScreenPos(
                    {origin.x + ImGui::GetContentRegionAvail().x - buildBtnW - 4.f,
                     origin.y});
                if (m_apkRunning) ImGui::BeginDisabled();
                {
                    const ImVec2 bCur = ImGui::GetCursorScreenPos();
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Cyan);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Cyan);
                    ImGui::PushStyleColor(ImGuiCol_Border,        Cyan);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   6.f);
                    const bool clicked = ImGui::Button("##buildbtn",
                                                       ImVec2(buildBtnW, buildBtnH));
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor(4);
                    if (clicked) startApkBuild(sel);

                    const char* lbl = "Build";
                    const float lblW  = ImGui::CalcTextSize(lbl).x;
                    const float iconW = 12.f;
                    const float gapW  = 6.f;
                    const float groupW = iconW + gapW + lblW;
                    const float gx = bCur.x + (buildBtnW - groupW) * 0.5f;
                    drawDownArrow(dlc,
                        {gx + iconW * 0.5f, bCur.y + buildBtnH * 0.5f},
                        IM_COL32(255, 255, 255, 255));
                    dlc->AddText({gx + iconW + gapW,
                                  bCur.y + (buildBtnH - ImGui::GetFontSize()) * 0.5f},
                                 IM_COL32(255, 255, 255, 255), lbl);
                }
                if (m_apkRunning) ImGui::EndDisabled();

                ImGui::SetCursorScreenPos({origin.x, origin.y + 32.f});
                renderApkPanel();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    ImGui::End();

    renderCreateDialog(engine);
    renderAddFileDialog();
}

// ── Below: dead code from earlier attempts, preprocessor-disabled ──
#if 0
    if (!m_initialSelectDone) {
        if (m_selectedIdx < 0 && !m_projects.empty()) m_selectedIdx = 0;
        m_initialSelectDone = true;
    }

    // ── Outer hub window (full display) ────────────────────────────────
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Project Hub", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    // ── Top bar: gradient M tile + "Project Hub" crumb + gear button ──
    ui::TopBar({"Project Hub"}, [&]{
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(Cyan, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  WithAlpha(Cyan, 0.18f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  {6.f, 6.f});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        const ImVec2 cur = ImGui::GetCursorScreenPos();
        const bool gearClicked = ImGui::Button("##gear", {28.f, 28.f});
        drawGearClean(ImGui::GetWindowDrawList(),
                      {cur.x + 14.f, cur.y + 14.f}, ToU32(TextMid));
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        if (gearClicked && engine) engine->switchLayer(EditorLayer::Settings);
    });

    // ── Header row: title + subtitle (left) | search + actions (right) ─
    {
        const float hdrH = 64.f;
        ImGui::BeginChild("##hdr", ImVec2(0, hdrH), false,
                          ImGuiWindowFlags_NoScrollbar);
        const float fullW = ImGui::GetContentRegionAvail().x;

        // Left: stacked title + subtitle.
        ImGui::SetWindowFontScale(1.6f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
        ImGui::TextUnformatted("Projects");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);

        ui::PushMono();
        ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
        if (m_projects.empty())
            ImGui::TextUnformatted("0 projects");
        else
            ImGui::Text("%d projects | last opened %s",
                        (int)m_projects.size(), m_projects.front().name.c_str());
        ImGui::PopStyleColor();
        ui::PopMono();

        // Right: search + Add file + Create — anchored top-right.
        const float searchW = 240.f;
        const float addW    = 96.f;
        const float createW = 140.f;
        const float gap     = 8.f;
        const float groupW  = searchW + addW + createW + gap * 2.f;
        ImGui::SetCursorPos({fullW - groupW, 6.f});

        ImGui::SetNextItemWidth(searchW);
        ImGui::InputTextWithHint("##search", "Search projects...   Ctrl+K",
                                 m_searchBuf, sizeof(m_searchBuf));
        ImGui::SameLine(0.f, gap);
        styleGhostButton(true);
        if (ImGui::Button("Add file", ImVec2(addW, 28))) {
            m_showAddFileDialog = true;
            m_addFileError.clear();
        }
        styleGhostButton(false);
        ImGui::SameLine(0.f, gap);
        stylePrimaryCyanButton(true);
        if (ImGui::Button("+ Create game", ImVec2(createW, 28)))
            m_showCreateDialog = true;
        stylePrimaryCyanButton(false);

        ImGui::EndChild();
    }

    if (m_projects.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, TextLow);
        ImGui::TextUnformatted("No projects found. Create one to get started.");
        ImGui::PopStyleColor();
        ImGui::End();
        renderCreateDialog(engine);
        renderAddFileDialog();
        return;
    }

    // ── Body: three columns (rail | middle | detail) ──────────────────
    const float bodyH   = std::max(220.f, ImGui::GetContentRegionAvail().y);
    const float railW   = 220.f;
    const float detailW = 280.f;
    const float midW    = std::max(280.f,
        ImGui::GetContentRegionAvail().x - railW - detailW
        - ImGui::GetStyle().ItemSpacing.x * 2.f);

    // ── LEFT RAIL ─────────────────────────────────────────────────────
    ImGui::BeginChild("##rail", ImVec2(railW, bodyH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        struct Row {
            const char* label;
            HubLeftRail value;
            int         count;
            void (*icon)(ImDrawList*, ImVec2, ImU32);
        };
        const int recentCount = std::min((int)m_projects.size(), 3);
        int starredCount = 0;
        for (const auto& p : m_projects)
            if (m_starred.count(p.name)) ++starredCount;

        const Row rows[] = {
            {"All projects", HubLeftRail::All,     (int)m_projects.size(),
                [](ImDrawList* dl, ImVec2 c, ImU32 col){ drawFolderIcon(dl, c, col); }},
            {"Recent",       HubLeftRail::Recent,  recentCount,
                [](ImDrawList* dl, ImVec2 c, ImU32 col){ drawClockIcon(dl, c, col); }},
            {"Starred",      HubLeftRail::Starred, starredCount,
                [](ImDrawList* dl, ImVec2 c, ImU32 col){ drawStarIcon(dl, c, 7.f, col, false); }},
        };

        for (const auto& r : rows) {
            ImGui::PushID((int)r.value);
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            const float rowW = ImGui::GetContentRegionAvail().x;
            const float rowH = 40.f;
            const bool active = (m_leftRail == r.value);

            if (ImGui::InvisibleButton("##row", {rowW, rowH}))
                m_leftRail = r.value;
            const bool hovered = ImGui::IsItemHovered();

            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Subtle row background on hover/active.
            if (active) {
                dl->AddRectFilled(cur, {cur.x + rowW, cur.y + rowH},
                                  ToU32(WithAlpha(Cyan, 0.10f)), 6.f);
                dl->AddRectFilled(cur, {cur.x + 3.f, cur.y + rowH}, ToU32(Cyan));
            } else if (hovered) {
                dl->AddRectFilled(cur, {cur.x + rowW, cur.y + rowH},
                                  ToU32(WithAlpha(Cyan, 0.04f)), 6.f);
            }

            // Icon.
            const ImU32 iconCol =
                ToU32(active ? Cyan : (hovered ? TextMid : TextLow));
            r.icon(dl, {cur.x + 22.f, cur.y + rowH * 0.5f}, iconCol);

            // Label (ImGui text — proper layout).
            ImGui::SetCursorScreenPos(
                {cur.x + 40.f, cur.y + (rowH - ImGui::GetFontSize()) * 0.5f});
            ImGui::PushStyleColor(ImGuiCol_Text,
                active ? TextHi : (hovered ? TextMid : TextLow));
            ImGui::TextUnformatted(r.label);
            ImGui::PopStyleColor();

            // Count badge — rounded rect + ImGui text.
            char nb[16];
            snprintf(nb, sizeof(nb), "%d", r.count);
            const float numW = ImGui::CalcTextSize(nb).x;
            const float padX = 8.f;
            const float bW = numW + padX * 2.f;
            const float bH = 18.f;
            const ImVec2 bMin{cur.x + rowW - bW - 12.f, cur.y + (rowH - bH) * 0.5f};
            // No background — count text only.
            ImGui::SetCursorScreenPos(
                {bMin.x + padX, bMin.y + (bH - ImGui::GetFontSize()) * 0.5f});
            ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
            ImGui::TextUnformatted(nb);
            ImGui::PopStyleColor();

            // Advance cursor past this row.
            ImGui::SetCursorScreenPos({cur.x, cur.y + rowH + 4.f});

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ── MIDDLE: chips row + table ──────────────────────────────────────
    ImGui::BeginChild("##mid", ImVec2(midW, bodyH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        // ── Chips row: uniform grayscale (only active is colored) + Sort ──
        struct ChipDef { const char* label; int idx; };
        const ChipDef chips[] = {
            {"All",       -1}, {"Drop 2D",   0}, {"Drop 3D",   1},
            {"Scan Line",  2}, {"Circle",    3},
        };
        const ImVec2 chipsRowOrigin = ImGui::GetCursorScreenPos();
        const float midFullW = ImGui::GetContentRegionAvail().x;

        for (const auto& c : chips) {
            const bool active = (m_modeFilter == c.idx);
            const ImVec4 bg     = active ? Cyan : BgPanel3;
            const ImVec4 fg     = active ? ImVec4{0,0,0,1} : TextHi;
            const ImVec4 border = active ? Cyan : BorderHi;
            ImGui::PushStyleColor(ImGuiCol_Button,        bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? bg : BgPanel2);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bg);
            ImGui::PushStyleColor(ImGuiCol_Text,          fg);
            ImGui::PushStyleColor(ImGuiCol_Border,        border);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {10.f, 4.f});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   999.f);
            if (ImGui::Button(c.label)) m_modeFilter = c.idx;
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(5);
            ImGui::SameLine();
        }

        // Right-anchor: Sort label + dropdown.
        const float sortLabelW = ImGui::CalcTextSize("Sort:").x + 6.f;
        const float ddW = 160.f;
        const float sortGroupW = sortLabelW + ddW + 4.f;
        ImGui::SetCursorScreenPos(
            {chipsRowOrigin.x + midFullW - sortGroupW,
             chipsRowOrigin.y + 2.f});
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, TextLow);
        ImGui::TextUnformatted("Sort:");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        const char* sortKey =
            (m_sortMode == HubSortMode::LastModified) ? "mod" :
            (m_sortMode == HubSortMode::NameAZ)       ? "name" : "songs";
        const char* const sortItems[][2] = {
            {"mod",   "Last modified"},
            {"name",  "Name A-Z"},
            {"songs", "Song count"},
        };
        ImGui::PushItemWidth(ddW);
        if (ui::Dropdown("##sort", sortItems, 3, &sortKey)) {
            if      (std::strcmp(sortKey, "mod")   == 0) m_sortMode = HubSortMode::LastModified;
            else if (std::strcmp(sortKey, "name")  == 0) m_sortMode = HubSortMode::NameAZ;
            else if (std::strcmp(sortKey, "songs") == 0) m_sortMode = HubSortMode::SongCount;
        }
        ImGui::PopItemWidth();

        ImGui::Dummy(ImVec2(0, 8.f));

        // ── Build sorted index view ────────────────────────────────────
        std::vector<int> order(m_projects.size());
        for (int i = 0; i < (int)m_projects.size(); ++i) order[i] = i;
        switch (m_sortMode) {
            case HubSortMode::LastModified:
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return m_projects[a].lastModifiedRaw > m_projects[b].lastModifiedRaw;
                });
                break;
            case HubSortMode::NameAZ:
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return m_projects[a].name < m_projects[b].name;
                });
                break;
            case HubSortMode::SongCount:
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return m_projects[a].songCount > m_projects[b].songCount;
                });
                break;
        }

        std::set<int> recentSet;
        if (m_leftRail == HubLeftRail::Recent) {
            std::vector<int> byMod = order;
            std::sort(byMod.begin(), byMod.end(), [&](int a, int b){
                return m_projects[a].lastModifiedRaw > m_projects[b].lastModifiedRaw;
            });
            for (int i = 0; i < (int)byMod.size() && i < 3; ++i)
                recentSet.insert(byMod[i]);
        }

        std::string query = m_searchBuf;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });

        int maxSongs = 1;
        for (const auto& p : m_projects) maxSongs = std::max(maxSongs, p.songCount);

        // ── Projects table ─────────────────────────────────────────────
        const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_BordersInnerH
                                     | ImGuiTableFlags_NoBordersInBody
                                     | ImGuiTableFlags_ScrollY;
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight,  Border);
        ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, BorderHi);
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg,     BgPanel);
        ImGui::PushStyleColor(ImGuiCol_TableRowBg,        BgBase);
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt,     BgBase);

        if (ImGui::BeginTable("##projects", 4, tflags,
                              ImVec2(0, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupColumn("NAME",     ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("MODE",     ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("SONGS",    ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableSetupColumn("MODIFIED", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupScrollFreeze(0, 1);

            ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            int visibleCount = 0;
            for (int idx : order) {
                const auto& proj = m_projects[idx];
                if (m_leftRail == HubLeftRail::Recent  && !recentSet.count(idx)) continue;
                if (m_leftRail == HubLeftRail::Starred && !m_starred.count(proj.name)) continue;
                if (!query.empty()) {
                    std::string lower = proj.name;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                                   [](unsigned char c){ return (char)std::tolower(c); });
                    if (lower.find(query) == std::string::npos) continue;
                }
                if (m_modeFilter >= 0 && modeFilterIdxFor(proj) != m_modeFilter) continue;
                ++visibleCount;

                ImGui::PushID(idx);
                ImGui::TableNextRow(0, 56.f);

                const ModeView mv = modeViewFor(proj);
                const bool selected = (idx == m_selectedIdx);
                if (selected) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        ToU32(WithAlpha(Cyan, 0.14f)));
                }

                // ── NAME cell ──────────────────────────────────────────
                ImGui::TableSetColumnIndex(0);
                const ImVec2 cellTL = ImGui::GetCursorScreenPos();

                // Whole-row Selectable goes FIRST so it's at the back.
                ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0,0,0,0));
                bool rowClicked = ImGui::Selectable("##row", selected,
                    ImGuiSelectableFlags_SpanAllColumns |
                    ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, 56.f));
                ImGui::PopStyleColor(3);
                bool dbl = rowClicked && ImGui::IsMouseDoubleClicked(0);

                ImDrawList* dl = ImGui::GetWindowDrawList();

                // Cyan side-bar on selected.
                if (selected) {
                    dl->AddRectFilled(cellTL, {cellTL.x + 3.f, cellTL.y + 56.f},
                                      ToU32(Cyan));
                }

                // 32×32 dark-gray tile + colored letter.
                const ImVec2 tileMin{cellTL.x + 10.f, cellTL.y + 12.f};
                const ImVec2 tileMax{tileMin.x + 32.f, tileMin.y + 32.f};
                dl->AddRectFilled(tileMin, tileMax,
                                  ToU32(BgPanel3), 6.f);
                dl->AddRect(tileMin, tileMax,
                            ToU32(BorderHi), 6.f);
                {
                    char letter[2] = {(char)std::toupper((unsigned char)proj.name[0]), 0};
                    const float ts = ImGui::GetFontSize() * 1.1f;
                    const float tw = ImGui::CalcTextSize(letter).x;
                    dl->AddText(nullptr, ts,
                        {tileMin.x + (32.f - tw) * 0.5f,
                         tileMin.y + (32.f - ts) * 0.5f - 1.f},
                        ToU32(mv.color), letter);
                }

                // Name (TextHi) and path (mono TextLow) — placed by absolute
                // ScreenPos so they don't fight the Selectable's hit test.
                ImGui::SetCursorScreenPos({cellTL.x + 52.f, cellTL.y + 8.f});
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                ImGui::TextUnformatted(proj.name.c_str());
                ImGui::PopStyleColor();

                ImGui::SetCursorScreenPos({cellTL.x + 52.f, cellTL.y + 30.f});
                ui::PushMono();
                ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
                ImGui::Text("Projects/%s/", proj.name.c_str());
                ImGui::PopStyleColor();
                ui::PopMono();

                // Star toggle — placed AFTER the Selectable and the visible
                // text, so its InvisibleButton wins the click on hover.
                const float nameAvailW = ImGui::GetContentRegionAvail().x;
                const ImVec2 starTL{cellTL.x + nameAvailW - 28.f, cellTL.y + 18.f};
                ImGui::SetCursorScreenPos(starTL);
                bool starClicked = ImGui::InvisibleButton("##star", {22.f, 22.f});
                const bool starHovered = ImGui::IsItemHovered();
                const bool starred = m_starred.count(proj.name) > 0;
                drawStarIcon(dl,
                    {starTL.x + 11.f, starTL.y + 11.f}, 8.f,
                    ToU32(starred ? Amber
                           : (starHovered ? WithAlpha(Amber, 0.75f)
                                          : WithAlpha(TextLow, 0.55f))),
                    starred);
                if (starClicked) {
                    if (starred) m_starred.erase(proj.name);
                    else         m_starred.insert(proj.name);
                    saveStarred();
                }

                // Apply row-click effect (skip if star handled the click).
                if (rowClicked && !starClicked) {
                    m_selectedIdx = idx;
                    if (dbl && engine) {
                        m_selectedProject = proj;
                        m_projectSelected = true;
                        engine->openProject(proj.path);
                        engine->startScreenEditor().load(proj.path);
                        engine->switchLayer(EditorLayer::StartScreen);
                        if (m_launchCallback) m_launchCallback(proj);
                    }
                }

                // ── MODE cell ──────────────────────────────────────────
                ImGui::TableSetColumnIndex(1);
                ImGui::Dummy(ImVec2(0, 16.f));
                ui::Pill(mv.label, mv.color, /*solid*/ false);

                // ── SONGS cell ─────────────────────────────────────────
                ImGui::TableSetColumnIndex(2);
                {
                    const ImVec2 sTL = ImGui::GetCursorScreenPos();
                    const float barW = 70.f, barH = 4.f;
                    const ImVec2 bMin{sTL.x, sTL.y + 24.f};
                    const ImVec2 bMax{bMin.x + barW, bMin.y + barH};
                    dl->AddRectFilled(bMin, bMax, ToU32(BgPanel3), 2.f);
                    const float fillW = barW *
                        std::min(1.f, (float)proj.songCount / (float)maxSongs);
                    if (fillW > 0.5f) {
                        dl->AddRectFilled(bMin, {bMin.x + fillW, bMax.y},
                                          ToU32(Cyan), 2.f);
                    }
                    ImGui::SetCursorScreenPos({bMax.x + 8.f, sTL.y + 18.f});
                    ui::PushMono();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                    ImGui::Text("%d", proj.songCount);
                    ImGui::PopStyleColor();
                    ui::PopMono();
                }

                // ── MODIFIED cell ──────────────────────────────────────
                ImGui::TableSetColumnIndex(3);
                {
                    const ImVec2 mTL = ImGui::GetCursorScreenPos();
                    const float colW = ImGui::GetContentRegionAvail().x;
                    ImGui::SetCursorScreenPos({mTL.x, mTL.y + 18.f});
                    ui::PushMono();
                    ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
                    ImGui::TextUnformatted(
                        proj.lastModified.empty() ? "-" : proj.lastModified.c_str());
                    ImGui::PopStyleColor();
                    ui::PopMono();
                    // Right chevron.
                    const ImVec2 chC{mTL.x + colW - 14.f, mTL.y + 26.f};
                    const float chs = 5.f;
                    dl->AddTriangleFilled(
                        {chC.x - chs * 0.5f, chC.y - chs},
                        {chC.x - chs * 0.5f, chC.y + chs},
                        {chC.x + chs * 0.6f, chC.y},
                        ToU32(TextLow));
                }

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (visibleCount == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, TextLow);
                ImGui::TextUnformatted("No projects match the current filter.");
                ImGui::PopStyleColor();
            }
        }
        ImGui::PopStyleColor(5);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ── DETAIL PANEL ────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_ChildBg, BgBase);
    ImGui::BeginChild("##detail", ImVec2(detailW, bodyH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        const bool hasSel = m_selectedIdx >= 0 &&
                            m_selectedIdx < (int)m_projects.size();
        if (!hasSel) {
            ImGui::PushStyleColor(ImGuiCol_Text, TextLow);
            ImGui::TextUnformatted("Select a project to see details.");
            ImGui::PopStyleColor();
        } else {
            const ProjectInfo& sel = m_projects[m_selectedIdx];
            const ModeView mv = modeViewFor(sel);

            // ── Header tile: 40×40 letter square + name + subtitle ──
            {
                ImGui::Dummy(ImVec2(0, 4.f));
                const ImVec2 tilePos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##headerTile", {40.f, 40.f});
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(tilePos, {tilePos.x + 40.f, tilePos.y + 40.f},
                                  ToU32(BgPanel3), 6.f);
                dl->AddRect(tilePos, {tilePos.x + 40.f, tilePos.y + 40.f},
                            ToU32(BorderHi), 6.f);
                char letter[2] = {(char)std::toupper((unsigned char)sel.name[0]), 0};
                const float ts = ImGui::GetFontSize() * 1.25f;
                const float tw = ImGui::CalcTextSize(letter).x;
                dl->AddText(nullptr, ts,
                    {tilePos.x + (40.f - tw) * 0.5f,
                     tilePos.y + (40.f - ts) * 0.5f - 1.f},
                    ToU32(mv.color), letter);

                // Right gutter: name (TextHi) + mono subtitle (TextLow).
                ImGui::SetCursorScreenPos({tilePos.x + 52.f, tilePos.y + 2.f});
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                ImGui::TextUnformatted(sel.name.c_str());
                ImGui::PopStyleColor();
                ImGui::SetCursorScreenPos({tilePos.x + 52.f, tilePos.y + 22.f});
                ui::PushMono();
                ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
                ImGui::Text("%s  -  %d songs", mv.label, sel.songCount);
                ImGui::PopStyleColor();
                ui::PopMono();
                ImGui::SetCursorScreenPos({tilePos.x, tilePos.y + 56.f});
            }

            // ── Metadata key/value table ──
            if (ImGui::BeginTable("##meta", 2,
                    ImGuiTableFlags_SizingFixedFit |
                    ImGuiTableFlags_NoBordersInBody)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 100.f);
                ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

                auto row = [](const char* k, const std::string& v) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    std::string up;
                    for (const char* p = k; *p; ++p)
                        up += (char)((*p >= 'a' && *p <= 'z') ? (*p - 32) : *p);
                    ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
                    ImGui::TextUnformatted(up.c_str());
                    ImGui::PopStyleColor();

                    ImGui::TableSetColumnIndex(1);
                    ui::PushMono();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                    ImGui::PushTextWrapPos(0.f);
                    ImGui::TextUnformatted(v.empty() ? "-" : v.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                    ui::PopMono();
                };

                row("Version",       sel.version);
                row("Default chart", sel.defaultChart);
                row("Shader path",   sel.shaderPath);
                row("Last opened",   sel.lastModified);
                row("Path",          sel.path);
                ImGui::EndTable();
            }

            ImGui::Dummy(ImVec2(0, 12.f));

            // ── Open Project (primary cyan with play-triangle prefix) ──
            stylePrimaryCyanButton(true);
            const ImVec2 btnPos = ImGui::GetCursorScreenPos();
            const float  btnW   = ImGui::GetContentRegionAvail().x;
            const bool   openClicked = ImGui::Button("    Open Project", ImVec2(-1, 36));
            // Play triangle drawn after the button so it sits on top.
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float cy = btnPos.y + 18.f;
                const float cx = btnPos.x + 18.f;
                dl->AddTriangleFilled({cx - 5.f, cy - 6.f},
                                       {cx - 5.f, cy + 6.f},
                                       {cx + 6.f, cy},
                                       IM_COL32(0, 0, 0, 255));
                (void)btnW;
            }
            if (openClicked) {
                m_selectedProject = sel;
                m_projectSelected = true;
                if (engine) {
                    engine->openProject(sel.path);
                    engine->startScreenEditor().load(sel.path);
                    engine->switchLayer(EditorLayer::StartScreen);
                }
                if (m_launchCallback) m_launchCallback(sel);
            }
            stylePrimaryCyanButton(false);

            ImGui::Dummy(ImVec2(0, 6.f));

            // ── Reveal + Add file (ghost) ──
            const float halfW = (ImGui::GetContentRegionAvail().x - 6.f) * 0.5f;
            styleGhostButton(true);
            if (ImGui::Button("Reveal", ImVec2(halfW, 26)))
                revealInExplorer(sel.path);
            ImGui::SameLine();
            if (ImGui::Button("Add file##detail", ImVec2(halfW, 26))) {
                m_showAddFileDialog = true;
                m_addFileError.clear();
            }
            styleGhostButton(false);

            ImGui::Dummy(ImVec2(0, 8.f));

            // ── Package APK card ──
            const float cardH = m_apkProjectName.empty() ? 60.f
                              : (m_apkRunning ? 96.f : 132.f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, BgPanel);
            ImGui::PushStyleColor(ImGuiCol_Border,  BorderHi);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   6.f);
            ImGui::BeginChild("##apk_card", ImVec2(0, cardH), true,
                              ImGuiWindowFlags_NoScrollbar);
            {
                // Header: amber filled pill containing white "PACKAGE APK" + Build.
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const char* hdrLabel = "PACKAGE APK";
                const float hdrW = ImGui::CalcTextSize(hdrLabel).x + 18.f;
                const float hdrH = 22.f;
                dl->AddRectFilled(origin, {origin.x + hdrW, origin.y + hdrH},
                                  ToU32(Amber), 4.f);
                ImGui::SetCursorScreenPos(
                    {origin.x + 9.f,
                     origin.y + (hdrH - ImGui::GetFontSize()) * 0.5f});
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
                ImGui::TextUnformatted(hdrLabel);
                ImGui::PopStyleColor();

                // Right-anchored Build button on same row.
                ImGui::SetCursorScreenPos(
                    {origin.x + ImGui::GetContentRegionAvail().x - 60.f,
                     origin.y});
                if (m_apkRunning) ImGui::BeginDisabled();
                styleGhostButton(true);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
                if (ImGui::Button("Build", ImVec2(60.f, hdrH)))
                    startApkBuild(sel);
                ImGui::PopStyleColor();
                styleGhostButton(false);
                if (m_apkRunning) ImGui::EndDisabled();

                ImGui::SetCursorScreenPos({origin.x, origin.y + hdrH + 8.f});
                renderApkPanel();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();

    renderCreateDialog(engine);
    renderAddFileDialog();
}

#if 0  // ── legacy render path (kept temporarily for reference; remove later) ──
static void __unused_old_render() {

    // ── Header row: title + subtitle on the left, search + actions right ─
    {
        const float headerH = 44.f;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        const float fullW = ImGui::GetContentRegionAvail().x;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float titleSize = ImGui::GetFontSize() * 1.6f;
        dl->AddText(nullptr, titleSize, origin,
                    IM_COL32(255, 255, 255, 255), "Projects");

        std::string subtitle = std::to_string(m_projects.size()) + " projects";
        if (!m_projects.empty())
            subtitle += " | last opened " + m_projects.front().name;
        ui::PushMono();
        dl->AddText({origin.x, origin.y + titleSize + 4.f},
                    ToU32(TextLow), subtitle.c_str());
        ui::PopMono();

        // Right-aligned: search + Add file + Create game.
        ImGui::SetCursorScreenPos({origin.x + fullW - 500.f, origin.y + 6.f});
        ImGui::SetNextItemWidth(240);
        ImGui::InputTextWithHint("##search", "Search projects...   Ctrl+K",
                                 m_searchBuf, sizeof(m_searchBuf));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        BgPanel2);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BgPanel3);
        ImGui::PushStyleColor(ImGuiCol_Border,        BorderHi);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        if (ImGui::Button("Add file", ImVec2(96, 28))) {
            m_showAddFileDialog = true;
            m_addFileError.clear();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        WithAlpha(Cyan, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Cyan);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Cyan);
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0, 0, 1));
        if (ImGui::Button("+ Create game", ImVec2(140, 28)))
            m_showCreateDialog = true;
        ImGui::PopStyleColor(4);

        ImGui::SetCursorScreenPos({origin.x, origin.y + headerH + 14.f});
    }

    // ── Three-column layout: left rail | table | detail panel ──────────────
    if (m_projects.empty()) {
        ImGui::TextDisabled("No projects found. Create one to get started.");
        ImGui::End();
        renderCreateDialog(engine);
        renderAddFileDialog();
        return;
    }

    const float railW   = 220.f;
    const float detailW = 280.f;
    const float midW    = std::max(280.f,
        ImGui::GetContentRegionAvail().x - railW - detailW
        - ImGui::GetStyle().ItemSpacing.x * 2.f);
    const float colsH   = std::max(220.f, ImGui::GetContentRegionAvail().y);

    // Auto-select most-recent project on first frame so the detail panel
    // renders immediately, matching the React mock.
    if (!m_initialSelectDone) {
        if (m_selectedIdx < 0 && !m_projects.empty()) m_selectedIdx = 0;
        m_initialSelectDone = true;
    }

    // ── Left rail ──────────────────────────────────────────────────────────
    ImGui::BeginChild("##hub_rail", ImVec2(railW, colsH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        struct RailRow {
            const char* label;
            HubLeftRail value;
            int         count;
            void (*icon)(ImDrawList*, ImVec2, ImU32);
        };
        const int recentCount  = std::min((int)m_projects.size(), 3);
        int starredCount = 0;
        for (const auto& p : m_projects)
            if (m_starred.count(p.name)) ++starredCount;

        const RailRow rows[] = {
            {"All projects", HubLeftRail::All,     (int)m_projects.size(), drawFolderIcon},
            {"Recent",       HubLeftRail::Recent,  recentCount,            drawClockIcon},
            {"Starred",      HubLeftRail::Starred, starredCount,           [](ImDrawList* dl, ImVec2 c, ImU32 col){ drawStarIcon(dl, c, 7.f, col, false); }},
        };

        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const auto& r : rows) {
            ImGui::PushID((int)r.value);
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            const float  rowW = ImGui::GetContentRegionAvail().x;
            const float  rowH = 36.f;
            const bool   active = (m_leftRail == r.value);

            if (ImGui::InvisibleButton("##rail_row", {rowW, rowH}))
                m_leftRail = r.value;
            const bool hovered = ImGui::IsItemHovered();

            // Background + active indicator.
            const ImU32 bg = active ? ToU32(WithAlpha(Cyan, 0.10f))
                              : (hovered ? ToU32(WithAlpha(Cyan, 0.05f)) : 0);
            if (bg) dl->AddRectFilled(cur, {cur.x + rowW, cur.y + rowH}, bg, 6.f);
            if (active) dl->AddRectFilled(cur, {cur.x + 3.f, cur.y + rowH}, ToU32(Cyan));

            // Icon.
            const ImU32 iconCol = ToU32(active ? Cyan : (hovered ? TextMid : TextLow));
            r.icon(dl, {cur.x + 22.f, cur.y + rowH * 0.5f}, iconCol);

            // Label.
            const ImU32 textCol = ToU32(active ? TextHi : (hovered ? TextMid : TextLow));
            dl->AddText({cur.x + 40.f, cur.y + (rowH - ImGui::GetFontSize()) * 0.5f},
                        textCol, r.label);

            // Count badge (right-aligned).
            char numBuf[16];
            snprintf(numBuf, sizeof(numBuf), "%d", r.count);
            const float numW = ImGui::CalcTextSize(numBuf).x;
            const float padX = 8.f;
            const float badgeW = numW + padX * 2.f;
            const float badgeH = 18.f;
            const ImVec2 bMin{cur.x + rowW - badgeW - 12.f, cur.y + (rowH - badgeH) * 0.5f};
            const ImVec2 bMax{bMin.x + badgeW, bMin.y + badgeH};
            dl->AddRectFilled(bMin, bMax, ToU32(BgPanel3), 9.f);
            dl->AddText({bMin.x + padX, bMin.y + (badgeH - ImGui::GetFontSize()) * 0.5f},
                        ToU32(TextMid), numBuf);

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ── Middle: chips row + sort row + table ──────────────────────────────
    ImGui::BeginChild("##hub_mid", ImVec2(midW, colsH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        // Compact mode-filter chips (faint dots, mock-style).
        struct FilterDef { const char* label; ImVec4 color; int idx; };
        const FilterDef filters[] = {
            {"All",       Cyan,    -1},
            {"Drop 2D",   Cyan,     0},
            {"Drop 3D",   Magenta,  1},
            {"Scan Line", Amber,    2},
            {"Circle",    Lime,     3},
        };
        for (const auto& f : filters) {
            const bool active = (m_modeFilter == f.idx);
            // Inactive: faint gray dot (no accent). Active: filled with the
            // mode color and dark text. Matches the prototype where the
            // chips read as small affordances, not headline elements.
            const ImVec4 bg     = active ? f.color : WithAlpha(TextLow, 0.10f);
            const ImVec4 fg     = active ? ImVec4{0,0,0,1} : TextMid;
            const ImVec4 border = active ? f.color : WithAlpha(TextLow, 0.20f);
            ImGui::PushStyleColor(ImGuiCol_Button,        bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? bg : WithAlpha(TextLow, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bg);
            ImGui::PushStyleColor(ImGuiCol_Text,          fg);
            ImGui::PushStyleColor(ImGuiCol_Border,        border);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {6.f, 2.f});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   999.f);
            if (ImGui::Button(f.label))
                m_modeFilter = f.idx;
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(5);
            ImGui::SameLine();
        }

        // Sort dropdown + funnel (right-aligned on the same row).
        const float fullW = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine();
        const char* sortKey =
            (m_sortMode == HubSortMode::LastModified) ? "mod" :
            (m_sortMode == HubSortMode::NameAZ)       ? "name" : "songs";
        const char* const sortItems[][2] = {
            {"mod",   "Last modified"},
            {"name",  "Name A-Z"},
            {"songs", "Song count"},
        };
        const float ddW = 160.f;
        const float lblW = ImGui::CalcTextSize("Sort:").x + 6.f;
        const float funnelW = 30.f;
        const float rightStart = ImGui::GetCursorPosX() + std::max(0.f,
            fullW - lblW - ddW - funnelW - 12.f);
        ImGui::SetCursorPosX(rightStart);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(TextLow, "Sort:");
        ImGui::SameLine();
        ImGui::PushItemWidth(ddW);
        const char* prev = sortKey;
        if (ui::Dropdown("##hub_sort", sortItems, 3, &sortKey)) {
            if      (std::strcmp(sortKey, "mod")   == 0) m_sortMode = HubSortMode::LastModified;
            else if (std::strcmp(sortKey, "name")  == 0) m_sortMode = HubSortMode::NameAZ;
            else if (std::strcmp(sortKey, "songs") == 0) m_sortMode = HubSortMode::SongCount;
        }
        (void)prev;
        ImGui::PopItemWidth();
        ImGui::SameLine();
        {
            ImVec2 cur = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Button,        BgPanel2);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BgPanel3);
            ImGui::PushStyleColor(ImGuiCol_Border,        BorderHi);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   4.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {6.f, 6.f});
            ImGui::Button("##funnel", {26.f, 26.f});
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(3);
            drawFunnelIcon(ImGui::GetWindowDrawList(),
                           {cur.x + 13.f, cur.y + 13.f}, ToU32(TextMid));
        }

        ImGui::Spacing();

        // Build the sorted index view, then apply rail + mode + search filters.
        std::vector<int> order(m_projects.size());
        for (int i = 0; i < (int)m_projects.size(); ++i) order[i] = i;
        switch (m_sortMode) {
            case HubSortMode::LastModified:
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return m_projects[a].lastModifiedRaw > m_projects[b].lastModifiedRaw;
                });
                break;
            case HubSortMode::NameAZ:
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return m_projects[a].name < m_projects[b].name;
                });
                break;
            case HubSortMode::SongCount:
                std::sort(order.begin(), order.end(), [&](int a, int b){
                    return m_projects[a].songCount > m_projects[b].songCount;
                });
                break;
        }

        // Recent rail = top 3 by modified time, regardless of current sort.
        std::set<int> recentSet;
        if (m_leftRail == HubLeftRail::Recent) {
            std::vector<int> byMod = order;
            std::sort(byMod.begin(), byMod.end(), [&](int a, int b){
                return m_projects[a].lastModifiedRaw > m_projects[b].lastModifiedRaw;
            });
            for (int i = 0; i < (int)byMod.size() && i < 3; ++i)
                recentSet.insert(byMod[i]);
        }

        std::string query = m_searchBuf;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });

        int maxSongs = 1;
        for (const auto& p : m_projects) maxSongs = std::max(maxSongs, p.songCount);

        // Table.
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, Border);
        ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, BorderHi);
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg,    BgPanel);
        ImGui::PushStyleColor(ImGuiCol_TableRowBg,       BgBase);
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt,    BgBase);
        const ImGuiTableFlags tflags = ImGuiTableFlags_BordersInnerH
                                     | ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_ScrollY
                                     | ImGuiTableFlags_NoBordersInBody;
        if (ImGui::BeginTable("##projects", 4, tflags,
                              ImVec2(0, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupColumn("NAME",     ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("MODE",     ImGuiTableColumnFlags_WidthFixed, 110.f);
            ImGui::TableSetupColumn("SONGS",    ImGuiTableColumnFlags_WidthFixed, 120.f);
            ImGui::TableSetupColumn("MODIFIED", ImGuiTableColumnFlags_WidthFixed, 210.f);
            ImGui::TableSetupScrollFreeze(0, 1);

            // Custom uppercase headers (TextLow).
            ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
            ImGui::TableHeadersRow();
            ImGui::PopStyleColor();

            int visibleCount = 0;
            for (int idx : order) {
                const auto& proj = m_projects[idx];

                if (m_leftRail == HubLeftRail::Recent && !recentSet.count(idx)) continue;
                if (m_leftRail == HubLeftRail::Starred && !m_starred.count(proj.name)) continue;

                if (!query.empty()) {
                    std::string lower = proj.name;
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                                   [](unsigned char c) { return (char)std::tolower(c); });
                    if (lower.find(query) == std::string::npos) continue;
                }
                if (m_modeFilter >= 0 && modeFilterIdxFor(proj) != m_modeFilter) continue;

                ++visibleCount;

                ImGui::PushID(idx);
                ImGui::TableNextRow(0, 56.f);

                const ModeView mv = modeViewFor(proj);

                // Hit area covering the full row (NAME col); SpanAllColumns
                // makes selection/click behave for the whole row.
                ImGui::TableSetColumnIndex(0);
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                const float fullRowW =
                    ImGui::GetContentRegionAvail().x
                    + ImGui::GetColumnWidth(1)
                    + ImGui::GetColumnWidth(2)
                    + ImGui::GetColumnWidth(3) + 24.f;
                const float rowH = 56.f;
                const bool selected = (idx == m_selectedIdx);
                ImDrawList* dl = ImGui::GetWindowDrawList();

                if (selected) {
                    dl->AddRectFilledMultiColor(rowMin,
                        {rowMin.x + fullRowW, rowMin.y + rowH},
                        ToU32(WithAlpha(Cyan, 0.10f)), 0,
                        0, ToU32(WithAlpha(Cyan, 0.10f)));
                    dl->AddRectFilled(rowMin,
                        {rowMin.x + 3.f, rowMin.y + rowH},
                        ToU32(Cyan));
                }

                // Icon tile (32×32 colored letter).
                const ImVec2 tileMin{rowMin.x + 10.f, rowMin.y + 12.f};
                const ImVec2 tileMax{tileMin.x + 32.f, tileMin.y + 32.f};
                dl->AddRectFilled(tileMin, tileMax,
                                  ToU32(WithAlpha(mv.color, 0.18f)), 6.f);
                dl->AddRect(tileMin, tileMax,
                            ToU32(WithAlpha(mv.color, 0.50f)), 6.f);
                {
                    char letter[2] = {0, 0};
                    letter[0] = std::toupper((unsigned char)proj.name[0]);
                    const float ts = ImGui::GetFontSize() * 1.1f;
                    const float tw = ImGui::CalcTextSize(letter).x;
                    dl->AddText(nullptr, ts,
                        {tileMin.x + (32.f - tw) * 0.5f,
                         tileMin.y + (32.f - ts) * 0.5f - 1.f},
                        ToU32(mv.color), letter);
                }

                // Name (bodyMed) + path (monoSm) — two-line layout.
                {
                    ImFont* nameFont = ui::fonts.bodyMed ? ui::fonts.bodyMed : ImGui::GetFont();
                    dl->AddText(nameFont, nameFont->FontSize,
                        {rowMin.x + 52.f, rowMin.y + 10.f},
                        ToU32(TextHi), proj.name.c_str());
                    char pathBuf[256];
                    snprintf(pathBuf, sizeof(pathBuf), "Projects/%s/", proj.name.c_str());
                    ImFont* pathFont = ui::fonts.monoSm ? ui::fonts.monoSm : ImGui::GetFont();
                    dl->AddText(pathFont, pathFont->FontSize,
                        {rowMin.x + 52.f, rowMin.y + 30.f},
                        ToU32(TextLow), pathBuf);
                }

                // Star toggle — place it as an InvisibleButton FIRST so it
                // wins clicks before the row Selectable below catches them.
                const float nameAvailW = ImGui::GetContentRegionAvail().x;
                const ImVec2 starHitMin{rowMin.x + nameAvailW - 26.f, rowMin.y + 18.f};
                const ImVec2 starHitMax{starHitMin.x + 22.f, starHitMin.y + 22.f};
                ImGui::SetCursorScreenPos(starHitMin);
                bool starClicked = ImGui::InvisibleButton("##star", {22.f, 22.f});
                const bool starHovered = ImGui::IsItemHovered();
                const bool starred = m_starred.count(proj.name) > 0;
                drawStarIcon(dl,
                    {starHitMin.x + 11.f, starHitMin.y + 11.f}, 7.5f,
                    ToU32(starred ? Amber
                           : (starHovered ? WithAlpha(Amber, 0.75f)
                                          : WithAlpha(TextLow, 0.55f))),
                    starred);
                if (starClicked) {
                    if (starred) m_starred.erase(proj.name);
                    else         m_starred.insert(proj.name);
                    saveStarred();
                }

                // Whole-row clickable Selectable. Placed AFTER the star so
                // the star's InvisibleButton steals the click first when the
                // pointer is over it.
                ImGui::SetCursorScreenPos(rowMin);
                ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0,0,0,0));
                bool clicked = ImGui::Selectable("##row", selected,
                    ImGuiSelectableFlags_AllowDoubleClick |
                    ImGuiSelectableFlags_SpanAllColumns,
                    ImVec2(0, rowH));
                ImGui::PopStyleColor(3);
                bool dbl = clicked && ImGui::IsMouseDoubleClicked(0);
                if (clicked && !starClicked) {
                    m_selectedIdx = idx;
                    if (dbl && engine) {
                        m_selectedProject = proj;
                        m_projectSelected = true;
                        engine->openProject(proj.path);
                        engine->startScreenEditor().load(proj.path);
                        engine->switchLayer(EditorLayer::StartScreen);
                        if (m_launchCallback) m_launchCallback(proj);
                    }
                }
                (void)starHitMax;

                // MODE column.
                ImGui::TableSetColumnIndex(1);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 18.f);
                ui::Pill(mv.label, mv.color, /*solid*/ false);

                // SONGS column: centered number + small progress bar below.
                ImGui::TableSetColumnIndex(2);
                {
                    ImVec2 cur = ImGui::GetCursorScreenPos();
                    const float colW = ImGui::GetColumnWidth();
                    char buf[16];
                    snprintf(buf, sizeof(buf), "%d", proj.songCount);
                    ImFont* mono = ui::fonts.mono ? ui::fonts.mono : ImGui::GetFont();
                    ImVec2 numSz = mono->CalcTextSizeA(mono->FontSize, FLT_MAX, 0.f, buf);
                    dl->AddText(mono, mono->FontSize,
                        {cur.x + (colW - numSz.x) * 0.5f, cur.y + 12.f},
                        ToU32(TextHi), buf);
                    const float barW = std::min(colW - 16.f, 70.f);
                    const float barX = cur.x + (colW - barW) * 0.5f;
                    const float barY = cur.y + 32.f;
                    const float barH = 3.f;
                    dl->AddRectFilled({barX, barY}, {barX + barW, barY + barH},
                                      ToU32(BgPanel3), 2.f);
                    const float fillW = barW *
                        std::min(1.f, (float)proj.songCount / (float)maxSongs);
                    if (fillW > 0.5f)
                        dl->AddRectFilled({barX, barY}, {barX + fillW, barY + barH},
                                          ToU32(WithAlpha(Cyan, 0.85f)), 2.f);
                }

                // MODIFIED column.
                ImGui::TableSetColumnIndex(3);
                {
                    ImVec2 cur = ImGui::GetCursorScreenPos();
                    cur.y += 20.f;
                    ImFont* mono = ui::fonts.monoSm ? ui::fonts.monoSm : ImGui::GetFont();
                    dl->AddText(mono, mono->FontSize, cur,
                        ToU32(TextMid),
                        proj.lastModified.empty() ? "-" : proj.lastModified.c_str());
                    // Right chevron.
                    const float colW = ImGui::GetColumnWidth();
                    const ImVec2 chCenter{cur.x + colW - 18.f, cur.y + 8.f};
                    const float chs = 5.f;
                    dl->AddTriangleFilled(
                        {chCenter.x - chs * 0.5f, chCenter.y - chs},
                        {chCenter.x - chs * 0.5f, chCenter.y + chs},
                        {chCenter.x + chs * 0.6f, chCenter.y},
                        ToU32(TextLow));
                }

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (visibleCount == 0) {
                ImGui::TextDisabled("No projects match the current filter.");
            }
        }
        ImGui::PopStyleColor(5);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ── Detail panel (right) ────────────────────────────────────────────────
    ImGui::BeginChild("##hub_detail", ImVec2(detailW, colsH), false,
                      ImGuiWindowFlags_NoScrollbar);
    {
        const bool hasSel = m_selectedIdx >= 0
                            && m_selectedIdx < (int)m_projects.size();
        if (!hasSel) {
            ImGui::TextDisabled("Select a project to see details.");
        } else {
            const ProjectInfo& sel = m_projects[m_selectedIdx];
            const ModeView mv = modeViewFor(sel);

            // ── Header row: 44×44 colored letter tile + (name / subtitle) ──
            {
                const float tile = 44.f;
                ImVec2 origin = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(origin, {origin.x + tile, origin.y + tile},
                                  ToU32(WithAlpha(mv.color, 0.18f)), 6.f);
                dl->AddRect(origin, {origin.x + tile, origin.y + tile},
                            ToU32(WithAlpha(mv.color, 0.50f)), 6.f);
                char letter[2] = {(char)std::toupper((unsigned char)sel.name[0]), 0};
                ImFont* headFont = ui::fonts.heading ? ui::fonts.heading : ImGui::GetFont();
                ImVec2 lsz = headFont->CalcTextSizeA(headFont->FontSize, FLT_MAX, 0.f, letter);
                dl->AddText(headFont, headFont->FontSize,
                    {origin.x + (tile - lsz.x) * 0.5f,
                     origin.y + (tile - headFont->FontSize) * 0.5f},
                    ToU32(mv.color), letter);

                ImFont* nameFont = ui::fonts.bodyMed ? ui::fonts.bodyMed : ImGui::GetFont();
                dl->AddText(nameFont, nameFont->FontSize,
                    {origin.x + tile + 12.f, origin.y + 4.f},
                    ToU32(TextHi), sel.name.c_str());

                ImFont* subFont = ui::fonts.monoSm ? ui::fonts.monoSm : ImGui::GetFont();
                char subBuf[64];
                snprintf(subBuf, sizeof(subBuf), "%s  -  %d songs", mv.label, sel.songCount);
                dl->AddText(subFont, subFont->FontSize,
                    {origin.x + tile + 12.f, origin.y + 24.f},
                    ToU32(TextLow), subBuf);

                ImGui::SetCursorScreenPos({origin.x, origin.y + tile + 14.f});
            }

            // ── Metadata box (BgPanel2, rounded) ────────────────────────
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 metaOrigin = ImGui::GetCursorScreenPos();
                const float metaW = ImGui::GetContentRegionAvail().x;

                ImGui::PushStyleColor(ImGuiCol_ChildBg, BgPanel2);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
                ImGui::BeginChild("##meta_box", ImVec2(metaW, 0), true,
                    ImGuiWindowFlags_AutoResize | ImGuiWindowFlags_NoScrollbar);

                ImFont* labelFont = ui::fonts.label ? ui::fonts.label : ImGui::GetFont();
                ImFont* monoSmFont = ui::fonts.monoSm ? ui::fonts.monoSm : ImGui::GetFont();

                if (ImGui::BeginTable("##meta", 2,
                        ImGuiTableFlags_SizingFixedFit
                        | ImGuiTableFlags_NoBordersInBody)) {
                    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110.f);
                    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

                    auto row = [&](const char* k, const std::string& v) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        std::string upper;
                        for (const char* p = k; *p; ++p)
                            upper += (char)((*p >= 'a' && *p <= 'z') ? (*p - 32) : *p);
                        ImGui::PushFont(labelFont);
                        ImGui::PushStyleColor(ImGuiCol_Text, TextLow);
                        ImGui::TextUnformatted(upper.c_str());
                        ImGui::PopStyleColor();
                        ImGui::PopFont();

                        ImGui::TableSetColumnIndex(1);
                        ImGui::PushFont(monoSmFont);
                        ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
                        ImGui::PushTextWrapPos(0.f);
                        ImGui::TextUnformatted(v.empty() ? "-" : v.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::PopStyleColor();
                        ImGui::PopFont();
                    };

                    row("Version",       sel.version);
                    row("Default chart", sel.defaultChart);
                    row("Shader path",   sel.shaderPath);
                    row("Last opened",   sel.lastModified);
                    row("Path",          sel.path);
                    ImGui::EndTable();
                }

                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            ImGui::Spacing();

            if (ui::PrimaryButton("Open Project", Cyan, ImVec2(-1, 32))) {
                m_selectedProject = sel;
                m_projectSelected = true;
                if (engine) {
                    engine->openProject(sel.path);
                    engine->startScreenEditor().load(sel.path);
                    engine->switchLayer(EditorLayer::StartScreen);
                }
                if (m_launchCallback) m_launchCallback(sel);
            }
            ImGui::Spacing();

            const float halfW = (ImGui::GetContentRegionAvail().x - 6.f) * 0.5f;
            if (ui::GhostButton("Reveal", ImVec2(halfW, 26)))
                revealInExplorer(sel.path);
            ImGui::SameLine();
            if (ui::GhostButton("Add file##detail", ImVec2(halfW, 26))) {
                m_showAddFileDialog = true;
                m_addFileError.clear();
            }
            ImGui::Spacing();

            // Package APK card.
            ImGui::PushStyleColor(ImGuiCol_ChildBg, BgPanel);
            ImGui::PushStyleColor(ImGuiCol_Border,  BorderHi);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
            const float cardH = m_apkProjectName.empty() ? 56.f
                                : (m_apkRunning ? 96.f : 130.f);
            ImGui::BeginChild("##apk_card", ImVec2(0, cardH), true,
                              ImGuiWindowFlags_NoScrollbar);
            {
                // Header row: amber square + uppercase TextLow label + Build.
                ImVec2 cur = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(cur, {cur.x + 14.f, cur.y + 14.f},
                                  ToU32(WithAlpha(Amber, 0.85f)), 3.f);
                ImGui::SetCursorScreenPos({cur.x + 22.f, cur.y});
                ImGui::PushStyleColor(ImGuiCol_Text, TextLow);
                ImGui::TextUnformatted("PACKAGE APK");
                ImGui::PopStyleColor();

                ImGui::SameLine();
                const float availW = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availW - 64.f);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.f);
                if (m_apkRunning) ImGui::BeginDisabled();
                ImGui::PushStyleColor(ImGuiCol_Button,        BgPanel3);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BgPanel2);
                ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.f, 1.f, 1.f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_Border,        BorderHi);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
                if (ImGui::Button("Build", ImVec2(60.f, 22.f)))
                    startApkBuild(sel);
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
                if (m_apkRunning) ImGui::EndDisabled();

                ImGui::Dummy(ImVec2(0, 4.f));
                renderApkPanel();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::EndChild();

    ImGui::End();

    renderCreateDialog(engine);
    renderAddFileDialog();
}
#endif  // inner legacy render path
#endif  // outer dead-code disable

// ── import existing project by path ──────────────────────────────────────────

bool ProjectHub::importProject(const std::string& srcPath) {
    fs::path src = srcPath;
    // Trim stray surrounding quotes (common when users paste from Explorer).
    std::string s = srcPath;
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    src = s;

    std::error_code ec;
    if (!fs::exists(src, ec)) {
        m_addFileError = "Path does not exist.";
        return false;
    }

    // Accept either a folder containing project.json, or a direct
    // project.json file (in which case we import its parent folder).
    fs::path srcDir;
    if (fs::is_regular_file(src) && src.filename() == "project.json")
        srcDir = src.parent_path();
    else if (fs::is_directory(src) && fs::exists(src / "project.json"))
        srcDir = src;
    else {
        m_addFileError =
            "Invalid format: folder must contain a project.json file.";
        return false;
    }

    std::string folderName = srcDir.filename().string();
    if (folderName.empty()) {
        m_addFileError = "Could not determine project folder name.";
        return false;
    }

    fs::path dst = fs::path("../../Projects") / folderName;
    if (fs::exists(dst)) {
        m_addFileError = "A project named '" + folderName + "' already exists.";
        return false;
    }

    try {
        fs::create_directories(dst.parent_path());
        fs::copy(srcDir, dst,
                 fs::copy_options::recursive |
                 fs::copy_options::overwrite_existing);
    } catch (const std::exception& e) {
        m_addFileError = std::string("Copy failed: ") + e.what();
        return false;
    }

    m_addFileError.clear();
    m_scanned = false;  // force rescan
    return true;
}

// ── add file dialog ──────────────────────────────────────────────────────────

void ProjectHub::renderAddFileDialog() {
    if (!m_showAddFileDialog) return;

    ImVec2 center{ImGui::GetIO().DisplaySize.x * 0.5f,
                  ImGui::GetIO().DisplaySize.y * 0.5f};
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 240), ImGuiCond_Always);
    ImGui::Begin("Add Project from File", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse);

    ImGui::TextWrapped(
        "Paste the path to a project folder or its project.json file. "
        "The folder must contain a valid project.json to be imported.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1);
    bool hitEnter = ImGui::InputTextWithHint(
        "##addpath", "C:/path/to/MyProject  (or project.json)",
        m_addFilePath, sizeof(m_addFilePath),
        ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!m_addFileError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.35f, 0.35f, 1.f));
        ImGui::TextWrapped("%s", m_addFileError.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    bool canImport = m_addFilePath[0] != '\0';
    if (!canImport) ImGui::BeginDisabled();
    bool doImport = ImGui::Button("Import", ImVec2(110, 32)) ||
                    (hitEnter && canImport);
    if (!canImport) ImGui::EndDisabled();

    if (doImport) {
        if (importProject(m_addFilePath)) {
            m_showAddFileDialog = false;
            std::memset(m_addFilePath, 0, sizeof(m_addFilePath));
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 32))) {
        m_showAddFileDialog = false;
        m_addFileError.clear();
        std::memset(m_addFilePath, 0, sizeof(m_addFilePath));
    }

    ImGui::End();
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

    // Stage a pruned copy of the project so the APK only ships each song's
    // currently-selected game mode. Falls back to the live project path if
    // staging fails (e.g. temp dir unwritable).
    fs::path staging = stageProjectForPackaging(fs::path(proj.path), safeName);
    std::string buildPath = staging.empty() ? proj.path : staging.string();
    m_apkStagingPath = staging.string();

    m_apkProjectName = proj.name;
    m_apkOutputPath  = outputApk.string();
    m_apkLogPath     = logPath.string();
    m_apkRunning     = true;
    m_showApkDialog  = true;
    m_apkExitCode    = 0;

    std::string scriptStr = script.string();
    std::string outStr    = outputApk.string();
    std::string logStr    = logPath.string();

    m_apkFuture = std::async(std::launch::async, [scriptStr, buildPath, outStr, logStr]() -> int {
        std::string cmd = "\"\"" + scriptStr + "\" \"" + buildPath +
                          "\" \"" + outStr + "\" > \"" + logStr + "\" 2>&1\"";
        return std::system(cmd.c_str());
    });
}

void ProjectHub::renderApkPanel() {
    // Reap the build future first so status flips before we render.
    if (m_apkRunning && m_apkFuture.valid() &&
        m_apkFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_apkExitCode = m_apkFuture.get();
        m_apkRunning  = false;

        // Drop the pruned staging copy — it has served its purpose whether
        // the build succeeded or failed.
        if (!m_apkStagingPath.empty()) {
            std::error_code ec;
            fs::remove_all(m_apkStagingPath, ec);
            m_apkStagingPath.clear();
        }
    }

    using namespace ui::tokens;

    // Inline status block — caller provides border + header. All text white.
    // Idle state: render nothing inside the card body (the header row alone
    // matches the prototype's compact "PACKAGE APK [Build]" card).
    if (m_apkProjectName.empty()) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
    if (!m_apkOutputPath.empty())
        ImGui::Text("Out: %s", m_apkOutputPath.c_str());
    ImGui::Spacing();

    if (m_apkRunning) {
        ImGui::TextColored(Amber, "Building... (running Gradle)");
        if (!m_apkLogPath.empty())
            ImGui::Text("Log: %s", m_apkLogPath.c_str());
    } else if (m_apkExitCode == 0 && !m_apkOutputPath.empty()) {
        ImGui::TextColored(Lime, "BUILD SUCCESSFUL");
        ImGui::Text("%s", m_apkOutputPath.c_str());
#ifdef _WIN32
        if (ImGui::Button("Show in Explorer", ImVec2(-1, 26))) {
            std::string arg = "/select,\"" + m_apkOutputPath + "\"";
            ShellExecuteA(nullptr, "open", "explorer.exe",
                          arg.c_str(), nullptr, SW_SHOWNORMAL);
        }
#endif
    } else if (m_apkExitCode != 0) {
        ImGui::TextColored(Red, "BUILD FAILED (exit %d)", m_apkExitCode);
        if (!m_apkLogPath.empty())
            ImGui::Text("Log: %s", m_apkLogPath.c_str());
#ifdef _WIN32
        if (ImGui::Button("Open Log", ImVec2(-1, 26))) {
            ShellExecuteA(nullptr, "open", m_apkLogPath.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
        }
#endif
    }
    ImGui::PopStyleColor();
}
