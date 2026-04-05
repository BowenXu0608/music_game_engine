#include "SongEditor.h"
#include "engine/Engine.h"
#include "game/chart/ChartLoader.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/BufferManager.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#pragma comment(lib, "comdlg32.lib")
#endif

namespace fs = std::filesystem;

// ── Vulkan lifecycle ─────────────────────────────────────────────────────────

void SongEditor::initVulkan(VulkanContext& ctx, BufferManager& bufMgr,
                            ImGuiLayer& imgui, GLFWwindow* window) {
    m_ctx    = &ctx;
    m_bufMgr = &bufMgr;
    m_imgui  = &imgui;
    m_window = window;
}

void SongEditor::shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr) {
    clearThumbnails();
}

void SongEditor::setSong(SongInfo* song, const std::string& projectPath) {
    m_song          = song;
    m_projectPath   = projectPath;
    m_assetsScanned = false; // rescan when switching songs

    // Cancel any running analysis
    m_analyzer.cancel();

    // Clear editor notes and load from chart files if they exist
    m_diffNotes.clear();
    m_diffMarkers.clear();
    m_pressFirstClick = false;

    if (!song) return;

    auto loadChart = [&](Difficulty diff, const std::string& chartRel) {
        if (chartRel.empty()) return;
        std::string fullPath = projectPath + "/" + chartRel;
        try {
            ChartData chart = ChartLoader::load(fullPath);
            auto& edNotes = m_diffNotes[(int)diff];
            for (auto& n : chart.notes) {
                EditorNote en;
                en.time = (float)n.time;

                int lane = 0;
                if (auto* tap = std::get_if<TapData>(&n.data))        lane = (int)tap->laneX;
                else if (auto* hold = std::get_if<HoldData>(&n.data)) lane = (int)hold->laneX;
                else if (auto* flick = std::get_if<FlickData>(&n.data)) lane = (int)flick->laneX;
                en.track = lane;

                if (n.type == NoteType::Hold) {
                    en.type = EditorNoteType::Hold;
                    if (auto* hold = std::get_if<HoldData>(&n.data))
                        en.endTime = en.time + hold->duration;
                } else if (n.type == NoteType::Slide) {
                    en.type = EditorNoteType::Slide;
                } else {
                    en.type = EditorNoteType::Tap;
                }
                edNotes.push_back(en);
            }
            std::cout << "[SongEditor] Loaded " << edNotes.size() << " notes from " << chartRel << "\n";
        } catch (...) {
            // Chart file doesn't exist or can't be parsed — start empty
        }
    };

    loadChart(Difficulty::Easy,   song->chartEasy);
    loadChart(Difficulty::Medium, song->chartMedium);
    loadChart(Difficulty::Hard,   song->chartHard);
}

// ── Thumbnail cache ──────────────────────────────────────────────────────────

void SongEditor::clearThumbnails() {
    if (!m_ctx || !m_bufMgr) return;
    for (auto& [path, entry] : m_thumbCache) {
        if (entry.tex.image != VK_NULL_HANDLE) {
            vkDestroySampler(m_ctx->device(), entry.tex.sampler, nullptr);
            vkDestroyImageView(m_ctx->device(), entry.tex.view, nullptr);
            vmaDestroyImage(m_bufMgr->allocator(), entry.tex.image, entry.tex.allocation);
        }
    }
    m_thumbCache.clear();
}

VkDescriptorSet SongEditor::getThumb(const std::string& relPath) {
    auto it = m_thumbCache.find(relPath);
    if (it != m_thumbCache.end()) return it->second.desc;
    if (!m_ctx || !m_bufMgr || !m_imgui) return VK_NULL_HANDLE;

    std::string fullPath = m_projectPath + "/" + relPath;
    try {
        ThumbEntry entry;
        TextureManager texMgr;
        texMgr.init(*m_ctx, *m_bufMgr);
        entry.tex  = texMgr.loadFromFile(*m_ctx, *m_bufMgr, fullPath);
        entry.desc = m_imgui->addTexture(entry.tex.view, entry.tex.sampler);
        auto& stored = m_thumbCache[relPath] = std::move(entry);
        return stored.desc;
    } catch (...) {
        m_thumbCache[relPath] = {};
        return VK_NULL_HANDLE;
    }
}

// ── File browser helper ──────────────────────────────────────────────────────

std::string SongEditor::browseFile(const wchar_t* filter, const std::string& destSubdir) {
#ifdef _WIN32
    OPENFILENAMEW ofn = {};
    wchar_t szFile[512] = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = m_window ? glfwGetWin32Window(m_window) : nullptr;
    ofn.lpstrFile    = szFile;
    ofn.nMaxFile     = static_cast<DWORD>(sizeof(szFile) / sizeof(wchar_t));
    ofn.lpstrFilter  = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        int len = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, nullptr, 0, nullptr, nullptr);
        std::string srcPath(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, szFile, -1, srcPath.data(), len, nullptr, nullptr);

        fs::path absProject = fs::absolute(fs::path(m_projectPath));
        fs::path destDir    = absProject / "assets" / destSubdir;
        fs::create_directories(destDir);
        fs::path dest = destDir / fs::path(srcPath).filename();
        try {
            fs::copy_file(srcPath, dest, fs::copy_options::overwrite_existing);
        } catch (const std::exception& e) {
            std::cout << "[SongEditor] Copy failed: " << e.what() << "\n";
            return "";
        }
        std::string rel = fs::relative(dest, absProject).string();
        std::replace(rel.begin(), rel.end(), '\\', '/');
        m_assetsScanned = false; // rescan after import
        return rel;
    }
#endif
    return "";
}

// ── importFiles ──────────────────────────────────────────────────────────────

void SongEditor::importFiles(const std::vector<std::string>& srcPaths) {
    int copied = importAssetsToProject(m_projectPath, srcPaths);
    if (copied > 0) {
        m_statusMsg   = "Imported " + std::to_string(copied) + " file(s)";
        m_statusTimer = 3.f;
    }
    m_assetsScanned = false;
}

// ── Main render ──────────────────────────────────────────────────────────────
// Layout: Left sidebar (props+config+assets) | Right center (scene+timeline+waveform)

