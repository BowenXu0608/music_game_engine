#include "ProjectHub.h"
#include "StyleTokens.h"
#include "Widgets.h"
#include "engine/Engine.h"
#include <imgui.h>
#include <algorithm>
#include <chrono>
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

void ProjectHub::render(Engine* engine) {
    scanProjects();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Project Hub", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);

    // Top bar matching MIGRATION mock — gradient logo + crumbs + Settings
    // gear (placeholder). Replaces the inline "Music Game Engine - Project
    // Hub" line that the rest of this function below used to print.
    ui::TopBar({"Project Hub"}, [&]{
        if (ui::TopNavForward("Settings")) {
            if (engine) engine->switchLayer(EditorLayer::Settings);
        }
    });

    // ── Header row (MIGRATION §3.1): big title + subtitle, right-aligned
    // search + Add file outline + Create primary-glow ─────────────────────
    using namespace ui::tokens;
    {
        const float headerH = 44.f;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        const float fullW = ImGui::GetContentRegionAvail().x;

        // Title (large) + subtitle (small mono).
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float titleSize = ImGui::GetFontSize() * 1.6f;
        dl->AddText(nullptr, titleSize, origin,
                    ToU32(TextHi), "Projects");

        // Last-opened sentinel: pick the first (most recent) entry post-scan.
        std::string subtitle = std::to_string(m_projects.size()) + " projects";
        if (!m_projects.empty())
            subtitle += " | last opened " + m_projects.front().name;
        ui::PushMono();
        dl->AddText({origin.x, origin.y + titleSize + 4.f},
                    ToU32(TextLow), subtitle.c_str());
        ui::PopMono();

        // Right-aligned: search box + Add file + Create game.
        ImGui::SetCursorScreenPos({origin.x + fullW - 480.f, origin.y + 6.f});
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##search", "Search projects...",
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

    // ── Filter pills row: All / 2D / 3D / Scan Line / Circle ────────────
    {
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
            const ImVec4 bg     = active ? f.color : WithAlpha(f.color, 0.14f);
            const ImVec4 fg     = active ? ImVec4{0,0,0,1} : f.color;
            const ImVec4 border = active ? f.color : WithAlpha(f.color, 0.40f);
            ImGui::PushStyleColor(ImGuiCol_Button,        bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bg);
            ImGui::PushStyleColor(ImGuiCol_Text,          fg);
            ImGui::PushStyleColor(ImGuiCol_Border,        border);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {10.f, 3.f});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   999.f);
            if (ImGui::Button(f.label))
                m_modeFilter = f.idx;
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(5);
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    ImGui::Spacing();

    if (m_projects.empty()) {
        ImGui::TextDisabled("No projects found. Create one to get started.");
        ImGui::End();
        renderCreateDialog(engine);
        renderAddFileDialog();
        return;
    }

    // ── Two-column layout: project list (fills) | detail panel (280 px) ────
    // Per MIGRATION §3.1. Selection in the list fills the detail panel, where
    // metadata + Open + inline APK build live.
    const float detailW = 300.f;
    const float listW   = std::max(200.f,
        ImGui::GetContentRegionAvail().x - detailW
        - ImGui::GetStyle().ItemSpacing.x);
    const float colsH   = std::max(160.f, ImGui::GetContentRegionAvail().y);

    ImGui::BeginChild("##hub_list", ImVec2(listW, colsH), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_HorizontalScrollbar);
    {
        // Case-insensitive substring match against project name.
        std::string query = m_searchBuf;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });

        int visibleCount = 0;
        for (size_t i = 0; i < m_projects.size(); ++i) {
            const auto& proj = m_projects[i];
            if (!query.empty()) {
                std::string lower = proj.name;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return (char)std::tolower(c); });
                if (lower.find(query) == std::string::npos) continue;
            }
            // Mode filter: 0 = Drop2D, 1 = Drop3D, 2 = ScanLine, 3 = Circle.
            if (m_modeFilter >= 0) {
                int rowIdx;
                switch (proj.gameMode) {
                    case GameModeType::DropNotes:
                        rowIdx = (proj.gameDim == DropDimension::ThreeD) ? 1 : 0; break;
                    case GameModeType::ScanLine: rowIdx = 2; break;
                    case GameModeType::Circle:   rowIdx = 3; break;
                    default: rowIdx = 0; break;
                }
                if (rowIdx != m_modeFilter) continue;
            }
            ++visibleCount;

            ImGui::PushID((int)i);
            const bool selected = (int)i == m_selectedIdx;

            const ImVec2 rowPos = ImGui::GetCursorScreenPos();
            const float  rowW   = ImGui::GetContentRegionAvail().x;
            const ImVec2 rowSize(rowW, 56.f);

            if (selected) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(rowPos,
                                  ImVec2(rowPos.x + rowW, rowPos.y + rowSize.y),
                                  ui::tokens::ToU32(ui::tokens::WithAlpha(ui::tokens::Cyan, 0.10f)), 6.f);
                dl->AddRect(rowPos,
                            ImVec2(rowPos.x + rowW, rowPos.y + rowSize.y),
                            ui::tokens::ToU32(ui::tokens::Cyan), 6.f, 0, 1.5f);
                dl->AddRectFilled(rowPos,
                                  ImVec2(rowPos.x + 3, rowPos.y + rowSize.y),
                                  ui::tokens::ToU32(ui::tokens::Cyan));
            }

            bool clicked = ImGui::Selectable("##proj_row", selected,
                                              ImGuiSelectableFlags_AllowDoubleClick,
                                              rowSize);
            bool dbl = clicked && ImGui::IsMouseDoubleClicked(0);

            ImGui::SetCursorScreenPos(ImVec2(rowPos.x + 14, rowPos.y + 8));
            ImGui::Text("%s", proj.name.c_str());
            ImGui::SetCursorScreenPos(ImVec2(rowPos.x + 14, rowPos.y + 30));
            ui::PushMono();
            ImGui::TextDisabled("Projects/%s/  |  v%s  |  %s",
                                proj.name.c_str(),
                                proj.version.c_str(),
                                proj.lastModified.empty() ? "-" : proj.lastModified.c_str());
            ui::PopMono();

            // Mode pill (right-aligned).
            const char* modeLabel = "Drop 2D";
            ImVec4 modeColor = ui::tokens::Cyan;
            switch (proj.gameMode) {
                case GameModeType::DropNotes:
                    if (proj.gameDim == DropDimension::ThreeD) {
                        modeLabel = "Drop 3D"; modeColor = ui::tokens::Magenta;
                    } else {
                        modeLabel = "Drop 2D"; modeColor = ui::tokens::Cyan;
                    } break;
                case GameModeType::ScanLine:
                    modeLabel = "Scan Line"; modeColor = ui::tokens::Amber; break;
                case GameModeType::Circle:
                    modeLabel = "Circle";    modeColor = ui::tokens::Lime;  break;
            }
            const float pillW = ImGui::CalcTextSize(modeLabel).x + 16.f;
            ImGui::SetCursorScreenPos(ImVec2(rowPos.x + rowW - pillW - 16.f,
                                              rowPos.y + (rowSize.y - 18.f) * 0.5f));
            ui::Pill(modeLabel, modeColor, /*solid*/ false);

            ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + rowSize.y + 4));

            if (clicked) {
                m_selectedIdx = (int)i;
                if (dbl && engine) {
                    m_selectedProject = proj;
                    m_projectSelected = true;
                    engine->openProject(proj.path);
                    engine->startScreenEditor().load(proj.path);
                    engine->switchLayer(EditorLayer::StartScreen);
                    if (m_launchCallback) m_launchCallback(proj);
                }
            }

            ImGui::PopID();
        }

        if (visibleCount == 0) {
            ImGui::TextDisabled("No projects match '%s'.", m_searchBuf);
        }
    }
    ImGui::EndChild();

    // ── Detail panel (right) ────────────────────────────────────────────────
    ImGui::SameLine();
    ImGui::BeginChild("##hub_detail", ImVec2(detailW, colsH), true,
                      ImGuiWindowFlags_NoScrollbar);
    {
        const bool hasSel = m_selectedIdx >= 0
                            && m_selectedIdx < (int)m_projects.size();
        if (!hasSel) {
            ImGui::TextDisabled("Select a project to see details.");
        } else {
            const ProjectInfo& sel = m_projects[m_selectedIdx];

            // Header tile: name + mode subtitle (mirrors React mock).
            ImGui::Text("%s", sel.name.c_str());
            const char* modeLabel = "Drop 2D";
            switch (sel.gameMode) {
                case GameModeType::DropNotes:
                    modeLabel = (sel.gameDim == DropDimension::ThreeD) ? "Drop 3D" : "Drop 2D"; break;
                case GameModeType::ScanLine: modeLabel = "Scan Line"; break;
                case GameModeType::Circle:   modeLabel = "Circle";    break;
            }
            ImGui::TextDisabled("%s  |  %d songs", modeLabel, sel.songCount);
            ImGui::Spacing();

            if (ui::SectionHeader("Metadata")) {
                auto kv = [](const char* k, const char* v) {
                    ImGui::TextDisabled("%s", k);
                    ImGui::SameLine(96.f);
                    ui::PushMono();
                    ImGui::TextWrapped("%s", v && *v ? v : "-");
                    ui::PopMono();
                };
                kv("Version",      sel.version.c_str());
                kv("Default chart",sel.defaultChart.c_str());
                kv("Shader path",  sel.shaderPath.c_str());
                kv("Last opened",  sel.lastModified.c_str());
                kv("Path",         sel.path.c_str());
                ImGui::Spacing();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Open Project — primary action.
            if (ImGui::Button(("Open " + sel.name).c_str(),
                              ImVec2(-1, 32))) {
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

            // Inline APK build (no popup).
            if (m_apkRunning) ImGui::BeginDisabled();
            ImGui::PushStyleColor(ImGuiCol_Button,
                ui::tokens::WithAlpha(ui::tokens::Amber, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::tokens::Amber);
            ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(0.f, 0.f, 0.f, 1.f));
            if (ImGui::Button("Build APK", ImVec2(-1, 26)))
                startApkBuild(sel);
            ImGui::PopStyleColor(3);
            if (m_apkRunning) ImGui::EndDisabled();

            ImGui::Spacing();
            renderApkPanel();
        }
    }
    ImGui::EndChild();

    ImGui::End();

    renderCreateDialog(engine);
    renderAddFileDialog();
}

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
    ImGui::PushStyleColor(ImGuiCol_ChildBg, BgPanel);
    ImGui::BeginChild("##apk_panel",
                      ImVec2(0, 168), true,
                      ImGuiWindowFlags_NoScrollbar);
    {
        ImGui::TextDisabled("PACKAGE APK");
        ImGui::Separator();

        if (m_apkProjectName.empty()) {
            ImGui::TextWrapped("Build an Android APK from the selected project. "
                               "Output lands under <project>/build/.");
        } else {
            ImGui::Text("Project: %s", m_apkProjectName.c_str());
            if (!m_apkOutputPath.empty())
                ImGui::TextDisabled("Out: %s", m_apkOutputPath.c_str());
        }
        ImGui::Spacing();

        if (m_apkRunning) {
            ImGui::TextColored(Amber,
                               "Building... (running Gradle)");
            if (!m_apkLogPath.empty())
                ImGui::TextDisabled("Log: %s", m_apkLogPath.c_str());
        } else if (!m_apkProjectName.empty() && m_apkExitCode == 0
                   && !m_apkOutputPath.empty()) {
            ImGui::TextColored(Lime, "BUILD SUCCESSFUL");
            ImGui::TextDisabled("%s", m_apkOutputPath.c_str());
#ifdef _WIN32
            if (ImGui::Button("Show in Explorer", ImVec2(-1, 26))) {
                std::string arg = "/select,\"" + m_apkOutputPath + "\"";
                ShellExecuteA(nullptr, "open", "explorer.exe",
                              arg.c_str(), nullptr, SW_SHOWNORMAL);
            }
#endif
        } else if (!m_apkProjectName.empty() && m_apkExitCode != 0) {
            ImGui::TextColored(Red, "BUILD FAILED (exit %d)", m_apkExitCode);
            if (!m_apkLogPath.empty())
                ImGui::TextDisabled("Log: %s", m_apkLogPath.c_str());
#ifdef _WIN32
            if (ImGui::Button("Open Log", ImVec2(-1, 26))) {
                ShellExecuteA(nullptr, "open", m_apkLogPath.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            }
#endif
        } else {
            ImGui::TextDisabled("Idle. Click Build to package.");
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