void SongEditor::render(Engine* engine) {
    if (m_statusTimer > 0.f) m_statusTimer -= ImGui::GetIO().DeltaTime;
    m_hoverTime = -1.f;  // reset each frame; set by timeline/waveform hover

    // Scan assets
    if (!m_assetsScanned && !m_projectPath.empty()) {
        m_assets        = scanAssets(m_projectPath);
        m_assetsScanned = true;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Song Editor", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    if (!m_song) {
        ImGui::Text("No song selected.");
        if (ImGui::Button("< Back to Music Selection")) {
            if (engine) engine->switchLayer(EditorLayer::MusicSelection);
        }
        ImGui::End();
        return;
    }

    loadWaveformIfNeeded(engine);

    // Poll async beat analysis
    m_analyzer.pollCompletion();

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float splitterThick = 5.f;
    const float navH      = 32.f;
    const float waveformH = 100.f;
    const float minSidebarW = 220.f;
    const float maxSidebarW = contentSize.x * 0.4f;

    m_sidebarW = std::clamp(m_sidebarW, minSidebarW, maxSidebarW);
    float mainW = std::max(200.f, contentSize.x - m_sidebarW - splitterThick);
    float bodyH = std::max(100.f, contentSize.y - navH - 4.f);

    // ══════════════════════════════════════════════════════════════════════════
    // LEFT SIDEBAR — Properties, Game Mode Config, Assets (scrollable)
    // ══════════════════════════════════════════════════════════════════════════
    ImGui::BeginChild("SESidebar", ImVec2(m_sidebarW, bodyH), true);
    {
        renderProperties();
        ImGui::Spacing();
        renderGameModeConfig();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Assets", ImGuiTreeNodeFlags_DefaultOpen))
            renderAssets();
    }
    ImGui::EndChild();

    // ── Horizontal splitter (sidebar | center) ──────────────────────────────
    ImGui::SameLine();
    ImGui::InvisibleButton("se_hsplit", ImVec2(splitterThick, bodyH));
    if (ImGui::IsItemActive()) {
        m_sidebarW += ImGui::GetIO().MouseDelta.x;
        m_sidebarW = std::clamp(m_sidebarW, minSidebarW, maxSidebarW);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // ══════════════════════════════════════════════════════════════════════════
    // RIGHT CENTER — Scene + Timeline + Waveform (all visible simultaneously)
    // ══════════════════════════════════════════════════════════════════════════
    ImGui::SameLine();
    ImGui::BeginChild("SECenter", ImVec2(mainW, bodyH), false);
    {
        float editableH = std::max(80.f, bodyH - waveformH - splitterThick);
        float sceneH    = std::max(40.f, editableH * m_sceneSplit);
        float timelineH = std::max(40.f, editableH - sceneH);

        // ── Scene Preview ───────────────────────────────────────────────────
        ImGui::BeginChild("SEScene", ImVec2(0, sceneH), true,
                          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
        {
            ImGui::TextDisabled("Preview");
            ImGui::SameLine();
            renderDifficultySelector();

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.y > 20.f) {
                ImVec2 sceneOrigin = ImGui::GetCursorScreenPos();
                ImVec2 sceneSize(avail.x, avail.y);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                renderSceneView(dl, sceneOrigin, sceneSize, engine);
                ImGui::Dummy(sceneSize);
            }
        }
        ImGui::EndChild();

        // ── Vertical splitter (scene | timeline) ────────────────────────────
        ImGui::InvisibleButton("se_vsplit", ImVec2(-1, splitterThick));
        if (ImGui::IsItemActive() && editableH > 1.f) {
            m_sceneSplit += ImGui::GetIO().MouseDelta.y / editableH;
            m_sceneSplit = std::clamp(m_sceneSplit, 0.15f, 0.65f);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

        // ── Chart Timeline ──────────────────────────────────────────────────
        ImGui::BeginChild("SETimeline", ImVec2(0, timelineH), true,
                          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
        {
            renderNoteToolbar();

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.y > 20.f) {
                ImVec2 tlOrigin = ImGui::GetCursorScreenPos();
                ImVec2 tlSize(avail.x, avail.y);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                renderChartTimeline(dl, tlOrigin, tlSize, engine);
                ImGui::Dummy(tlSize);
            }
        }
        ImGui::EndChild();

        // ── Waveform Strip ──────────────────────────────────────────────────
        ImGui::BeginChild("SEWaveform", ImVec2(0, waveformH), true,
                          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 wfOrigin = ImGui::GetCursorScreenPos();
            ImVec2 wfSize(avail.x, avail.y);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            renderWaveform(dl, wfOrigin, wfSize, engine);
            ImGui::Dummy(wfSize);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    // Persist hover time for Scene view
    if (m_hoverTime >= 0.f) m_sceneTime = m_hoverTime;

    // ── Nav bar ──────────────────────────────────────────────────────────────
    if (ImGui::Button("< Back")) {
        if (engine) engine->switchLayer(EditorLayer::MusicSelection);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (engine) {
            exportAllCharts();
            engine->musicSelectionEditor().save();
            m_statusMsg   = "Saved!";
            m_statusTimer = 2.f;
        }
    }
    ImGui::SameLine();

    // ── Test Game button ────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.55f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.2f, 0.65f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.1f, 0.45f, 0.15f, 1.0f));
    if (ImGui::Button("Test Game", ImVec2(90, 0))) {
        if (m_song && engine) {
            bool anyNotes = false;
            for (int d = 0; d < 3; d++) {
                if (!m_diffNotes[d].empty()) { anyNotes = true; break; }
            }
            if (!anyNotes) {
                m_testErrorMsg = "Cannot start test game!\n\n"
                                 "At least one difficulty must have notes.\n"
                                 "Place some notes in the timeline first.";
                m_showTestError = true;
            } else if (notes().empty()) {
                const char* diffNames[] = {"Easy", "Medium", "Hard"};
                m_testErrorMsg = std::string("No notes in the current difficulty (")
                                 + diffNames[(int)m_currentDifficulty] + ").\n\n"
                                 "Select a difficulty that has notes,\n"
                                 "or add notes to this difficulty first.";
                m_showTestError = true;
            } else {
                exportAllCharts();
                engine->musicSelectionEditor().save();
                launchTestProcess();
            }
        }
    }
    ImGui::PopStyleColor(3);

    // ── Audio playback controls ────────────────────────────────────────────
    ImGui::SameLine(0.f, 20.f);
    if (engine && m_song && !m_song->audioFile.empty()) {
        bool playing = engine->audio().isPlaying();
        if (playing) {
            if (ImGui::Button("Pause", ImVec2(50, 0))) {
                engine->audio().pause();
            }
        } else {
            if (ImGui::Button("Play", ImVec2(50, 0))) {
                std::string fullPath = m_projectPath + "/" + m_song->audioFile;
                if (engine->audio().positionSeconds() < 0) {
                    engine->audio().load(fullPath);
                }
                engine->audio().play();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(40, 0))) {
            engine->audio().stop();
        }
        ImGui::SameLine();
        // Show current position
        double pos = engine->audio().positionSeconds();
        if (pos >= 0) {
            int m = (int)pos / 60, s = (int)pos % 60;
            ImGui::TextDisabled("%d:%02d", m, s);
        }
    }

    if (m_statusTimer > 0.f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "%s", m_statusMsg.c_str());
    }

    // ── Error popup ─────────────────────────────────────────────────────────
    if (m_showTestError) {
        ImGui::OpenPopup("Test Game Error");
        m_showTestError = false;
    }
    if (ImGui::BeginPopupModal("Test Game Error", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_testErrorMsg.c_str());
        ImGui::Spacing();
        float btnW = 120.f;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - btnW) * 0.5f);
        if (ImGui::Button("OK", ImVec2(btnW, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
}

// ── renderProperties ─────────────────────────────────────────────────────────

void SongEditor::renderProperties() {
    ImGui::Text("Editing: %s", m_song->name.c_str());
    if (!m_song->artist.empty())
        ImGui::TextDisabled("by %s", m_song->artist.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    // ── Audio File ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        char audioBuf[256];
        strncpy(audioBuf, m_song->audioFile.c_str(), 255); audioBuf[255] = '\0';
        if (ImGui::InputText("Audio File", audioBuf, 256))
            m_song->audioFile = audioBuf;

        // Drag-drop target
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                m_song->audioFile = std::string(static_cast<const char*>(payload->Data), payload->DataSize - 1);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button("Browse##audio")) {
            std::string path = browseFile(
                L"Audio\0*.mp3;*.ogg;*.wav;*.flac;*.aac\0All Files\0*.*\0",
                "audio");
            if (!path.empty()) m_song->audioFile = path;
        }

        if (!m_song->audioFile.empty())
            ImGui::TextDisabled("File: %s", m_song->audioFile.c_str());

        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("Offset (s)", &m_song->gameMode.audioOffset, -2.f, 2.f, "%.3f s");
        ImGui::TextDisabled("Delay before notes start (sync audio with chart)");
    }

}

// ── renderGameModeConfig ─────────────────────────────────────────────────────

void SongEditor::renderGameModeConfig() {
    if (!m_song) return;
    GameModeConfig& gm = m_song->gameMode;

    ImGui::Text("Game Mode");
    ImGui::Separator();
    ImGui::Spacing();

    // ── Mode Type Selection ──────────────────────────────────────────────────
    ImGui::Text("Style");
    ImGui::Spacing();

    struct ModeOption {
        const char* label;
        const char* desc;
        GameModeType type;
    };
    ModeOption options[] = {
        {"Basic Drop Notes", "Notes fall toward a hit zone", GameModeType::DropNotes},
        {"Circle",           "Circle notes + scan line",     GameModeType::Circle},
        {"Scan Line",        "Judgment lines + attached notes", GameModeType::ScanLine},
    };

    for (auto& opt : options) {
        bool selected = (gm.type == opt.type);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button,       ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.35f, 0.75f, 1.0f));
        }
        if (ImGui::Button(opt.label, ImVec2(-1, 42)))
            gm.type = opt.type;
        if (selected)
            ImGui::PopStyleColor(3);

        ImVec2 btnMin = ImGui::GetItemRectMin();
        ImGui::SetCursorScreenPos(ImVec2(btnMin.x + 10, btnMin.y + 24));
        ImGui::TextDisabled("%s", opt.desc);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight());
        ImGui::Spacing();
    }

    ImGui::Spacing();

    // ── Dimension (only for DropNotes) ───────────────────────────────────────
    if (gm.type == GameModeType::DropNotes) {
        ImGui::Text("Dimension");
        ImGui::Spacing();

        float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        bool is2D = (gm.dimension == DropDimension::TwoD);
        bool is3D = (gm.dimension == DropDimension::ThreeD);

        if (is2D) {
            ImGui::PushStyleColor(ImGuiCol_Button,       ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.35f, 0.75f, 1.0f));
        }
        if (ImGui::Button("2D - Ground Only", ImVec2(w, 36)))
            gm.dimension = DropDimension::TwoD;
        if (is2D) ImGui::PopStyleColor(3);

        ImGui::SameLine();

        if (is3D) {
            ImGui::PushStyleColor(ImGuiCol_Button,       ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.35f, 0.75f, 1.0f));
        }
        if (ImGui::Button("3D - Ground + Sky", ImVec2(w, 36)))
            gm.dimension = DropDimension::ThreeD;
        if (is3D) ImGui::PopStyleColor(3);

        ImGui::Spacing();
    }

    // ── Track Count ──────────────────────────────────────────────────────────
    ImGui::Text("Tracks");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##tracks", &gm.trackCount, 3, 12, "%d tracks");
    ImGui::Spacing();

    // ── Judgment Windows ─────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Judgment Windows", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        // Color swatches as legend
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = ImGui::GetCursorScreenPos().x;
        float y = ImGui::GetCursorScreenPos().y;
        float sz = 10.f;

        auto legend = [&](ImU32 col, const char* label) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), col, 2.f);
            ImGui::Dummy(ImVec2(sz, sz));
            ImGui::SameLine();
            ImGui::Text("%s", label);
        };

        legend(IM_COL32(40, 200, 80, 200),  "Perfect");
        legend(IM_COL32(200, 180, 40, 200),  "Good");
        legend(IM_COL32(200, 80, 40, 200),   "Bad");
        ImGui::TextDisabled("Beyond Bad = Miss");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##perfect", &gm.perfectMs, 10.f, gm.goodMs, "Perfect: +/- %.0f ms");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##good", &gm.goodMs, gm.perfectMs, gm.badMs, "Good: +/- %.0f ms");
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##bad", &gm.badMs, gm.goodMs, 300.f, "Bad: +/- %.0f ms");

        ImGui::Spacing();
    }

    // ── Score per Judgment ───────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Score", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("Perfect##score", &gm.perfectScore, 50, 500);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("Good##score", &gm.goodScore, 50, 500);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputInt("Bad##score", &gm.badScore, 50, 500);
        ImGui::TextDisabled("Miss = 0");

        gm.perfectScore = std::clamp(gm.perfectScore, 0, 100000);
        gm.goodScore    = std::clamp(gm.goodScore,    0, gm.perfectScore);
        gm.badScore     = std::clamp(gm.badScore,     0, gm.goodScore);

        ImGui::Spacing();
    }

    // ── Achievements ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Achievements", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        // Auto-judge explanation
        ImGui::TextDisabled("Evaluated automatically at results screen:");
        ImGui::Spacing();

        // ── Full Combo (FC) ─────────────────────────────────────────────────
        ImGui::Text("Full Combo (FC)");
        ImGui::TextDisabled("No misses — every note hit");

        // FC image picker
        {
            char fcBuf[256];
            strncpy(fcBuf, gm.fcImage.c_str(), 255); fcBuf[255] = '\0';
            ImGui::SetNextItemWidth(-70);
            if (ImGui::InputText("##fcImg", fcBuf, 256))
                gm.fcImage = fcBuf;

            // Drag-drop target
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    gm.fcImage = std::string(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::Button("Browse##fc")) {
                std::string path = browseFile(
                    L"Images\0*.png;*.jpg;*.jpeg\0All Files\0*.*\0", "images");
                if (!path.empty()) gm.fcImage = path;
            }

            // Show thumbnail if set
            if (!gm.fcImage.empty()) {
                VkDescriptorSet fcThumb = getThumb(gm.fcImage);
                if (fcThumb) {
                    ImGui::Image((ImTextureID)(uint64_t)fcThumb, ImVec2(48, 48));
                } else {
                    ImGui::TextDisabled("[%s]", gm.fcImage.c_str());
                }
            }
        }

        ImGui::Spacing();

        // ── All Perfect (AP) ────────────────────────────────────────────────
        ImGui::Text("All Perfect (AP)");
        ImGui::TextDisabled("Every note judged Perfect");

        // AP image picker
        {
            char apBuf[256];
            strncpy(apBuf, gm.apImage.c_str(), 255); apBuf[255] = '\0';
            ImGui::SetNextItemWidth(-70);
            if (ImGui::InputText("##apImg", apBuf, 256))
                gm.apImage = apBuf;

            // Drag-drop target
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    gm.apImage = std::string(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (ImGui::Button("Browse##ap")) {
                std::string path = browseFile(
                    L"Images\0*.png;*.jpg;*.jpeg\0All Files\0*.*\0", "images");
                if (!path.empty()) gm.apImage = path;
            }

            // Show thumbnail if set
            if (!gm.apImage.empty()) {
                VkDescriptorSet apThumb = getThumb(gm.apImage);
                if (apThumb) {
                    ImGui::Image((ImTextureID)(uint64_t)apThumb, ImVec2(48, 48));
                } else {
                    ImGui::TextDisabled("[%s]", gm.apImage.c_str());
                }
            }
        }

        ImGui::Spacing();
    }

    // ── HUD Text: Score & Combo ─────────────────────────────────────────────
    if (ImGui::CollapsingHeader("HUD — Score & Combo", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();

        // Helper lambda to edit a HudTextConfig
        auto editHudText = [](const char* label, HudTextConfig& h) {
            ImGui::PushID(label);
            ImGui::Text("%s", label);
            ImGui::Separator();
            ImGui::SliderFloat2("Position", h.pos, 0.f, 1.f, "%.2f");
            ImGui::SliderFloat("Font Size", &h.fontSize, 10.f, 72.f);
            ImGui::SliderFloat("Scale", &h.scale, 0.1f, 5.f);
            ImGui::ColorEdit4("Color", h.color);
            ImGui::Checkbox("Bold", &h.bold);
            ImGui::Checkbox("Glow", &h.glow);
            if (h.glow) {
                ImGui::ColorEdit4("Glow Color", h.glowColor);
                ImGui::SliderFloat("Glow Radius", &h.glowRadius, 1.f, 24.f);
            }
            ImGui::PopID();
            ImGui::Spacing();
        };

        editHudText("Score Display", gm.scoreHud);
        editHudText("Combo Display", gm.comboHud);
    }


    // ── Background Image ───────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Spacing();
        ImGui::TextDisabled("Image displayed behind the gameplay scene");
        ImGui::Spacing();

        char bgBuf[256];
        strncpy(bgBuf, gm.backgroundImage.c_str(), 255); bgBuf[255] = '\0';
        ImGui::SetNextItemWidth(-70);
        if (ImGui::InputText("##bgImg", bgBuf, 256))
            gm.backgroundImage = bgBuf;

        // Drag-drop target
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                gm.backgroundImage = std::string(static_cast<const char*>(payload->Data), payload->DataSize - 1);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button("Browse##bg")) {
            std::string path = browseFile(
                L"Images\0*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0", "images");
            if (!path.empty()) gm.backgroundImage = path;
        }

        // Show thumbnail if set
        if (!gm.backgroundImage.empty()) {
            VkDescriptorSet bgThumb = getThumb(gm.backgroundImage);
            if (bgThumb) {
                ImGui::Image((ImTextureID)(uint64_t)bgThumb, ImVec2(120, 68));
            } else {
                ImGui::TextDisabled("[%s]", gm.backgroundImage.c_str());
            }
        }

        if (!gm.backgroundImage.empty()) {
            if (ImGui::Button("Clear##bgClear"))
                gm.backgroundImage.clear();
        }

        ImGui::Spacing();
    }
}

// ── loadWaveformIfNeeded ─────────────────────────────────────────────────────

void SongEditor::loadWaveformIfNeeded(Engine* engine) {
    if (!m_song || m_song->audioFile.empty()) {
        if (m_waveformLoaded) {
            m_waveform = WaveformData{};
            m_waveformAudioPath.clear();
            m_waveformLoaded = false;
        }
        return;
    }

    std::string fullPath = m_projectPath + "/" + m_song->audioFile;
    if (fullPath == m_waveformAudioPath && m_waveformLoaded) return;

    m_waveform = AudioEngine::decodeWaveform(fullPath);  // 65536-bucket finest LOD + derived coarser levels
    m_waveformAudioPath = fullPath;
    m_waveformLoaded = (m_waveform.bucketCount > 0);
}

// ── renderChartTimeline ─────────────────────────────────────────────────────

void SongEditor::renderChartTimeline(ImDrawList* dl, ImVec2 origin, ImVec2 size,
                                     Engine* engine) {
    if (!m_song) return;
    const GameModeConfig& gm = m_song->gameMode;

    ImVec2 pMax = ImVec2(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, pMax, true);

    // Background
    dl->AddRectFilled(origin, pMax, IM_COL32(20, 20, 35, 255));

    float visibleDuration = size.x / m_timelineZoom;
    float startTime = m_timelineScrollX;
    float endTime   = startTime + visibleDuration;
    int tc = gm.trackCount;

    if (gm.type == GameModeType::ScanLine) {
        // ── Scan Line mode: show screen Y range with scan line position ──
        // The full height represents the screen; scan line sweeps up/down
        // Draw a few reference lines showing screen regions
        for (int i = 0; i <= 4; i++) {
            float y = origin.y + (float)i / 4.f * size.y;
            dl->AddLine(ImVec2(origin.x, y), ImVec2(pMax.x, y),
                        IM_COL32(40, 40, 60, 120), 1.f);
        }
        // Y-axis labels (screen position %)
        const char* yLabels[] = {"Top", "25%", "50%", "75%", "Bottom"};
        for (int i = 0; i < 5; i++) {
            float y = origin.y + (float)i / 4.f * size.y;
            dl->AddText(ImVec2(origin.x + 4, y + 1),
                        IM_COL32(100, 100, 140, 160), yLabels[i]);
        }
        // Animated scan line position (zigzag over time)
        // Show scan line path as a colored wave through time
        float prevY = origin.y + size.y * 0.5f;
        for (float px = 0; px < size.x; px += 2.f) {
            float t = startTime + px / m_timelineZoom;
            // Scan line bounces between top and bottom
            float period = 4.f; // seconds per full cycle
            float phase = fmodf(t, period) / period;
            float scanFrac = (phase < 0.5f) ? (phase * 2.f) : (2.f - phase * 2.f);
            float curY = origin.y + scanFrac * size.y;
            if (px > 0) {
                dl->AddLine(ImVec2(origin.x + px - 2, prevY),
                            ImVec2(origin.x + px, curY),
                            IM_COL32(0, 200, 220, 140), 1.5f);
            }
            prevY = curY;
        }
    } else if (gm.type == GameModeType::DropNotes && gm.dimension == DropDimension::ThreeD) {
        // ── 3D Drop Notes: Sky tracks (purple) + Ground tracks (blue) ──
        float gap     = 4.f;
        float skyH    = (size.y - gap) * 0.4f;
        float groundH = (size.y - gap) * 0.6f;
        float skyTop  = origin.y;
        float skyBot  = skyTop + skyH;
        float gndTop  = skyBot + gap;
        float gndBot  = gndTop + groundH;

        // Sky region background
        dl->AddRectFilled(ImVec2(origin.x, skyTop), ImVec2(pMax.x, skyBot),
                          IM_COL32(35, 20, 50, 255));
        // Ground region background
        dl->AddRectFilled(ImVec2(origin.x, gndTop), ImVec2(pMax.x, gndBot),
                          IM_COL32(20, 25, 45, 255));

        // Sky track lanes
        float skyTrackH = skyH / tc;
        for (int i = 0; i <= tc; i++) {
            float y = skyTop + i * skyTrackH;
            ImU32 col = (i == 0 || i == tc) ? IM_COL32(160, 80, 220, 140)
                                             : IM_COL32(100, 50, 140, 60);
            dl->AddLine(ImVec2(origin.x, y), ImVec2(pMax.x, y), col, 1.f);
        }
        // Sky track labels
        for (int i = 0; i < tc; i++) {
            float y = skyTop + (i + 0.5f) * skyTrackH;
            char label[8]; snprintf(label, sizeof(label), "%d", i + 1);
            dl->AddText(ImVec2(origin.x + 4, y - 6),
                        IM_COL32(180, 100, 220, 140), label);
        }

        // Ground track lanes
        float gndTrackH = groundH / tc;
        for (int i = 0; i <= tc; i++) {
            float y = gndTop + i * gndTrackH;
            ImU32 col = (i == 0 || i == tc) ? IM_COL32(60, 100, 180, 140)
                                             : IM_COL32(40, 60, 120, 60);
            dl->AddLine(ImVec2(origin.x, y), ImVec2(pMax.x, y), col, 1.f);
        }
        // Ground track labels
        for (int i = 0; i < tc; i++) {
            float y = gndTop + (i + 0.5f) * gndTrackH;
            char label[8]; snprintf(label, sizeof(label), "%d", i + 1);
            dl->AddText(ImVec2(origin.x + 4, y - 6),
                        IM_COL32(80, 130, 200, 140), label);
        }

        // Region labels
        dl->AddText(ImVec2(origin.x + 20, skyTop + 2),
                    IM_COL32(200, 120, 255, 180), "Sky");
        dl->AddText(ImVec2(origin.x + 20, gndTop + 2),
                    IM_COL32(100, 160, 255, 180), "Ground");

        // Judge line is drawn by the shared block below (follows hover/playback)

    } else {
        // ── 2D Drop Notes / Circle: tc horizontal track lanes + judge line ──
        float trackH = size.y / tc;

        // Track lane separators
        for (int i = 0; i <= tc; i++) {
            float y = origin.y + i * trackH;
            ImU32 col = (i == 0 || i == tc) ? IM_COL32(60, 80, 140, 160)
                                             : IM_COL32(40, 50, 80, 80);
            dl->AddLine(ImVec2(origin.x, y), ImVec2(pMax.x, y), col, 1.f);
        }

        // Track number labels (left side)
        for (int i = 0; i < tc; i++) {
            float y = origin.y + (i + 0.5f) * trackH;
            char label[8]; snprintf(label, sizeof(label), "%d", i + 1);
            dl->AddText(ImVec2(origin.x + 4, y - 6),
                        IM_COL32(80, 100, 150, 140), label);
        }

    }

    // Draw user-placed markers (dashed vertical lines)
    for (float mt : markers()) {
        float mx2 = origin.x + (mt - startTime) * m_timelineZoom;
        if (mx2 < origin.x || mx2 > pMax.x) continue;
        // Dashed line: draw short segments
        for (float y = origin.y; y < pMax.y; y += 8.f) {
            float y2 = std::min(y + 4.f, pMax.y);
            dl->AddLine(ImVec2(mx2, y), ImVec2(mx2, y2),
                        IM_COL32(255, 140, 60, 180), 1.f);
        }
    }

    // Early mouse-in-rect check for note placement
    bool mouseInTimeline;
    {
        ImVec2 mp = ImGui::GetIO().MousePos;
        mouseInTimeline = (mp.x >= origin.x && mp.x <= pMax.x &&
                           mp.y >= origin.y && mp.y <= pMax.y);
    }

    // ── Render notes + handle note placement ─────────────────────────────────
    if (gm.type == GameModeType::DropNotes && gm.dimension == DropDimension::ThreeD) {
        // 3D: sky region + ground region
        float gap     = 4.f;
        float skyH    = (size.y - gap) * 0.4f;
        float groundH = (size.y - gap) * 0.6f;
        float skyTop  = origin.y;
        float gndTop  = skyTop + skyH + gap;
        float skyTrackH = skyH / tc;
        float gndTrackH = groundH / tc;

        // Render sky notes (skyOnly=true) and ground notes (skyOnly=false)
        renderNotes(dl, origin, size, startTime, tc, skyTrackH, skyTop, true);
        renderNotes(dl, origin, size, startTime, tc, gndTrackH, gndTop, false);

        // Handle note placement in sky or ground based on mouse Y
        if (mouseInTimeline && m_noteTool != NoteTool::None) {
            ImVec2 mpos2 = ImGui::GetIO().MousePos;
            if (mpos2.y < gndTop) {
                // Sky region — only Click and Press allowed
                if (m_noteTool != NoteTool::Slide)
                    handleNotePlacement(origin, size, startTime, tc, true, skyTrackH, skyTop, engine);
            } else {
                // Ground region — all types
                handleNotePlacement(origin, size, startTime, tc, false, gndTrackH, gndTop, engine);
            }
        }
    } else if (gm.type != GameModeType::ScanLine) {
        // 2D Drop Notes or Circle: single region
        float trackH = size.y / tc;
        renderNotes(dl, origin, size, startTime, tc, trackH, origin.y, false);

        if (mouseInTimeline && m_noteTool != NoteTool::None) {
            handleNotePlacement(origin, size, startTime, tc, false, trackH, origin.y, engine);
        }
    }

    // ── Pending Press preview line ──────────────────────────────────────────
    if (m_pressFirstClick && m_noteTool == NoteTool::Hold) {
        float px1 = origin.x + (m_pressStartTime - startTime) * m_timelineZoom;
        float mx  = ImGui::GetIO().MousePos.x;
        if (px1 >= origin.x && px1 <= pMax.x) {
            // Draw a dashed line from start to current mouse X
            float y;
            if (gm.type == GameModeType::DropNotes && gm.dimension == DropDimension::ThreeD) {
                float gap2     = 4.f;
                float skyH2    = (size.y - gap2) * 0.4f;
                float gndTop2  = origin.y + skyH2 + gap2;
                float trkH = m_pressStartSky ? (skyH2 / tc) : ((size.y - gap2) * 0.6f / tc);
                float regTop = m_pressStartSky ? origin.y : gndTop2;
                y = regTop + (m_pressStartTrack + 0.5f) * trkH;
            } else {
                float trkH = size.y / tc;
                y = origin.y + (m_pressStartTrack + 0.5f) * trkH;
            }
            // Draw a highlight bar from start to mouse
            dl->AddRectFilled(ImVec2(px1, y - 4), ImVec2(mx, y + 4),
                              IM_COL32(80, 200, 80, 100), 2.f);
            dl->AddRect(ImVec2(px1, y - 4), ImVec2(mx, y + 4),
                        IM_COL32(80, 200, 80, 200), 2.f, 0, 1.f);
        }
    }

    // Judge line — follows mouse X position directly (same time axis as waveform)
    {
        float mx = ImGui::GetIO().MousePos.x;
        // Show judge line when mouse is within the horizontal extent of this panel
        if (mx >= origin.x && mx <= pMax.x) {
            dl->AddLine(ImVec2(mx, origin.y), ImVec2(mx, pMax.y),
                        IM_COL32(255, 200, 60, 220), 2.f);
            // Time label
            float judgeT = startTime + (mx - origin.x) / m_timelineZoom;
            int jm = (int)judgeT / 60, js = (int)judgeT % 60;
            int jms = (int)(fmodf(judgeT, 1.f) * 100);
            char jBuf[24];
            snprintf(jBuf, sizeof(jBuf), "%d:%02d.%02d", jm, js, jms);
            dl->AddText(ImVec2(mx + 6, pMax.y - 16),
                        IM_COL32(255, 200, 60, 200), jBuf);
        }
    }

    // Playback cursor + timestamp
    if (engine) {
        double pos = engine->audio().positionSeconds();
        if (pos >= 0) {
            float cx = origin.x + (float)(pos - startTime) * m_timelineZoom;
            if (cx >= origin.x && cx <= pMax.x) {
                dl->AddLine(ImVec2(cx, origin.y), ImVec2(cx, pMax.y),
                            IM_COL32(255, 60, 60, 240), 2.f);
                // Timestamp label at cursor
                int m = (int)pos / 60, s = (int)pos % 60, ms = (int)(fmod(pos, 1.0) * 100);
                char tsBuf[24];
                snprintf(tsBuf, sizeof(tsBuf), "%d:%02d.%02d", m, s, ms);
                dl->AddText(ImVec2(cx + 4, origin.y + 2),
                            IM_COL32(255, 100, 100, 240), tsBuf);
            }
        }
    }

    // Current view timestamp (top-left corner of visible area)
    {
        int m = (int)startTime / 60, s = (int)startTime % 60;
        char viewBuf[32];
        snprintf(viewBuf, sizeof(viewBuf), "Zoom: %.0f px/s  @%d:%02d",
                 m_timelineZoom, m, s);
        dl->AddText(ImVec2(pMax.x - 140, origin.y + 2),
                    IM_COL32(140, 140, 180, 200), viewBuf);
    }

    dl->PopClipRect();

    // Hover cursor line + time label + click to place markers
    if (mouseInTimeline) {
        ImVec2 mpos = ImGui::GetIO().MousePos;
        float mx = mpos.x;
        float hoverT = startTime + (mx - origin.x) / m_timelineZoom;
        m_hoverTime = hoverT;

        // Vertical hover line
        dl->PushClipRect(origin, pMax, true);
        dl->AddLine(ImVec2(mx, origin.y), ImVec2(mx, pMax.y),
                    IM_COL32(255, 255, 255, 120), 1.f);
        // Time label at cursor
        int hm = (int)hoverT / 60, hs = (int)hoverT % 60;
        int hms = (int)(fmodf(hoverT, 1.f) * 100);
        char hBuf[24];
        snprintf(hBuf, sizeof(hBuf), "%d:%02d.%02d", hm, hs, hms);
        dl->AddText(ImVec2(mx + 6, origin.y + 14),
                    IM_COL32(255, 255, 255, 200), hBuf);
        dl->PopClipRect();

        ImGuiIO& io = ImGui::GetIO();
        if (m_noteTool == NoteTool::None) {
            // Left-click: place marker (raw IO)
            if (io.MouseClicked[0] && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt) {
                markers().push_back(hoverT);
            }
        }

        // Right-click: delete nearest note first; if no note found, delete nearest marker
        if (io.MouseClicked[1]) {
            float threshold = 5.f / m_timelineZoom;
            bool deletedNote = false;

            // Try to delete a note
            if (!notes().empty()) {
                int bestIdx = -1;
                float bestDist = threshold;
                for (int i = 0; i < (int)notes().size(); i++) {
                    float d = fabsf(notes()[i].time - hoverT);
                    if (d < bestDist) { bestDist = d; bestIdx = i; }
                }
                if (bestIdx >= 0) {
                    notes().erase(notes().begin() + bestIdx);
                    m_pressFirstClick = false;
                    deletedNote = true;
                }
            }

            // If no note deleted, try to delete a marker
            if (!deletedNote && !markers().empty()) {
                int bestIdx = -1;
                float bestDist = threshold;
                for (int i = 0; i < (int)markers().size(); i++) {
                    float d = fabsf(markers()[i] - hoverT);
                    if (d < bestDist) { bestDist = d; bestIdx = i; }
                }
                if (bestIdx >= 0)
                    markers().erase(markers().begin() + bestIdx);
            }
        }
    }

    // Debug: marker + note count (always visible)
    {
        const char* diffNames[] = {"Easy", "Medium", "Hard"};
        const char* diffName = diffNames[(int)m_currentDifficulty];
        char mcBuf[64];
        snprintf(mcBuf, sizeof(mcBuf), "[%s] Markers: %d  Notes: %d",
                 diffName, (int)markers().size(), (int)notes().size());
        dl->AddText(ImVec2(origin.x + 4, origin.y + 4),
                    IM_COL32(255, 255, 0, 255), mcBuf);
    }

    // Scroll/zoom: wheel = scroll, Ctrl+wheel = zoom (resolution)
    if (mouseInTimeline) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) {
            if (ImGui::GetIO().KeyCtrl) {
                // Zoom (resolution)
                float oldZoom = m_timelineZoom;
                m_timelineZoom *= (1.f + wheel * 0.15f);
                if (m_timelineZoom < 10.f)    m_timelineZoom = 10.f;
                if (m_timelineZoom > 2000.f)  m_timelineZoom = 2000.f;
                // Zoom toward mouse position
                float mouseT = startTime + (ImGui::GetIO().MousePos.x - origin.x) / oldZoom;
                m_timelineScrollX = mouseT - (ImGui::GetIO().MousePos.x - origin.x) / m_timelineZoom;
            } else {
                // Horizontal scroll
                m_timelineScrollX -= wheel * 0.5f;
            }
            if (m_timelineScrollX < 0.f) m_timelineScrollX = 0.f;
            float maxScroll = m_waveformLoaded ? (float)m_waveform.durationSeconds : 60.f;
            if (m_timelineScrollX > maxScroll) m_timelineScrollX = maxScroll;
        }
    }

    // Auto-follow during playback
    if (engine && engine->audio().isPlaying()) {
        double pos = engine->audio().positionSeconds();
        if (pos >= 0 && (float)pos > m_timelineScrollX + visibleDuration * 0.8f)
            m_timelineScrollX = (float)pos - visibleDuration * 0.3f;
    }
}

// ── renderWaveform ──────────────────────────────────────────────────────────

void SongEditor::renderWaveform(ImDrawList* dl, ImVec2 origin, ImVec2 size,
                                Engine* engine) {
    ImVec2 pMax = ImVec2(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, pMax, true);

    // Background
    dl->AddRectFilled(origin, pMax, IM_COL32(15, 15, 25, 255));

    if (!m_waveformLoaded) {
        const char* hint = "No audio loaded";
        ImVec2 tsz = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(origin.x + (size.x - tsz.x) * 0.5f,
                           origin.y + (size.y - tsz.y) * 0.5f),
                    IM_COL32(100, 100, 120, 160), hint);
        dl->PopClipRect();
        return;
    }

    float midY  = origin.y + size.y * 0.5f;
    float halfH = size.y * 0.45f;

    // Center reference line
    dl->AddLine(ImVec2(origin.x, midY), ImVec2(pMax.x, midY),
                IM_COL32(40, 40, 60, 120), 1.f);

    // Draw waveform envelope
    float startTime = m_timelineScrollX;

    // LOD selection: pick the coarsest level that still provides >= 1 bucket per pixel.
    // This gives maximum detail at high zoom, and falls back to coarser data when zoomed out.
    const WaveformLOD* lod = &m_waveform.lods[0];  // finest as fallback (very high zoom)
    for (int i = (int)m_waveform.lods.size() - 1; i >= 0; --i) {
        double bucketsPerSec = (double)m_waveform.lods[i].bucketCount / m_waveform.durationSeconds;
        if (bucketsPerSec >= (double)m_timelineZoom) {
            lod = &m_waveform.lods[i];
            break;  // coarsest LOD with at least 1 bucket per pixel — sharp without waste
        }
    }

    double secsPerBucket = m_waveform.durationSeconds / lod->bucketCount;

    for (float px = 0; px < size.x; px += 1.f) {
        float t = startTime + px / m_timelineZoom;
        int bucket = (int)(t / secsPerBucket);
        if (bucket < 0 || bucket >= (int)lod->bucketCount) continue;

        float mn = lod->minSamples[bucket];
        float mx = lod->maxSamples[bucket];

        float y0 = midY - mx * halfH;
        float y1 = midY - mn * halfH;

        dl->AddLine(ImVec2(origin.x + px, y0), ImVec2(origin.x + px, y1),
                    IM_COL32(60, 150, 255, 180));
    }

    // Draw user-placed markers (dashed lines, synced with chart timeline)
    for (float mt : markers()) {
        float markerX = origin.x + (mt - startTime) * m_timelineZoom;
        if (markerX < origin.x || markerX > pMax.x) continue;
        for (float y = origin.y; y < pMax.y; y += 8.f) {
            float y2 = std::min(y + 4.f, pMax.y);
            dl->AddLine(ImVec2(markerX, y), ImVec2(markerX, y2),
                        IM_COL32(255, 140, 60, 180), 1.f);
        }
    }

    // Playback cursor + timestamp
    if (engine) {
        double pos = engine->audio().positionSeconds();
        if (pos >= 0) {
            float cx = origin.x + (float)(pos - startTime) * m_timelineZoom;
            if (cx >= origin.x && cx <= pMax.x) {
                dl->AddLine(ImVec2(cx, origin.y), ImVec2(cx, pMax.y),
                            IM_COL32(255, 60, 60, 240), 2.f);
                int m = (int)pos / 60, s = (int)pos % 60, ms = (int)(fmod(pos, 1.0) * 100);
                char tsBuf[24];
                snprintf(tsBuf, sizeof(tsBuf), "%d:%02d.%02d", m, s, ms);
                dl->AddText(ImVec2(cx + 4, origin.y + 2),
                            IM_COL32(255, 100, 100, 240), tsBuf);
            }
        }
    }

    // Duration label
    if (m_waveformLoaded) {
        int dm = (int)m_waveform.durationSeconds / 60;
        int ds = (int)m_waveform.durationSeconds % 60;
        char durBuf[24];
        snprintf(durBuf, sizeof(durBuf), "/ %d:%02d", dm, ds);
        ImVec2 dsz = ImGui::CalcTextSize(durBuf);
        dl->AddText(ImVec2(pMax.x - dsz.x - 4, pMax.y - dsz.y - 4),
                    IM_COL32(120, 120, 160, 180), durBuf);
    }

    // Raw mouse-in-rect check
    ImVec2 wfMpos = ImGui::GetIO().MousePos;
    bool mouseInWaveform = (wfMpos.x >= origin.x && wfMpos.x <= pMax.x &&
                            wfMpos.y >= origin.y && wfMpos.y <= pMax.y);

    // Hover cursor line + time label + click to place markers
    if (mouseInWaveform) {
        float mx = wfMpos.x;
        float hoverT = m_timelineScrollX + (mx - origin.x) / m_timelineZoom;
        m_hoverTime = hoverT;

        dl->AddLine(ImVec2(mx, origin.y), ImVec2(mx, pMax.y),
                    IM_COL32(255, 255, 255, 120), 1.f);
        int hm = (int)hoverT / 60, hs = (int)hoverT % 60;
        int hms = (int)(fmodf(hoverT, 1.f) * 100);
        char hBuf[24];
        snprintf(hBuf, sizeof(hBuf), "%d:%02d.%02d", hm, hs, hms);
        dl->AddText(ImVec2(mx + 6, origin.y + 2),
                    IM_COL32(255, 255, 255, 200), hBuf);

        // Left-click: place marker
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseClicked[0] && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt) {
            markers().push_back(hoverT);
        }
        // Right-click: remove nearest marker
        if (io.MouseClicked[1] && !markers().empty()) {
            float threshold = 5.f / m_timelineZoom;
            int bestIdx = -1;
            float bestDist = threshold;
            for (int i = 0; i < (int)markers().size(); i++) {
                float d = fabsf(markers()[i] - hoverT);
                if (d < bestDist) { bestDist = d; bestIdx = i; }
            }
            if (bestIdx >= 0)
                markers().erase(markers().begin() + bestIdx);
        }
    }

    // Scroll/zoom: wheel = scroll, Ctrl+wheel = zoom
    if (mouseInWaveform) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) {
            if (ImGui::GetIO().KeyCtrl) {
                float oldZoom = m_timelineZoom;
                m_timelineZoom *= (1.f + wheel * 0.15f);
                if (m_timelineZoom < 10.f)    m_timelineZoom = 10.f;
                if (m_timelineZoom > 2000.f)  m_timelineZoom = 2000.f;
                float mouseT = m_timelineScrollX + (ImGui::GetIO().MousePos.x - origin.x) / oldZoom;
                m_timelineScrollX = mouseT - (ImGui::GetIO().MousePos.x - origin.x) / m_timelineZoom;
            } else {
                m_timelineScrollX -= wheel * 0.5f;
            }
            if (m_timelineScrollX < 0.f) m_timelineScrollX = 0.f;
            float maxScroll = (float)m_waveform.durationSeconds;
            if (m_timelineScrollX > maxScroll) m_timelineScrollX = maxScroll;
        }
    }

    dl->PopClipRect();
}

// ── renderSceneView ─────────────────────────────────────────────────────────
// Shows a static game-scene snapshot at the current cursor time.
// Notes within a visible time window are drawn on the highway / circle / screen.

void SongEditor::renderSceneView(ImDrawList* dl, ImVec2 origin, ImVec2 size,
                                 Engine* engine) {
    if (!m_song) return;

    const GameModeConfig& gm = m_song->gameMode;
    const auto& curNotes = notes();

    ImVec2 pMax = ImVec2(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, pMax, true);
    dl->AddRectFilled(origin, pMax, IM_COL32(15, 15, 25, 255));

    // Draw background image if set
    if (!gm.backgroundImage.empty()) {
        VkDescriptorSet bgTex = getThumb(gm.backgroundImage);
        if (bgTex) {
            dl->AddImage((ImTextureID)(uint64_t)bgTex, origin, pMax);
            // Dim overlay so notes remain visible
            dl->AddRectFilled(origin, pMax, IM_COL32(0, 0, 0, 100));
        }
    }

    int tc = gm.trackCount;

    // Determine the "current time" from playback or last cursor position
    float curTime = m_sceneTime;
    if (engine && engine->audio().isPlaying()) {
        double pos = engine->audio().positionSeconds();
        if (pos >= 0) curTime = (float)pos;
    }

    // How far ahead (in seconds) notes are visible before reaching the judge line
    const float lookAhead = 3.0f;

    // ── Helper: note-type color ─────────────────────────────────────────────
    auto noteColor = [](EditorNoteType t) -> ImU32 {
        switch (t) {
            case EditorNoteType::Tap: return IM_COL32(100, 180, 255, 230);
            case EditorNoteType::Hold: return IM_COL32(80, 220, 100, 230);
            case EditorNoteType::Slide: return IM_COL32(220, 130, 255, 230);
        }
        return IM_COL32(100, 180, 255, 230);
    };
    auto noteColorDim = [](EditorNoteType t) -> ImU32 {
        switch (t) {
            case EditorNoteType::Tap: return IM_COL32(60, 110, 160, 160);
            case EditorNoteType::Hold: return IM_COL32(50, 140, 60, 160);
            case EditorNoteType::Slide: return IM_COL32(140, 80, 160, 160);
        }
        return IM_COL32(60, 110, 160, 160);
    };

    if (gm.type == GameModeType::DropNotes) {
        // ── Camera-based perspective highway ────────────────────────────────
        // Build a real perspective VP matrix from the camera config so the
        // preview updates when the user adjusts Eye / Target / FOV.
        float aspect = size.x / std::max(size.y, 1.f);
        glm::vec3 camEye   {gm.cameraEye[0],    gm.cameraEye[1],    gm.cameraEye[2]};
        glm::vec3 camTarget{gm.cameraTarget[0],  gm.cameraTarget[1], gm.cameraTarget[2]};
        float     camFov = glm::radians(std::clamp(gm.cameraFov, 20.f, 120.f));
        glm::mat4 proj = glm::perspective(camFov, aspect, 0.1f, 300.f);
        // Flip Y for screen coords (top=0)
        proj[1][1] *= -1.f;
        glm::mat4 view = glm::lookAt(camEye, camTarget, glm::vec3(0.f, 1.f, 0.f));
        glm::mat4 vp = proj * view;

        constexpr float HIT_ZONE_Z  = 0.f;
        constexpr float APPROACH_Z  = -55.f;

        auto w2s = [&](glm::vec3 pos) -> ImVec2 {
            glm::vec4 clip = vp * glm::vec4(pos, 1.f);
            if (clip.w <= 0.f) return ImVec2(-9999.f, -9999.f);
            float ndcX = clip.x / clip.w;
            float ndcY = clip.y / clip.w;
            return ImVec2(origin.x + (ndcX * 0.5f + 0.5f) * size.x,
                          origin.y + (ndcY * 0.5f + 0.5f) * size.y);
        };
        constexpr float SCROLL_SPEED = 14.f;
        float laneSpacing = 1.2f;
        {
            ImVec2 lt = w2s({-1.f, 0.f, HIT_ZONE_Z});
            ImVec2 rt = w2s({ 1.f, 0.f, HIT_ZONE_Z});
            float pxPerUnit = (rt.x - lt.x) * 0.5f;
            if (pxPerUnit > 0.f) {
                float desiredPx = size.x * 0.30f;
                laneSpacing = desiredPx / pxPerUnit / tc;
            }
        }

        bool is3D = (gm.dimension == DropDimension::ThreeD);

        // Draw lane dividers from approach to hit zone
        for (int i = 0; i <= tc; i++) {
            float wx = (i - tc * 0.5f) * laneSpacing;
            ImVec2 nearPt = w2s({wx, 0.f, HIT_ZONE_Z});
            ImVec2 farPt  = w2s({wx, 0.f, APPROACH_Z});
            dl->AddLine(nearPt, farPt, IM_COL32(60, 60, 100, 200));
        }
        // Highway borders (thicker)
        {
            float leftX  = -(tc * 0.5f) * laneSpacing;
            float rightX =  (tc * 0.5f) * laneSpacing;
            ImVec2 nl = w2s({leftX,  0.f, HIT_ZONE_Z});
            ImVec2 nr = w2s({rightX, 0.f, HIT_ZONE_Z});
            ImVec2 fl = w2s({leftX,  0.f, APPROACH_Z});
            ImVec2 fr = w2s({rightX, 0.f, APPROACH_Z});
            dl->AddLine(nl, fl, IM_COL32(100, 100, 160, 255), 1.5f);
            dl->AddLine(nr, fr, IM_COL32(100, 100, 160, 255), 1.5f);
        }

        // Ground judge line
        {
            float leftX  = -(tc * 0.5f) * laneSpacing;
            float rightX =  (tc * 0.5f) * laneSpacing;
            ImVec2 l = w2s({leftX,  0.f, HIT_ZONE_Z});
            ImVec2 r = w2s({rightX, 0.f, HIT_ZONE_Z});
            dl->AddLine(l, r, IM_COL32(255, 200, 60, 255), 2.5f);
        }

        // 3D: sky judge line (elevated hit zone)
        constexpr float SKY_Y = 3.0f;
        if (is3D) {
            float leftX  = -(tc * 0.5f) * laneSpacing;
            float rightX =  (tc * 0.5f) * laneSpacing;
            ImVec2 sl = w2s({leftX,  SKY_Y, HIT_ZONE_Z});
            ImVec2 sr = w2s({rightX, SKY_Y, HIT_ZONE_Z});
            dl->AddLine(sl, sr, IM_COL32(220, 100, 255, 200), 2.f);
            // Vertical connectors from sky to ground
            ImVec2 gl = w2s({leftX,  0.f, HIT_ZONE_Z});
            ImVec2 gr = w2s({rightX, 0.f, HIT_ZONE_Z});
            dl->AddLine(sl, gl, IM_COL32(80, 60, 120, 100), 1.f);
            dl->AddLine(sr, gr, IM_COL32(80, 60, 120, 100), 1.f);
        }

        // Draw actual notes on the highway
        for (const auto& note : curNotes) {
            float dt = note.time - curTime;
            if (dt < -0.2f || dt > lookAhead) continue;

            bool isSky = is3D && note.isSky;
            float noteZ = -dt * SCROLL_SPEED;
            float worldX = (note.track - (tc - 1) * 0.5f) * laneSpacing;
            float worldY = isSky ? SKY_Y : 0.f;
            glm::vec3 worldPos{worldX, worldY, noteZ};

            glm::vec4 clip = vp * glm::vec4(worldPos, 1.f);
            if (clip.w <= 0.f) continue;
            ImVec2 screen = w2s(worldPos);

            // Perspective-correct note size — full lane width
            float proj11 = std::abs(proj[1][1]);
            float nw = laneSpacing * proj11 * size.y * 0.5f / clip.w;
            if (nw < 2.f) continue;
            float nh = nw * 0.3f;

            if (note.type == EditorNoteType::Hold && note.endTime > note.time) {
                float dt2 = note.endTime - curTime;
                float noteZ2 = -dt2 * SCROLL_SPEED;
                glm::vec3 endPos{worldX, worldY, noteZ2};
                glm::vec4 clip2 = vp * glm::vec4(endPos, 1.f);
                if (clip2.w > 0.f) {
                    ImVec2 screen2 = w2s(endPos);
                    float nw2 = laneSpacing * proj11 * size.y * 0.5f / clip2.w;
                    float hw1 = nw * 0.4f, hw2 = nw2 * 0.4f;
                    dl->AddQuadFilled(
                        ImVec2(screen.x - hw1, screen.y), ImVec2(screen.x + hw1, screen.y),
                        ImVec2(screen2.x + hw2, screen2.y), ImVec2(screen2.x - hw2, screen2.y),
                        noteColorDim(note.type));
                }
            }

            dl->AddRectFilled(ImVec2(screen.x - nw / 2, screen.y - nh / 2),
                              ImVec2(screen.x + nw / 2, screen.y + nh / 2),
                              noteColor(note.type), 2.f);
        }

        const char* label = is3D ? "3D Drop Notes" : "2D Drop Notes";
        dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                    IM_COL32(200, 200, 200, 180), label);

    } else if (gm.type == GameModeType::Circle) {
        // ── Circle mode ─────────────────────────────────────────────────────
        float centerX = origin.x + size.x * 0.5f;
        float centerY = origin.y + size.y * 0.52f;
        float minDim = (size.x < size.y) ? size.x : size.y;
        float outerR = minDim * 0.44f;
        float innerR = outerR * 0.28f;

        // Draw rings and radial lines
        dl->AddCircle(ImVec2(centerX, centerY), outerR,
                      IM_COL32(180, 140, 60, 220), 64, 2.5f);
        dl->AddCircle(ImVec2(centerX, centerY), innerR,
                      IM_COL32(140, 120, 80, 160), 48, 1.5f);
        dl->AddCircleFilled(ImVec2(centerX, centerY), innerR * 0.3f,
                            IM_COL32(100, 80, 50, 120));

        float angleStep = 6.2831853f / tc;
        for (int i = 0; i < tc; i++) {
            float angle = i * angleStep - 1.5707963f;
            dl->AddLine(ImVec2(centerX + innerR * cosf(angle), centerY + innerR * sinf(angle)),
                        ImVec2(centerX + outerR * cosf(angle), centerY + outerR * sinf(angle)),
                        IM_COL32(80, 70, 50, 120), 1.f);
        }

        // Draw actual notes as trapezoids at their radial position
        float noteThick = 0.06f;
        for (const auto& note : curNotes) {
            float dt = note.time - curTime;
            if (dt < -0.2f || dt > lookAhead) continue;

            // frac: 0 = at outer ring (judge), 1 = at inner ring (spawn)
            float frac = std::clamp(dt / lookAhead, 0.f, 1.f);
            float rInner2 = outerR - frac * (outerR - innerR);
            float rOuter2 = rInner2 + noteThick * (outerR - innerR);

            int track = note.track % tc;
            float aL = track * angleStep - 1.5707963f;
            float aR = (track + 1) * angleStep - 1.5707963f;

            ImVec2 quad[4] = {
                ImVec2(centerX + rInner2 * cosf(aL), centerY + rInner2 * sinf(aL)),
                ImVec2(centerX + rInner2 * cosf(aR), centerY + rInner2 * sinf(aR)),
                ImVec2(centerX + rOuter2 * cosf(aR), centerY + rOuter2 * sinf(aR)),
                ImVec2(centerX + rOuter2 * cosf(aL), centerY + rOuter2 * sinf(aL)),
            };
            dl->AddQuadFilled(quad[0], quad[1], quad[2], quad[3], noteColor(note.type));
            dl->AddQuad(quad[0], quad[1], quad[2], quad[3],
                        IM_COL32(255, 255, 255, 120), 1.2f);
        }

        dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                    IM_COL32(200, 200, 200, 180), "Circle Mode");

    } else {
        // ── Scan Line mode ──────────────────────────────────────────────────
        // Scan line bounces top<->bottom; position based on curTime
        float period = 4.f;
        float phase = fmodf(curTime, period) / period;
        float scanFrac = (phase < 0.5f) ? (phase * 2.f) : (2.f - phase * 2.f);
        float scanY = origin.y + scanFrac * size.y;

        dl->AddLine(ImVec2(origin.x + 10, scanY), ImVec2(pMax.x - 10, scanY),
                    IM_COL32(255, 255, 255, 200), 2.f);
        dl->AddLine(ImVec2(origin.x + 10, scanY), ImVec2(pMax.x - 10, scanY),
                    IM_COL32(0, 200, 255, 60), 6.f);

        float colW = size.x / tc;
        float noteR = std::clamp(colW * 0.18f, 5.f, 16.f);

        // Draw actual notes at their screen positions
        for (const auto& note : curNotes) {
            float dt = fabsf(note.time - curTime);
            if (dt > lookAhead) continue;

            int track = note.track % tc;
            float cx = origin.x + (track + 0.5f) * colW;

            // Y position: notes have a fixed Y based on their time within the scan cycle
            float nPhase = fmodf(note.time, period) / period;
            float nFrac  = (nPhase < 0.5f) ? (nPhase * 2.f) : (2.f - nPhase * 2.f);
            float cy = origin.y + nFrac * size.y;

            float dist = fabsf(cy - scanY);
            bool active = dist < size.y * 0.08f;
            ImU32 col = active ? noteColor(note.type) : noteColorDim(note.type);

            dl->AddCircleFilled(ImVec2(cx, cy), noteR, col);
            if (active) {
                dl->AddCircle(ImVec2(cx, cy), noteR + 5.f,
                              IM_COL32(255, 255, 255, 120), 0, 2.f);
            }
        }

        dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                    IM_COL32(200, 200, 200, 180), "Scan Line Mode");
    }

    // ── Simulate score & combo at curTime ──────────────────────────────────
    int simScore = 0;
    int simCombo = 0;
    {
        // Count notes already passed — assume all Perfect for preview
        for (const auto& n : curNotes) {
            if (n.time <= curTime) {
                simScore += gm.perfectScore;
                simCombo++;
            }
        }
    }

    // ── HUD text rendering helper (logo-style) ─────────────────────────────
    auto drawHudText = [&](const HudTextConfig& h, const char* text) {
        if (!text || text[0] == '\0') return;
        float fx = origin.x + size.x * h.pos[0];
        float fy = origin.y + size.y * h.pos[1];
        float fs = h.fontSize * h.scale;
        ImU32 col = IM_COL32((int)(h.color[0]*255), (int)(h.color[1]*255),
                             (int)(h.color[2]*255), (int)(h.color[3]*255));
        ImFont* font = ImGui::GetFont();
        ImVec2 textSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
        ImVec2 textPos(fx - textSz.x * 0.5f, fy - textSz.y * 0.5f);

        if (h.glow) {
            ImU32 gc = IM_COL32((int)(h.glowColor[0]*255), (int)(h.glowColor[1]*255),
                                (int)(h.glowColor[2]*255), (int)(h.glowColor[3]*255));
            float r = h.glowRadius;
            float offsets[][2] = {{-r,0},{r,0},{0,-r},{0,r},{-r*0.7f,-r*0.7f},
                                  {r*0.7f,-r*0.7f},{-r*0.7f,r*0.7f},{r*0.7f,r*0.7f}};
            for (auto& o : offsets)
                dl->AddText(font, fs, ImVec2(textPos.x+o[0], textPos.y+o[1]), gc, text);
        }
        if (h.bold)
            dl->AddText(font, fs, ImVec2(textPos.x + 1.f, textPos.y), col, text);
        dl->AddText(font, fs, textPos, col, text);
    };

    // ── Draw Score HUD ──────────────────────────────────────────────────────
    {
        char scoreBuf[32];
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", simScore);
        drawHudText(gm.scoreHud, scoreBuf);
    }

    // ── Draw Combo HUD ──────────────────────────────────────────────────────
    {
        char comboBuf[32];
        snprintf(comboBuf, sizeof(comboBuf), "%d", simCombo);
        drawHudText(gm.comboHud, comboBuf);

        // "COMBO" label below the number
        HudTextConfig comboLabel = gm.comboHud;
        comboLabel.pos[1] += comboLabel.fontSize * comboLabel.scale / size.y * 1.2f;
        comboLabel.fontSize *= 0.45f;
        drawHudText(comboLabel, "COMBO");
    }

    // ── Timestamp overlay ───────────────────────────────────────────────────
    {
        int m = (int)curTime / 60, s = (int)curTime % 60;
        int ms = (int)(fmodf(curTime, 1.f) * 100);
        char tsBuf[32];
        snprintf(tsBuf, sizeof(tsBuf), "%d:%02d.%02d", m, s, ms);
        ImVec2 tsz = ImGui::CalcTextSize(tsBuf);
        dl->AddText(ImVec2(pMax.x - tsz.x - 8, origin.y + 6),
                    IM_COL32(255, 200, 60, 200), tsBuf);
    }

    // Show hint if no notes
    if (curNotes.empty()) {
        const char* hint = "No notes — switch to Editor tab to place notes";
        ImVec2 tsz = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(origin.x + (size.x - tsz.x) * 0.5f,
                           pMax.y - tsz.y - 12),
                    IM_COL32(100, 100, 120, 160), hint);
    }

    dl->PopClipRect();
}

// ── renderGameModePreview ───────────────────────────────────────────────────

void SongEditor::renderGameModePreview(ImDrawList* dl, ImVec2 origin, ImVec2 size) {
    if (!m_song) return;
    const GameModeConfig& gm = m_song->gameMode;

    ImVec2 pMax = ImVec2(origin.x + size.x, origin.y + size.y);
    dl->PushClipRect(origin, pMax, true);

    // Dark background
    dl->AddRectFilled(origin, pMax, IM_COL32(15, 15, 25, 255));

    int tc = gm.trackCount;

    if (gm.type == GameModeType::DropNotes) {
        // Both 2D and 3D use a perspective highway with converging lanes
        float vpX  = origin.x + size.x * 0.5f;
        float vpY  = origin.y + size.y * 0.12f;
        float baseY = origin.y + size.y * 0.9f;
        float baseL = origin.x + size.x * 0.08f;
        float baseR = origin.x + size.x * 0.92f;
        float baseW = baseR - baseL;

        // Lane dividers converging to vanishing point
        for (int i = 0; i <= tc; i++) {
            float t = (float)i / tc;
            float bx = baseL + t * baseW;
            dl->AddLine(ImVec2(vpX, vpY), ImVec2(bx, baseY),
                        IM_COL32(60, 60, 100, 200));
        }
        // Highway borders
        dl->AddLine(ImVec2(vpX, vpY), ImVec2(baseL, baseY),
                    IM_COL32(100, 100, 160, 255), 1.5f);
        dl->AddLine(ImVec2(vpX, vpY), ImVec2(baseR, baseY),
                    IM_COL32(100, 100, 160, 255), 1.5f);

        // Ground judge line (both 2D and 3D have this)
        float groundFrac = 0.85f;
        float groundY = vpY + (baseY - vpY) * groundFrac;
        float groundSpread = baseW * groundFrac;
        float groundL = vpX - groundSpread * 0.5f;
        float groundR = vpX + groundSpread * 0.5f;
        dl->AddLine(ImVec2(groundL, groundY), ImVec2(groundR, groundY),
                    IM_COL32(255, 200, 60, 255), 2.5f);

        // Ground notes
        float gNoteFracs[] = {0.35f, 0.5f, 0.65f, 0.75f};
        int   gNoteLanes[] = {1, tc / 2, tc - 2, 0};
        for (int i = 0; i < 4 && gNoteLanes[i] < tc; i++) {
            float f = gNoteFracs[i];
            float ny = vpY + (baseY - vpY) * f;
            float spread = baseW * f;
            float lineL = vpX - spread * 0.5f;
            float lw = spread / tc;
            float nx = lineL + (gNoteLanes[i] + 0.5f) * lw;
            float nw = lw * 0.6f;
            float nh = 6.f;
            dl->AddRectFilled(ImVec2(nx - nw / 2, ny - nh / 2),
                              ImVec2(nx + nw / 2, ny + nh / 2),
                              IM_COL32(100, 180, 255, 200), 2.f);
        }

        if (gm.dimension == DropDimension::TwoD) {
            dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                        IM_COL32(200, 200, 200, 180), "2D Drop Notes");
        } else {
            // ── 3D: Sky input line above ground ──
            // Sky judge line at a higher position (like Arcaea's "Sky Input")
            float skyFrac = 0.55f;
            float skyY = vpY + (baseY - vpY) * skyFrac;
            float skySpread = baseW * skyFrac;
            float skyL = vpX - skySpread * 0.5f;
            float skyR = vpX + skySpread * 0.5f;
            dl->AddLine(ImVec2(skyL, skyY), ImVec2(skyR, skyY),
                        IM_COL32(220, 100, 255, 200), 2.f);

            // "Sky Input" label near the sky line
            dl->AddText(ImVec2(skyR + 4, skyY - 8),
                        IM_COL32(220, 100, 255, 160), "Sky Input");

            // Vertical connectors showing the two levels
            dl->AddLine(ImVec2(skyL, skyY), ImVec2(groundL, groundY),
                        IM_COL32(80, 60, 120, 100), 1.f);
            dl->AddLine(ImVec2(skyR, skyY), ImVec2(groundR, groundY),
                        IM_COL32(80, 60, 120, 100), 1.f);

            // Sky notes (floating above the ground track)
            float sNoteFracs[] = {0.25f, 0.38f, 0.48f};
            int   sNoteLanes[] = {tc / 2, 1, tc - 2};
            for (int i = 0; i < 3 && sNoteLanes[i] < tc; i++) {
                float f = sNoteFracs[i];
                float ny = vpY + (baseY - vpY) * f;
                // Sky notes are elevated: draw them between sky and vanishing point
                float elevY = ny - (groundY - skyY) * 0.35f;
                float spread = baseW * f;
                float lineL2 = vpX - spread * 0.5f;
                float lw = spread / tc;
                float nx = lineL2 + (sNoteLanes[i] + 0.5f) * lw;
                float nw = lw * 0.5f;
                float nh = 5.f;
                dl->AddRectFilled(ImVec2(nx - nw / 2, elevY - nh / 2),
                                  ImVec2(nx + nw / 2, elevY + nh / 2),
                                  IM_COL32(220, 130, 255, 200), 2.f);
            }

            dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                        IM_COL32(200, 200, 200, 180), "3D Drop Notes");
        }

    } else if (gm.type == GameModeType::Circle) {
        // ── Circle mode (Lanota-style: outer hit circle + inner spawn circle + radial tracks) ──
        float centerX = origin.x + size.x * 0.5f;
        float centerY = origin.y + size.y * 0.52f;
        float minDim = (size.x < size.y) ? size.x : size.y;
        float outerR = minDim * 0.44f;
        float innerR = outerR * 0.28f;

        // Outer circle (hit zone)
        dl->AddCircle(ImVec2(centerX, centerY), outerR,
                      IM_COL32(180, 140, 60, 220), 64, 2.5f);
        dl->AddCircle(ImVec2(centerX, centerY), outerR - 2,
                      IM_COL32(180, 140, 60, 40), 64, 4.f);

        // Inner circle (spawn zone)
        dl->AddCircle(ImVec2(centerX, centerY), innerR,
                      IM_COL32(140, 120, 80, 160), 48, 1.5f);
        dl->AddCircleFilled(ImVec2(centerX, centerY), innerR * 0.3f,
                            IM_COL32(100, 80, 50, 120));

        // Radial track lines (divide circle into sectors)
        float angleStep = 6.2831853f / tc;
        for (int i = 0; i < tc; i++) {
            float angle = i * angleStep - 1.5707963f;
            float ix = centerX + innerR * cosf(angle);
            float iy = centerY + innerR * sinf(angle);
            float ox = centerX + outerR * cosf(angle);
            float oy = centerY + outerR * sinf(angle);
            dl->AddLine(ImVec2(ix, iy), ImVec2(ox, oy),
                        IM_COL32(80, 70, 50, 120), 1.f);
        }

        // Sample notes: trapezoids spanning full sector width between divider lines
        float noteFracs[] = {0.45f, 0.72f, 0.55f, 0.85f, 0.35f, 0.6f,
                             0.75f, 0.5f, 0.65f, 0.4f, 0.8f, 0.55f};
        float noteThick = 0.06f; // radial thickness as fraction of (outerR - innerR)
        int noteCount = tc < 5 ? tc : 5;
        for (int i = 0; i < noteCount; i++) {
            float frac = noteFracs[i % 12];
            float rInner = innerR + frac * (outerR - innerR);
            float rOuter = rInner + noteThick * (outerR - innerR);

            // Left edge = line i, right edge = line i+1
            float aL = i * angleStep - 1.5707963f;
            float aR = (i + 1) * angleStep - 1.5707963f;

            // 4 corners of the trapezoid (inner-left, inner-right, outer-right, outer-left)
            ImVec2 quad[4] = {
                ImVec2(centerX + rInner * cosf(aL), centerY + rInner * sinf(aL)),
                ImVec2(centerX + rInner * cosf(aR), centerY + rInner * sinf(aR)),
                ImVec2(centerX + rOuter * cosf(aR), centerY + rOuter * sinf(aR)),
                ImVec2(centerX + rOuter * cosf(aL), centerY + rOuter * sinf(aL)),
            };
            dl->AddQuadFilled(quad[0], quad[1], quad[2], quad[3],
                              IM_COL32(80, 180, 255, 200));
            dl->AddQuad(quad[0], quad[1], quad[2], quad[3],
                        IM_COL32(120, 210, 255, 255), 1.2f);
        }

        dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                    IM_COL32(200, 200, 200, 180), "Circle Mode");

    } else {
        // ── Scan Line mode (Cytus-style) ──
        // Horizontal scan line sweeping vertically
        float scanY = origin.y + size.y * 0.45f;
        dl->AddLine(ImVec2(origin.x + 10, scanY), ImVec2(pMax.x - 10, scanY),
                    IM_COL32(255, 255, 255, 200), 2.f);
        // Glow effect on scan line
        dl->AddLine(ImVec2(origin.x + 10, scanY), ImVec2(pMax.x - 10, scanY),
                    IM_COL32(0, 200, 255, 60), 6.f);

        // Notes scattered across the screen in trackCount columns
        float colW = size.x / tc;
        float noteR = colW * 0.18f;
        if (noteR > 16.f) noteR = 16.f;
        if (noteR < 5.f)  noteR = 5.f;

        // Note positions: some above scan line, some below, some on it
        float yPositions[] = {0.2f, 0.7f, 0.35f, 0.6f, 0.15f, 0.8f,
                              0.45f, 0.55f, 0.25f, 0.75f, 0.3f, 0.65f};
        for (int i = 0; i < tc; i++) {
            float cx = origin.x + (i + 0.5f) * colW;
            float cy = origin.y + yPositions[i % 12] * size.y;

            // Notes near the scan line are "active" (brighter)
            float dist = fabsf(cy - scanY);
            bool active = dist < size.y * 0.08f;

            if (active) {
                // Active note: filled bright circle with ring
                dl->AddCircleFilled(ImVec2(cx, cy), noteR,
                                    IM_COL32(100, 220, 255, 240));
                dl->AddCircle(ImVec2(cx, cy), noteR + 5.f,
                              IM_COL32(100, 220, 255, 120), 0, 2.f);
            } else {
                // Inactive note: outlined, dimmer
                dl->AddCircle(ImVec2(cx, cy), noteR,
                              IM_COL32(100, 180, 255, 140), 0, 1.5f);
                dl->AddCircleFilled(ImVec2(cx, cy), noteR * 0.4f,
                                    IM_COL32(100, 180, 255, 100));
            }
        }

        // Arrow indicators showing scan direction
        float arrowX = pMax.x - 20;
        dl->AddTriangleFilled(
            ImVec2(arrowX, scanY - 14),
            ImVec2(arrowX - 6, scanY - 6),
            ImVec2(arrowX + 6, scanY - 6),
            IM_COL32(255, 255, 255, 100));
        dl->AddTriangleFilled(
            ImVec2(arrowX, scanY + 14),
            ImVec2(arrowX - 6, scanY + 6),
            ImVec2(arrowX + 6, scanY + 6),
            IM_COL32(255, 255, 255, 100));

        dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                    IM_COL32(200, 200, 200, 180), "Scan Line Mode (Cytus)");
    }

    dl->PopClipRect();
}

// ── renderAssets ─────────────────────────────────────────────────────────────

void SongEditor::renderAssets() {
    if (ImGui::Button("Open File...")) {
#ifdef _WIN32
        OPENFILENAMEW ofn = {};
        wchar_t szFile[4096] = {};
        ofn.lStructSize  = sizeof(ofn);
        ofn.hwndOwner    = m_window ? glfwGetWin32Window(m_window) : nullptr;
        ofn.lpstrFile    = szFile;
        ofn.nMaxFile     = static_cast<DWORD>(sizeof(szFile) / sizeof(wchar_t));
        ofn.lpstrFilter  = L"Audio\0*.mp3;*.ogg;*.wav;*.flac;*.aac\0"
                           L"Images\0*.png;*.jpg;*.jpeg\0"
                           L"Charts\0*.json;*.chart;*.ucf\0"
                           L"All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST |
                           OFN_ALLOWMULTISELECT | OFN_EXPLORER;
        if (GetOpenFileNameW(&ofn)) {
            std::vector<std::string> paths;
            wchar_t* p = szFile;
            std::wstring dir = p;
            p += dir.size() + 1;
            if (*p == L'\0') {
                int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0, nullptr, nullptr);
                std::string path(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, path.data(), len, nullptr, nullptr);
                paths.push_back(std::move(path));
            } else {
                while (*p) {
                    std::wstring fname = p;
                    std::wstring full  = dir + L"\\" + fname;
                    int len = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, nullptr, 0, nullptr, nullptr);
                    std::string path(len - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, path.data(), len, nullptr, nullptr);
                    paths.push_back(std::move(path));
                    p += fname.size() + 1;
                }
            }
            if (!paths.empty()) importFiles(paths);
        }
#endif
    }
    ImGui::Separator();

    if (!m_assetsScanned) { ImGui::TextDisabled("(not scanned)"); return; }

    bool anyFiles = !m_assets.images.empty() || !m_assets.gifs.empty() ||
                    !m_assets.videos.empty() || !m_assets.audios.empty();
    if (!anyFiles) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p  = ImGui::GetCursorScreenPos();
        ImVec2 sz = ImGui::GetContentRegionAvail();
        dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y - 4),
                    IM_COL32(120, 120, 120, 100), 4.f, 0, 1.5f);
        const char* hint = "Drop files here, or click Open File...";
        ImVec2 tsz = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(p.x + sz.x * 0.5f - tsz.x * 0.5f,
                           p.y + sz.y * 0.5f - tsz.y * 0.5f),
                    IM_COL32(150, 150, 150, 200), hint);
        return;
    }

    const float thumbSize = 80.f;
    std::string toDelete;

    auto drawThumbs = [&](const std::vector<std::string>& files) {
        if (files.empty()) return;
        for (int i = 0; i < (int)files.size(); ++i) {
            const std::string& f = files[i];
            std::string name = fs::path(f).filename().string();
            ImGui::PushID(i);
            ImGui::BeginGroup();

            ImVec2 thumbPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##t", ImVec2(thumbSize, thumbSize));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            VkDescriptorSet thumb = getThumb(f);
            if (thumb) {
                dl->AddImage((ImTextureID)(uint64_t)thumb, thumbPos,
                             ImVec2(thumbPos.x + thumbSize, thumbPos.y + thumbSize));
            } else {
                dl->AddRectFilled(thumbPos, ImVec2(thumbPos.x + thumbSize, thumbPos.y + thumbSize),
                                  IM_COL32(50, 50, 70, 255), 4.f);
                ImVec2 isz = ImGui::CalcTextSize("...");
                dl->AddText(ImVec2(thumbPos.x + thumbSize * 0.5f - isz.x * 0.5f,
                                   thumbPos.y + thumbSize * 0.5f - isz.y * 0.5f),
                            IM_COL32(160, 160, 180, 200), "...");
            }
            if (ImGui::IsItemHovered()) {
                dl->AddRect(thumbPos, ImVec2(thumbPos.x + thumbSize, thumbPos.y + thumbSize),
                            IM_COL32(100, 160, 255, 200), 4.f, 0, 2.f);
                ImGui::SetTooltip("%s", f.c_str());
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ASSET_PATH", f.c_str(), f.size() + 1);
                if (thumb) ImGui::Image((ImTextureID)(uint64_t)thumb, ImVec2(60.f, 60.f));
                else       ImGui::Text("%s", name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem("##ctx")) {
                if (ImGui::MenuItem("Delete")) toDelete = f;
                ImGui::EndPopup();
            }

            std::string lbl = name.size() > 11 ? name.substr(0, 9) + ".." : name;
            ImVec2 lsz = ImGui::CalcTextSize(lbl.c_str());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (thumbSize - lsz.x) * 0.5f);
            ImGui::TextDisabled("%s", lbl.c_str());

            ImGui::EndGroup();
            ImGui::SameLine(0.f, 6.f);
            ImGui::PopID();
        }
        ImGui::NewLine();
    };

    if (!m_assets.images.empty()) { ImGui::Text("Images:"); drawThumbs(m_assets.images); }

    // Audio
    if (!m_assets.audios.empty()) {
        ImGui::Text("Audio:");
        for (int i = 0; i < (int)m_assets.audios.size(); ++i) {
            const std::string& f = m_assets.audios[i];
            std::string name = fs::path(f).filename().string();
            ImGui::PushID(1000 + i);
            ImGui::BeginGroup();
            ImVec2 thumbPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##a", ImVec2(thumbSize, thumbSize));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(thumbPos, ImVec2(thumbPos.x + thumbSize, thumbPos.y + thumbSize),
                              IM_COL32(30, 40, 60, 255), 4.f);
            const char* icon = "MUS";
            ImVec2 aisz = ImGui::CalcTextSize(icon);
            dl->AddText(ImVec2(thumbPos.x + thumbSize * 0.5f - aisz.x * 0.5f,
                               thumbPos.y + thumbSize * 0.5f - aisz.y * 0.5f),
                        IM_COL32(100, 180, 255, 220), icon);
            if (ImGui::IsItemHovered()) {
                dl->AddRect(thumbPos, ImVec2(thumbPos.x + thumbSize, thumbPos.y + thumbSize),
                            IM_COL32(100, 160, 255, 200), 4.f, 0, 2.f);
                ImGui::SetTooltip("%s", f.c_str());
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ASSET_PATH", f.c_str(), f.size() + 1);
                ImGui::Text("%s", name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem("##actx")) {
                if (ImGui::MenuItem("Delete")) toDelete = f;
                ImGui::EndPopup();
            }
            std::string lbl = name.size() > 11 ? name.substr(0, 9) + ".." : name;
            ImVec2 lsz = ImGui::CalcTextSize(lbl.c_str());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (thumbSize - lsz.x) * 0.5f);
            ImGui::TextDisabled("%s", lbl.c_str());
            ImGui::EndGroup();
            ImGui::SameLine(0.f, 6.f);
            ImGui::PopID();
        }
        ImGui::NewLine();
    }

    // Handle deletion
    if (!toDelete.empty()) {
        std::string fullPath = m_projectPath + "/" + toDelete;
        try { fs::remove(fullPath); } catch (...) {}

        auto it = m_thumbCache.find(toDelete);
        if (it != m_thumbCache.end()) {
            if (it->second.tex.image != VK_NULL_HANDLE && m_ctx && m_bufMgr) {
                vkDestroySampler(m_ctx->device(), it->second.tex.sampler, nullptr);
                vkDestroyImageView(m_ctx->device(), it->second.tex.view, nullptr);
                vmaDestroyImage(m_bufMgr->allocator(), it->second.tex.image, it->second.tex.allocation);
            }
            m_thumbCache.erase(it);
        }
        m_assetsScanned = false;
    }
}

// ── buildChartFromNotes ──────────────────────────────────────────────────────

ChartData SongEditor::buildChartFromNotes() const {
    ChartData chart;
    if (m_song) {
        chart.title  = m_song->name;
        chart.artist = m_song->artist;
    }

    const auto& edNotes = notes();
    uint32_t id = 0;
    for (const auto& en : edNotes) {
        NoteEvent ev{};
        ev.time = (double)en.time;
        ev.id   = id++;

        switch (en.type) {
            case EditorNoteType::Tap:
                ev.type = NoteType::Tap;
                ev.data = TapData{(float)en.track};
                break;
            case EditorNoteType::Hold:
                ev.type = NoteType::Hold;
                ev.data = HoldData{(float)en.track, en.endTime - en.time};
                break;
            case EditorNoteType::Slide:
                ev.type = NoteType::Slide;
                ev.data = TapData{(float)en.track};
                break;
        }
        chart.notes.push_back(ev);
    }

    // Sort by time
    std::sort(chart.notes.begin(), chart.notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.time < b.time; });

    return chart;
}

// ── exportAllCharts ─────────────────────────────────────────────────────────

void SongEditor::exportAllCharts() {
    if (!m_song || m_projectPath.empty()) return;

    const char* diffSuffix[] = {"easy", "medium", "hard"};
    Difficulty  diffs[]      = {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard};

    for (int d = 0; d < 3; d++) {
        auto& edNotes = m_diffNotes[d];
        if (edNotes.empty()) continue;

        // Save current difficulty, temporarily switch, build chart, restore
        Difficulty saved = m_currentDifficulty;
        m_currentDifficulty = diffs[d];
        ChartData chart = buildChartFromNotes();
        m_currentDifficulty = saved;

        // Write unified chart JSON
        std::string relPath = std::string("assets/charts/") + m_song->name + "_" + diffSuffix[d] + ".json";
        std::string fullPath = m_projectPath + "/" + relPath;

        // Ensure directory exists
        fs::create_directories(fs::path(fullPath).parent_path());

        std::ofstream f(fullPath);
        if (!f.is_open()) continue;

        f << "{\"version\": \"1.0\",\n";
        f << " \"title\": \"" << chart.title << "\",\n";
        f << " \"artist\": \"" << chart.artist << "\",\n";
        f << " \"offset\": " << chart.offset << ",\n";
        f << " \"notes\": [\n";
        for (size_t i = 0; i < chart.notes.size(); i++) {
            auto& n = chart.notes[i];
            f << "    {\"time\": " << n.time << ", \"type\": ";
            switch (n.type) {
                case NoteType::Tap:   f << "\"tap\"";   break;
                case NoteType::Hold:  f << "\"hold\"";  break;
                case NoteType::Slide: f << "\"slide\""; break;
                default:              f << "\"tap\"";    break;
            }
            f << ", \"lane\": ";
            if (auto* tap = std::get_if<TapData>(&n.data))        f << tap->laneX;
            else if (auto* hold = std::get_if<HoldData>(&n.data)) f << hold->laneX;
            else f << 0;
            if (n.type == NoteType::Hold) {
                if (auto* hold = std::get_if<HoldData>(&n.data))
                    f << ", \"duration\": " << hold->duration;
            }
            f << "}";
            if (i + 1 < chart.notes.size()) f << ",";
            f << "\n";
        }
        f << "  ]\n}\n";
        f.close();

        // Set the chart path on SongInfo
        std::replace(relPath.begin(), relPath.end(), '\\', '/');
        switch (diffs[d]) {
            case Difficulty::Easy:   m_song->chartEasy   = relPath; break;
            case Difficulty::Medium: m_song->chartMedium = relPath; break;
            case Difficulty::Hard:   m_song->chartHard   = relPath; break;
        }
    }
}

// ── launchTestProcess ────────────────────────────────────────────────────────

void SongEditor::launchTestProcess() {
#ifdef _WIN32
    // Get the path to our own executable
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Build the absolute project path
    fs::path absProject = fs::absolute(fs::path(m_projectPath));
    std::string projectArg = absProject.string();

    // Build command line: "exe" --test "project_path"
    std::wstring cmdLine = std::wstring(L"\"") + exePath + L"\" --test \"";
    // Convert project path to wide string
    int len = MultiByteToWideChar(CP_UTF8, 0, projectArg.c_str(), -1, nullptr, 0);
    std::wstring wProject(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, projectArg.c_str(), -1, wProject.data(), len);
    cmdLine += wProject + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                       0, nullptr, nullptr, &si, &pi)) {
        // Don't wait — let the test game run independently
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        m_statusMsg   = "Test Game launched!";
        m_statusTimer = 3.f;
        std::cout << "[SongEditor] Launched test game process\n";
    } else {
        m_testErrorMsg = "Failed to launch test game process.";
        m_showTestError = true;
    }
#else
    // Unix: fork + exec
    std::string exePath = "/proc/self/exe"; // Linux
    fs::path absProject = fs::absolute(fs::path(m_projectPath));
    std::string cmd = std::string("\"") + exePath + "\" --test \"" + absProject.string() + "\" &";
    system(cmd.c_str());
    m_statusMsg   = "Test Game launched!";
    m_statusTimer = 3.f;
#endif
}

// ── renderDifficultySelector ────────────────────────────────────────────────

void SongEditor::renderDifficultySelector() {
    struct DiffInfo {
        const char* label;
        Difficulty  diff;
        ImVec4      color;
    };
    DiffInfo diffs[] = {
        {"Easy",   Difficulty::Easy,   ImVec4(0.2f, 0.7f, 0.3f, 1.f)},
        {"Medium", Difficulty::Medium, ImVec4(0.8f, 0.6f, 0.1f, 1.f)},
        {"Hard",   Difficulty::Hard,   ImVec4(0.8f, 0.2f, 0.2f, 1.f)},
    };

    ImGui::Text("Difficulty:");
    ImGui::SameLine();

    for (auto& d : diffs) {
        bool active = (m_currentDifficulty == d.diff);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        d.color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(d.color.x + 0.1f, d.color.y + 0.1f, d.color.z + 0.1f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(d.color.x - 0.05f, d.color.y - 0.05f, d.color.z - 0.05f, 1.f));
        }
        if (ImGui::Button(d.label, ImVec2(70, 24))) {
            m_currentDifficulty = d.diff;
            m_pressFirstClick = false; // cancel any pending press note
        }
        if (active) ImGui::PopStyleColor(3);
        ImGui::SameLine();
    }

    // Show note count for current difficulty
    ImGui::TextDisabled("(%d notes)", (int)notes().size());
}

// ── renderNoteToolbar ───────────────────────────────────────────────────────

void SongEditor::renderNoteToolbar() {
    if (!m_song) return;
    const GameModeConfig& gm = m_song->gameMode;
    bool is3D = (gm.type == GameModeType::DropNotes && gm.dimension == DropDimension::ThreeD);

    ImGui::Spacing();

    // Pointer tool (no note placement, marker mode)
    auto toolBtn = [&](const char* label, NoteTool tool, ImVec4 color) {
        bool active = (m_noteTool == tool);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(color.x - 0.05f, color.y - 0.05f, color.z - 0.05f, 1.f));
        }
        if (ImGui::Button(label, ImVec2(80, 26))) {
            if (m_noteTool == tool)
                m_noteTool = NoteTool::None; // toggle off
            else {
                m_noteTool = tool;
                m_pressFirstClick = false; // reset pending press
            }
        }
        if (active) ImGui::PopStyleColor(3);
        ImGui::SameLine();
    };

    toolBtn("Marker", NoteTool::None, ImVec4(0.5f, 0.4f, 0.2f, 1.f));
    toolBtn("Click",  NoteTool::Tap, ImVec4(0.2f, 0.5f, 0.8f, 1.f));
    toolBtn("Press",  NoteTool::Hold, ImVec4(0.2f, 0.7f, 0.3f, 1.f));

    // Slide: not available for ScanLine; always available for 2D, Circle, and 3D ground
    // (3D sky restriction is handled at placement time)
    if (gm.type != GameModeType::ScanLine) {
        toolBtn("Slide", NoteTool::Slide, ImVec4(0.7f, 0.3f, 0.7f, 1.f));
    }

    // Show pending press indicator
    if (m_pressFirstClick && m_noteTool == NoteTool::Hold) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "Click end point...");
    }

    // Show hint for 3D sky restriction
    if (is3D && m_noteTool == NoteTool::Slide) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.3f, 1.f), "(Sky: no slide)");
    }

    // ── Beat Analysis buttons ───────────────────────────────────────────────
    ImGui::SameLine(0.f, 20.f);  // gap before analysis buttons

    if (m_analyzer.isRunning()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.f));
        ImGui::Button("Analyzing...", ImVec2(100, 26));
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.5f, 0.2f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.2f, 0.6f, 0.25f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.1f, 0.4f, 0.15f, 1.f));
        if (ImGui::Button("Analyze Beats", ImVec2(100, 26))) {
            if (m_song && !m_song->audioFile.empty()) {
                std::string fullAudioPath = m_projectPath + "/" + m_song->audioFile;
                m_analyzer.setCallback([this](AudioAnalysisResult result) {
                    if (result.success) {
                        m_diffMarkers[(int)Difficulty::Easy]   = std::move(result.easyMarkers);
                        m_diffMarkers[(int)Difficulty::Medium] = std::move(result.mediumMarkers);
                        m_diffMarkers[(int)Difficulty::Hard]   = std::move(result.hardMarkers);
                        m_statusMsg   = "Beats analyzed! BPM: " + std::to_string((int)result.bpm);
                        m_statusTimer = 4.f;
                    } else {
                        m_analysisErrorMsg  = result.errorMessage;
                        m_showAnalysisError = true;
                    }
                });
                m_analyzer.startAnalysis(fullAudioPath);
            }
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear Markers", ImVec2(96, 26))) {
        markers().clear();
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.5f, 0.35f, 0.1f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.6f, 0.45f, 0.15f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.4f, 0.25f, 0.08f, 1.f));
    if (ImGui::Button("Place All", ImVec2(72, 26))) {
        // Place a Tap note on every marker, distributing across tracks
        int tc = m_song ? m_song->gameMode.trackCount : 7;
        int track = 0;
        for (float t : markers()) {
            // Skip if a note already exists near this time
            bool exists = false;
            for (auto& n : notes()) {
                if (fabsf(n.time - t) < 0.01f) { exists = true; break; }
            }
            if (!exists) {
                EditorNote note;
                note.type  = EditorNoteType::Tap;
                note.time  = t;
                note.track = track % tc;
                notes().push_back(note);
                track++;
            }
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Place a Tap note on every marker");
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.15f, 0.15f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.7f, 0.2f, 0.2f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.5f, 0.1f, 0.1f, 1.f));
    if (ImGui::Button("Clear Notes", ImVec2(86, 26))) {
        notes().clear();
    }
    ImGui::PopStyleColor(3);

    // Analysis error popup
    if (m_showAnalysisError) {
        ImGui::OpenPopup("Analysis Error");
        m_showAnalysisError = false;
    }
    if (ImGui::BeginPopupModal("Analysis Error", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", m_analysisErrorMsg.c_str());
        ImGui::Spacing();
        float btnW = 120.f;
        ImGui::SetCursorPosX((ImGui::GetWindowSize().x - btnW) * 0.5f);
        if (ImGui::Button("OK", ImVec2(btnW, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Spacing();
}

// ── snapToMarker ────────────────────────────────────────────────────────────

float SongEditor::snapToMarker(float time) const {
    if (markers().empty()) return time;

    float bestTime = markers()[0];
    float bestDist = fabsf(time - bestTime);
    for (size_t i = 1; i < markers().size(); i++) {
        float d = fabsf(time - markers()[i]);
        if (d < bestDist) { bestDist = d; bestTime = markers()[i]; }
    }
    return bestTime;
}

// ── trackFromY ──────────────────────────────────────────────────────────────

int SongEditor::trackFromY(float mouseY, float regionTop, float trackH, int trackCount) const {
    int t = (int)((mouseY - regionTop) / trackH);
    if (t < 0) t = 0;
    if (t >= trackCount) t = trackCount - 1;
    return t;
}

// ── handleNotePlacement ─────────────────────────────────────────────────────

void SongEditor::handleNotePlacement(ImVec2 origin, ImVec2 size, float startTime,
                                     int trackCount, bool is3DSky,
                                     float trackH, float regionTop,
                                     Engine* engine) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.MouseClicked[0] || io.KeyCtrl || io.KeyShift || io.KeyAlt) return;

    float mouseX = io.MousePos.x;
    float mouseY = io.MousePos.y;
    float rawTime = startTime + (mouseX - origin.x) / m_timelineZoom;
    float snappedTime = snapToMarker(rawTime);
    int   track = trackFromY(mouseY, regionTop, trackH, trackCount);

    // 3D sky: only Click and Press
    NoteTool effectiveTool = m_noteTool;
    if (is3DSky && effectiveTool == NoteTool::Slide)
        return; // disallow

    if (effectiveTool == NoteTool::Hold) {
        if (!m_pressFirstClick) {
            // First click: record start
            m_pressFirstClick = true;
            m_pressStartTime  = snappedTime;
            m_pressStartTrack = track;
            m_pressStartSky   = is3DSky;
        } else {
            // Second click: create the press note
            EditorNote note;
            note.type    = EditorNoteType::Hold;
            note.time    = m_pressStartTime;
            note.endTime = snappedTime;
            note.track   = m_pressStartTrack;
            note.isSky   = m_pressStartSky;
            // Ensure start < end
            if (note.endTime < note.time) std::swap(note.time, note.endTime);
            notes().push_back(note);
            m_pressFirstClick = false;
            if (engine) engine->audio().playClickSfx();
        }
    } else {
        // Click or Slide: single click to place
        EditorNote note;
        note.type  = (effectiveTool == NoteTool::Tap) ? EditorNoteType::Tap : EditorNoteType::Slide;
        note.time  = snappedTime;
        note.track = track;
        note.isSky = is3DSky;
        notes().push_back(note);
        if (engine) engine->audio().playClickSfx();
    }
}

// ── renderNotes ─────────────────────────────────────────────────────────────

void SongEditor::renderNotes(ImDrawList* dl, ImVec2 origin, ImVec2 size,
                             float startTime, int trackCount,
                             float trackH, float regionTop, bool skyOnly) {
    if (!m_song) return;
    const GameModeConfig& gm = m_song->gameMode;

    float pxPerSec = m_timelineZoom;
    float pMaxX = origin.x + size.x;

    // Judgment windows from settings (ms -> seconds)
    const float perfectSec = gm.perfectMs * 0.001f;
    const float goodSec    = gm.goodMs    * 0.001f;
    const float badSec     = gm.badMs     * 0.001f;

    // Per-type perfect colors (used to distinguish note types visually)
    // Click = blue-green, Press = green, Slide = purple-green
    auto perfectColor = [](EditorNoteType t) -> ImU32 {
        switch (t) {
            case EditorNoteType::Tap: return IM_COL32(40, 160, 220, 80);
            case EditorNoteType::Hold: return IM_COL32(40, 200, 80, 80);
            case EditorNoteType::Slide: return IM_COL32(180, 60, 200, 80);
        }
        return IM_COL32(40, 200, 80, 80);
    };

    // Helper: draw judgment bands around a time position
    auto drawJudgmentBands = [&](float px, float cy, float hh, EditorNoteType type) {
        float badPx     = badSec     * pxPerSec;
        float goodPx    = goodSec    * pxPerSec;
        float perfectPx = perfectSec * pxPerSec;

        // Red (Bad) band — outermost
        dl->AddRectFilled(ImVec2(px - badPx, cy - hh),
                          ImVec2(px + badPx, cy + hh),
                          IM_COL32(200, 60, 40, 30), 2.f);
        // Yellow (Good) band
        dl->AddRectFilled(ImVec2(px - goodPx, cy - hh),
                          ImVec2(px + goodPx, cy + hh),
                          IM_COL32(200, 180, 40, 45), 2.f);
        // Perfect band — color indicates note type
        dl->AddRectFilled(ImVec2(px - perfectPx, cy - hh),
                          ImVec2(px + perfectPx, cy + hh),
                          perfectColor(type), 2.f);
    };

    for (const auto& note : notes()) {
        if (note.isSky != skyOnly) continue;

        float noteX = origin.x + (note.time - startTime) * pxPerSec;
        if (noteX < origin.x - 60 || noteX > pMaxX + 60) continue;

        float centerY = regionTop + (note.track + 0.5f) * trackH;
        float halfH   = trackH * 0.35f;

        // Judgment bands at the note start
        drawJudgmentBands(noteX, centerY, halfH, note.type);

        if (note.type == EditorNoteType::Hold) {
            float endX = origin.x + (note.endTime - startTime) * pxPerSec;

            // Judgment bands at the note end
            drawJudgmentBands(endX, centerY, halfH, note.type);

            // Hold bar body
            dl->AddRectFilled(ImVec2(noteX, centerY - halfH * 0.6f),
                              ImVec2(endX, centerY + halfH * 0.6f),
                              IM_COL32(50, 180, 80, 120), 3.f);
            dl->AddRect(ImVec2(noteX, centerY - halfH * 0.6f),
                        ImVec2(endX, centerY + halfH * 0.6f),
                        IM_COL32(80, 220, 100, 200), 3.f, 0, 1.5f);

            // Start cap
            dl->AddRectFilled(ImVec2(noteX - 3, centerY - halfH),
                              ImVec2(noteX + 3, centerY + halfH),
                              IM_COL32(80, 220, 100, 255), 2.f);
            // End cap
            dl->AddRectFilled(ImVec2(endX - 3, centerY - halfH),
                              ImVec2(endX + 3, centerY + halfH),
                              IM_COL32(80, 220, 100, 255), 2.f);

        } else if (note.type == EditorNoteType::Tap) {
            // Click: diamond shape
            ImVec2 pts[4] = {
                ImVec2(noteX,            centerY - halfH),
                ImVec2(noteX + halfH,    centerY),
                ImVec2(noteX,            centerY + halfH),
                ImVec2(noteX - halfH,    centerY),
            };
            dl->AddQuadFilled(pts[0], pts[1], pts[2], pts[3],
                              IM_COL32(60, 140, 255, 200));
            dl->AddQuad(pts[0], pts[1], pts[2], pts[3],
                        IM_COL32(100, 180, 255, 255), 1.5f);

        } else {
            // Slide: arrow/triangle pointing right
            ImVec2 pts[3] = {
                ImVec2(noteX - halfH,    centerY - halfH),
                ImVec2(noteX + halfH,    centerY),
                ImVec2(noteX - halfH,    centerY + halfH),
            };
            dl->AddTriangleFilled(pts[0], pts[1], pts[2],
                                  IM_COL32(200, 80, 220, 200));
            dl->AddTriangle(pts[0], pts[1], pts[2],
                            IM_COL32(230, 120, 255, 255), 1.5f);
        }
    }
}
