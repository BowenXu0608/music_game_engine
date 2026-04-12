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

// ── Disk animation sampling helpers (used by both scene preview and
//    chart timeline lane-mask builder) ────────────────────────────────────────
//
// Mirrors LanotaRenderer::{getDiskCenter, getCurrentRotation, getDiskScale}
// so the editor can evaluate the current disk pose at any song time without
// instantiating the gameplay renderer.  Keep in sync with LanotaRenderer.cpp.

namespace {
    constexpr float kPi_ = 3.14159265358979f;

    float applyDiskEasing(float t, DiskEasing e) {
        switch (e) {
            case DiskEasing::SineInOut:
                return -(std::cos(kPi_ * t) - 1.f) * 0.5f;
            case DiskEasing::QuadInOut:
                return t < 0.5f ? 2.f * t * t
                                 : 1.f - (-2.f * t + 2.f) * (-2.f * t + 2.f) * 0.5f;
            case DiskEasing::CubicInOut:
                return t < 0.5f ? 4.f * t * t * t
                                 : 1.f - (-2.f * t + 2.f) * (-2.f * t + 2.f) * (-2.f * t + 2.f) * 0.5f;
            case DiskEasing::Linear:
            default:
                return t;
        }
    }

    template <typename EventT, typename ValueT, typename GetTarget>
    ValueT sampleSegment(double songTime,
                         const std::vector<EventT>& events,
                         const ValueT& base,
                         GetTarget getTarget) {
        if (events.empty() || songTime < events.front().startTime) return base;
        size_t i = 0;
        for (; i + 1 < events.size(); ++i)
            if (events[i + 1].startTime > songTime) break;
        const EventT& cur = events[i];
        ValueT prev = (i == 0) ? base : getTarget(events[i - 1]);
        double segEnd = cur.startTime + cur.duration;
        if (cur.duration <= 1e-6 || songTime >= segEnd) return getTarget(cur);
        float t = static_cast<float>((songTime - cur.startTime) / cur.duration);
        float e = applyDiskEasing(std::clamp(t, 0.f, 1.f), cur.easing);
        return prev + e * (getTarget(cur) - prev);
    }

    glm::vec2 sampleDiskCenter(double t, const std::vector<DiskMoveEvent>& ev) {
        return sampleSegment<DiskMoveEvent, glm::vec2>(
            t, ev, {0.f, 0.f},
            [](const DiskMoveEvent& e){ return e.target; });
    }
    float sampleDiskScale(double t, const std::vector<DiskScaleEvent>& ev) {
        return sampleSegment<DiskScaleEvent, float>(
            t, ev, 1.f,
            [](const DiskScaleEvent& e){ return e.targetScale; });
    }
    float sampleDiskRotation(double t, const std::vector<DiskRotationEvent>& ev) {
        return sampleSegment<DiskRotationEvent, float>(
            t, ev, 0.f,
            [](const DiskRotationEvent& e){ return e.targetAngle; });
    }

    // Reachability predicate bounds match the FOV/Z=0 hit plane used by
    // LanotaRenderer (FOV_Y=60°, eye at z=4): ~±3.0 world-units horizontally
    // and ~±2.3 vertically, shrunk by a 5% inner margin.
    constexpr float kPlayHalfX = 3.0f * 0.95f;
    constexpr float kPlayHalfY = 2.3f * 0.95f;
    constexpr float kBaseRadius = 2.4f;  // LanotaRenderer BASE_RADIUS

    uint32_t laneMaskForTransform(int trackCount,
                                  const glm::vec2& center,
                                  float scale,
                                  float rotation) {
        if (trackCount <= 0) return 0xFFFFFFFFu;
        const float outerR = kBaseRadius * scale;
        uint32_t mask = 0;
        for (int lane = 0; lane < trackCount && lane < 32; ++lane) {
            float a = kPi_ * 0.5f - (static_cast<float>(lane) / trackCount) * (kPi_ * 2.f) + rotation;
            float lx = center.x + std::cos(a) * outerR;
            float ly = center.y + std::sin(a) * outerR;
            if (std::abs(lx) > kPlayHalfX || std::abs(ly) > kPlayHalfY) continue;
            mask |= (1u << lane);
        }
        return mask;
    }
} // namespace

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
    m_bpmChanges.clear();
    m_dominantBpm = 0.f;
    m_holdDragging  = false;
    m_holdLastTrack = -1;

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
                if (auto* tap = std::get_if<TapData>(&n.data)) {
                    lane = (int)tap->laneX;
                    en.scanX = tap->scanX;
                    en.scanY = tap->scanY;
                    if (!tap->scanPath.empty()) en.scanPath = tap->scanPath;
                    if (tap->duration > 0.f)    en.endTime  = en.time + tap->duration;
                    if (!tap->samplePoints.empty()) en.samplePoints = tap->samplePoints;
                } else if (auto* hold = std::get_if<HoldData>(&n.data)) {
                    lane = (int)hold->laneX;
                    en.scanX           = hold->scanX;
                    en.scanY           = hold->scanY;
                    en.scanEndY        = hold->scanEndY;
                    en.scanHoldSweeps  = hold->scanHoldSweeps;
                } else if (auto* flick = std::get_if<FlickData>(&n.data)) {
                    lane = (int)flick->laneX;
                    en.scanX = flick->scanX;
                    en.scanY = flick->scanY;
                }
                en.track = lane;

                if (n.type == NoteType::Hold) {
                    en.type = EditorNoteType::Hold;
                    if (auto* hold = std::get_if<HoldData>(&n.data)) {
                        en.endTime = en.time + hold->duration;
                        en.laneSpan = hold->laneSpan;
                        if (!hold->waypoints.empty()) {
                            // Multi-waypoint chart
                            en.waypoints.reserve(hold->waypoints.size());
                            for (const auto& w : hold->waypoints) {
                                EditorHoldWaypoint ew{};
                                ew.tOffset       = w.tOffset;
                                ew.lane          = w.lane;
                                ew.transitionLen = w.transitionLen;
                                ew.style         = (EditorHoldTransition)(int)w.style;
                                en.waypoints.push_back(ew);
                            }
                            en.endTrack = -1; // legacy field unused
                        } else if (hold->endLaneX >= 0.f && hold->endLaneX != hold->laneX) {
                            en.endTrack = (int)hold->endLaneX;
                            en.transition      = (EditorHoldTransition)(int)hold->transition;
                            en.transitionLen   = hold->transitionLen;
                            en.transitionStart = hold->transitionStart;
                        } else {
                            en.endTrack = -1;
                        }
                        en.samplePoints.reserve(hold->samplePoints.size());
                        for (const auto& sp : hold->samplePoints)
                            en.samplePoints.push_back(sp.tOffset);
                    }
                } else if (n.type == NoteType::Slide) {
                    en.type = EditorNoteType::Slide;
                } else if (n.type == NoteType::Flick) {
                    en.type = EditorNoteType::Flick;
                } else {
                    en.type = EditorNoteType::Tap;
                }
                edNotes.push_back(en);
            }

            // Disk animation (circle mode) — copy into the per-difficulty
            // editor state so the author can edit existing keyframes.
            m_diffDiskRot  [(int)diff] = chart.diskAnimation.rotations;
            m_diffDiskMove [(int)diff] = chart.diskAnimation.moves;
            m_diffDiskScale[(int)diff] = chart.diskAnimation.scales;
            m_diffScanSpeed[(int)diff] = chart.scanSpeedEvents;
            m_laneMaskDirty = true;
            m_scanPhaseDirty = true;

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
        fs::path srcPath(szFile);

        fs::path absProject = fs::absolute(fs::path(m_projectPath));
        fs::path destDir    = absProject / "assets" / destSubdir;
        fs::create_directories(destDir);
        fs::path dest = destDir / srcPath.filename();
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
        const bool scanLineMode = (m_song->gameMode.type == GameModeType::ScanLine);
        float editableH = std::max(80.f, bodyH - waveformH - splitterThick);
        float sceneH    = scanLineMode ? editableH
                                       : std::max(40.f, editableH * m_sceneSplit);
        float timelineH = scanLineMode ? 0.f
                                       : std::max(40.f, editableH - sceneH);

        // ── Scene Preview ───────────────────────────────────────────────────
        ImGui::BeginChild("SEScene", ImVec2(0, sceneH), true,
                          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
        {
            ImGui::TextDisabled("Preview");
            ImGui::SameLine();
            renderDifficultySelector();

            // Scan-line mode: scene-embedded tool toolbar.
            if (scanLineMode) {
                ImGui::SameLine(0.f, 20.f);
                auto toolBtn = [&](const char* label, NoteTool t) {
                    bool on = (m_noteTool == t);
                    if (on) {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f, 0.55f, 0.35f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f,  0.65f, 0.4f,  1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.18f, 0.45f, 0.28f, 1.f));
                    }
                    if (ImGui::Button(label, ImVec2(54, 0))) {
                        m_noteTool = on ? NoteTool::None : t;
                        m_scanHoldAwaitEnd  = false;
                        m_scanSlideDragging = false;
                    }
                    if (on) ImGui::PopStyleColor(3);
                    ImGui::SameLine();
                };
                toolBtn("Tap",   NoteTool::Tap);
                toolBtn("Flick", NoteTool::Flick);
                toolBtn("Hold",  NoteTool::Hold);
                toolBtn("Slide", NoteTool::Slide);
                ImGui::NewLine();
            }

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

        if (!scanLineMode) {
            // ── Vertical splitter (scene | timeline) ────────────────────────
            ImGui::InvisibleButton("se_vsplit", ImVec2(-1, splitterThick));
            if (ImGui::IsItemActive() && editableH > 1.f) {
                m_sceneSplit += ImGui::GetIO().MouseDelta.y / editableH;
                m_sceneSplit = std::clamp(m_sceneSplit, 0.15f, 0.65f);
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

            // ── Chart Timeline ──────────────────────────────────────────────
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
        }

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

    // ── Note Properties popup ────────────────────────────────────────────────
    // Opens whenever a note is left-clicked in the timeline. Each section is
    // gated by game mode or note type — laneSpan is Circle-only, and the Hold
    // transition / sample points section only appears for Hold notes.
    if (m_selectedNoteIdx >= 0 && m_selectedNoteIdx < (int)notes().size()) {
        ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_Appearing);
        bool open = true;
        if (ImGui::Begin("Note Properties", &open,
                         ImGuiWindowFlags_NoCollapse)) {
            EditorNote& sel = notes()[m_selectedNoteIdx];
            const char* typeName =
                (sel.type == EditorNoteType::Tap)   ? "Tap"   :
                (sel.type == EditorNoteType::Hold)  ? "Hold"  :
                (sel.type == EditorNoteType::Flick) ? "Flick" : "Slide";
            ImGui::Text("Type: %s", typeName);
            ImGui::Text("Time: %.3f s", sel.time);
            const bool isScanLine = (m_song && m_song->gameMode.type == GameModeType::ScanLine);
            if (sel.type == EditorNoteType::Hold) {
                ImGui::Text("End:  %.3f s  (%.3f s long)",
                            sel.endTime, sel.endTime - sel.time);
                if (isScanLine)
                    ImGui::Text("Pos:  (%.2f, %.2f) → %.2f",
                                sel.scanX, sel.scanY, sel.scanEndY);
                else
                    ImGui::Text("Track: %d → %d", sel.track, sel.effectiveEndTrack());
            } else if (sel.type == EditorNoteType::Slide && isScanLine) {
                ImGui::Text("End:  %.3f s  (%.3f s long)",
                            sel.endTime, sel.endTime - sel.time);
                ImGui::Text("Path:  %d points", (int)sel.scanPath.size());
            } else if (isScanLine) {
                ImGui::Text("Pos:  (%.2f, %.2f)", sel.scanX, sel.scanY);
            } else {
                ImGui::Text("Track: %d", sel.track);
            }
            ImGui::Separator();

            // Circle-mode lane width
            if (m_song && m_song->gameMode.type == GameModeType::Circle) {
                ImGui::Text("Lane Width");
                float btnW = (ImGui::GetContentRegionAvail().x
                              - ImGui::GetStyle().ItemSpacing.x * 2) / 3.f;
                for (int w = 1; w <= 3; ++w) {
                    char lbl[16];
                    snprintf(lbl, sizeof(lbl), "%d lane%s", w, w == 1 ? "" : "s");
                    bool on = (sel.laneSpan == w);
                    if (on) {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.55f, 0.25f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.3f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.45f, 0.2f, 1.f));
                    }
                    if (ImGui::Button(lbl, ImVec2(btnW, 26)))
                        sel.laneSpan = w;
                    if (on) ImGui::PopStyleColor(3);
                    if (w < 3) ImGui::SameLine();
                }
                ImGui::Spacing();
                ImGui::Separator();
            }

            // Hold-only: per-segment lane change editor + sample points
            if (sel.type == EditorNoteType::Hold) {
                const float duration = std::max(0.f, sel.endTime - sel.time);

                if (sel.waypoints.empty()) {
                    ImGui::TextDisabled("Straight hold (no lane changes)");
                } else {
                    ImGui::Text("Lane changes (%d segments)", (int)sel.waypoints.size() - 1);
                    ImGui::TextDisabled("Each segment: previous lane → target lane.");
                    ImGui::TextDisabled("Drag the duration to control how long the");
                    ImGui::TextDisabled("change takes (ends at the marked time).");
                    ImGui::Spacing();

                    const char* items[] = {"Straight", "90 Angle", "Curve", "Rhomboid"};
                    for (size_t i = 1; i < sel.waypoints.size(); ++i) {
                        ImGui::PushID((int)i);
                        const auto& prev = sel.waypoints[i - 1];
                        auto& cur = sel.waypoints[i];

                        ImGui::Text("[%zu] lane %d → %d  @%.2fs", i,
                                    prev.lane, cur.lane, cur.tOffset);

                        int style = (int)cur.style;
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::Combo("##style", &style, items, 4))
                            cur.style = (EditorHoldTransition)style;

                        // Max length = how much room there is between the prev
                        // waypoint and this one (the change has to fit inside).
                        float maxLen = std::max(0.f, cur.tOffset - prev.tOffset);
                        float tLen = std::clamp(cur.transitionLen, 0.f, maxLen);
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderFloat("##tlen", &tLen, 0.f,
                                               std::max(0.001f, maxLen), "%.3f s"))
                            cur.transitionLen = std::clamp(tLen, 0.f, maxLen);

                        ImGui::Spacing();
                        ImGui::PopID();
                    }
                }

                ImGui::Spacing();
                ImGui::Separator();

                // Sample points list
                ImGui::Text("Sample Points (hold checkpoints)");
                ImGui::TextDisabled("Players just keep holding — each tick awards combo.");

                // Add-at-playhead button: the playhead time is m_sceneTime
                float playT = m_sceneTime - sel.time;
                bool canAdd = (playT > 0.01f && playT < duration - 0.01f);
                if (!canAdd) ImGui::BeginDisabled();
                if (ImGui::Button("Add at playhead", ImVec2(-1, 24))) {
                    sel.samplePoints.push_back(playT);
                    std::sort(sel.samplePoints.begin(), sel.samplePoints.end());
                }
                if (!canAdd) ImGui::EndDisabled();

                int removeIdx = -1;
                for (size_t i = 0; i < sel.samplePoints.size(); ++i) {
                    ImGui::PushID((int)i);
                    float t = sel.samplePoints[i];
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
                    if (ImGui::SliderFloat("##sp", &t, 0.f, duration, "%.3f s"))
                        sel.samplePoints[i] = std::clamp(t, 0.f, duration);
                    ImGui::SameLine();
                    if (ImGui::Button("x", ImVec2(22, 0))) removeIdx = (int)i;
                    ImGui::PopID();
                }
                if (removeIdx >= 0)
                    sel.samplePoints.erase(sel.samplePoints.begin() + removeIdx);

                ImGui::Spacing();
                ImGui::Separator();
            }

            // ── Scan-line slide sample points ─────────────────────────────
            if (isScanLine && sel.type == EditorNoteType::Slide) {
                const float duration = std::max(0.f, sel.endTime - sel.time);
                ImGui::Text("Sample Points (slide ticks)");
                ImGui::TextDisabled("Click on the slide path in the scene to add one,");
                ImGui::TextDisabled("or press the button below at the current playhead.");

                float playT = m_sceneTime - sel.time;
                bool canAdd = (playT > 0.01f && playT < duration - 0.01f);
                if (!canAdd) ImGui::BeginDisabled();
                if (ImGui::Button("Add at playhead", ImVec2(-1, 24))) {
                    sel.samplePoints.push_back(playT);
                    std::sort(sel.samplePoints.begin(), sel.samplePoints.end());
                }
                if (!canAdd) ImGui::EndDisabled();

                if (sel.samplePoints.empty()) {
                    ImGui::TextDisabled("(no sample points yet)");
                } else {
                    int removeIdx = -1;
                    for (size_t i = 0; i < sel.samplePoints.size(); ++i) {
                        ImGui::PushID((int)i);
                        float t = sel.samplePoints[i];
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 30);
                        if (ImGui::SliderFloat("##slsp", &t, 0.f,
                                               std::max(0.001f, duration), "%.3f s"))
                            sel.samplePoints[i] = std::clamp(t, 0.f, duration);
                        ImGui::SameLine();
                        if (ImGui::Button("x", ImVec2(22, 0))) removeIdx = (int)i;
                        ImGui::PopID();
                    }
                    if (removeIdx >= 0)
                        sel.samplePoints.erase(sel.samplePoints.begin() + removeIdx);
                }

                if (ImGui::Button("Clear All Samples", ImVec2(-1, 22)))
                    sel.samplePoints.clear();

                ImGui::Spacing();
                ImGui::Separator();
            }

            if (ImGui::Button("Delete Note", ImVec2(-1, 26))) {
                notes().erase(notes().begin() + m_selectedNoteIdx);
                m_selectedNoteIdx = -1;
            }
        }
        ImGui::End();
        if (!open) m_selectedNoteIdx = -1;
    }
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

    // ── BPM Map (from analysis) ─────────────────────────────────────────────
    if (m_dominantBpm > 0.f && ImGui::CollapsingHeader("BPM Map", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Dominant BPM: %.1f", m_dominantBpm);
        if (m_bpmChanges.size() > 1) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.f, 1.f),
                "Dynamic tempo: %d sections", (int)m_bpmChanges.size());
            ImGui::Spacing();

            // Show tempo sections in a compact table
            if (ImGui::BeginTable("##bpmtable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.f);
                ImGui::TableSetupColumn("BPM",  ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < m_bpmChanges.size(); i++) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    int mins = (int)(m_bpmChanges[i].time / 60.f);
                    float secs = m_bpmChanges[i].time - mins * 60.f;
                    ImGui::Text("%d:%05.2f", mins, secs);
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f", m_bpmChanges[i].bpm);
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled("Constant tempo throughout");
        }

        // Button to clear BPM data
        if (ImGui::SmallButton("Clear BPM Map")) {
            m_bpmChanges.clear();
            m_dominantBpm = 0.f;
        }
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
        {"Basic Drop Notes", "Notes fall toward a hit zone",          GameModeType::DropNotes},
        {"Circle",           "Ring notes on a rotating disk",          GameModeType::Circle},
        {"Scan Line",        "Sweep line crosses notes on a 2D field", GameModeType::ScanLine},
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
    // Circle mode supports up to 36 lanes; other modes keep the original
    // 3..12 range they were designed and tested against, so bumping the
    // Circle cap doesn't accidentally change their playable layouts.
    int trackMax = (gm.type == GameModeType::Circle) ? 36 : 12;
    if (gm.trackCount > trackMax) gm.trackCount = trackMax;
    int prevTrackCount = gm.trackCount;
    if (ImGui::SliderInt("##tracks", &gm.trackCount, 3, trackMax, "%d tracks")
        && gm.trackCount != prevTrackCount && gm.trackCount > 0) {
        // Re-fit every authored note to the new lane count using modulo, so
        // notes that fell off the right edge wrap back into the highway.
        // Affects all difficulties — the editor only stores authored data
        // here; exported chart files keep whatever was active when written.
        const int newCount = gm.trackCount;
        for (auto& [diff, list] : m_diffNotes) {
            for (auto& en : list) {
                if (en.track >= newCount || en.track < 0)
                    en.track = ((en.track % newCount) + newCount) % newCount;
                if (en.endTrack >= 0 && en.endTrack >= newCount)
                    en.endTrack = ((en.endTrack % newCount) + newCount) % newCount;
                // Collapse cross-lane → straight if the wrap landed on the
                // same lane; otherwise the transition would be a no-op visual.
                if (en.endTrack == en.track) en.endTrack = -1;
            }
        }
    }
    ImGui::Spacing();

    // ── Default Note Width (Circle mode) ─────────────────────────────────────
    // Selects the laneSpan assigned to newly placed notes. The "Apply to All"
    // button rewrites every existing note in the current difficulty to this
    // width in one shot (the "adjust all the notes" box).
    if (gm.type == GameModeType::Circle) {
        ImGui::Text("Default Note Width");
        float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.f;
        for (int w = 1; w <= 3; ++w) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "%d lane%s", w, w == 1 ? "" : "s");
            bool sel = (m_defaultLaneSpan == w);
            if (sel) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.35f, 0.75f, 1.0f));
            }
            if (ImGui::Button(lbl, ImVec2(btnW, 24)))
                m_defaultLaneSpan = w;
            if (sel) ImGui::PopStyleColor(3);
            if (w < 3) ImGui::SameLine();
        }
        if (ImGui::Button("Apply to All Notes", ImVec2(-1, 24))) {
            for (auto& n : notes()) n.laneSpan = m_defaultLaneSpan;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rewrite every note in the current difficulty\n"
                              "to use the Default Note Width above.");
        ImGui::Spacing();

        // ── Disk Animation (rotate / scale / move keyframes) ────────────
        // Each keyframe has a startTime (when the change begins) and a
        // duration (how long the transform takes to reach its target).
        // Start time is captured from the playhead; duration is either
        // typed in the panel or dragged on the Disk FX timeline lane.
        if (ImGui::CollapsingHeader("Disk Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Spacing();

            // Track tabs.
            struct TrackOpt { const char* label; DiskKfTrack v; };
            TrackOpt tracks[] = {
                {"Rotate", DiskKfTrack::Rotation},
                {"Scale",  DiskKfTrack::Scale   },
                {"Move",   DiskKfTrack::Move    },
            };
            float tw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.f;
            for (int ti = 0; ti < 3; ++ti) {
                bool sel = (m_diskKfTrack == tracks[ti].v);
                if (sel) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.35f, 0.75f, 1.0f));
                }
                if (ImGui::Button(tracks[ti].label, ImVec2(tw, 22))) {
                    m_diskKfTrack    = tracks[ti].v;
                    m_selectedDiskKf = -1;
                }
                if (sel) ImGui::PopStyleColor(3);
                if (ti < 2) ImGui::SameLine();
            }
            ImGui::Spacing();

            // Count for the current track.
            auto trackCount = [&]() -> size_t {
                switch (m_diskKfTrack) {
                    case DiskKfTrack::Rotation: return diskRot().size();
                    case DiskKfTrack::Scale:    return diskScale().size();
                    case DiskKfTrack::Move:     return diskMove().size();
                }
                return 0;
            };

            // Add at playhead — the answer to "how to set start time easily".
            char addLabel[64];
            snprintf(addLabel, sizeof(addLabel), "+ Add at Playhead (t=%.2fs)", m_sceneTime);
            if (ImGui::Button(addLabel, ImVec2(-1, 26))) {
                switch (m_diskKfTrack) {
                    case DiskKfTrack::Rotation: {
                        DiskRotationEvent e{};
                        e.startTime   = m_sceneTime;
                        e.duration    = 1.0;
                        e.targetAngle = 0.f;
                        diskRot().push_back(e);
                        std::sort(diskRot().begin(), diskRot().end(),
                                  [](const DiskRotationEvent& a, const DiskRotationEvent& b){ return a.startTime < b.startTime; });
                        m_selectedDiskKf = (int)diskRot().size() - 1;
                        break;
                    }
                    case DiskKfTrack::Scale: {
                        DiskScaleEvent e{};
                        e.startTime   = m_sceneTime;
                        e.duration    = 1.0;
                        e.targetScale = 1.f;
                        diskScale().push_back(e);
                        std::sort(diskScale().begin(), diskScale().end(),
                                  [](const DiskScaleEvent& a, const DiskScaleEvent& b){ return a.startTime < b.startTime; });
                        m_selectedDiskKf = (int)diskScale().size() - 1;
                        break;
                    }
                    case DiskKfTrack::Move: {
                        DiskMoveEvent e{};
                        e.startTime = m_sceneTime;
                        e.duration  = 1.0;
                        e.target    = {0.f, 0.f};
                        diskMove().push_back(e);
                        std::sort(diskMove().begin(), diskMove().end(),
                                  [](const DiskMoveEvent& a, const DiskMoveEvent& b){ return a.startTime < b.startTime; });
                        m_selectedDiskKf = (int)diskMove().size() - 1;
                        break;
                    }
                }
                m_laneMaskDirty = true;
            }

            ImGui::Spacing();
            ImGui::TextDisabled("%zu keyframe(s)", trackCount());
            ImGui::Spacing();

            // Inline list of every keyframe on the active track. Each row
            // is a selectable button — click to edit it below. Displayed in
            // a scrollable child so long lists stay usable.
            {
                const float rowH  = ImGui::GetFrameHeight();
                const float listH = std::min(6.f, (float)std::max<size_t>(1, trackCount())) * (rowH + 4.f) + 8.f;
                ImGui::BeginChild("##diskKfList", ImVec2(-1, listH), true);
                auto renderRow = [&](int idx, const char* summary) {
                    bool sel = (m_selectedDiskKf == idx);
                    ImGui::PushID(idx);
                    if (ImGui::Selectable(summary, sel, 0, ImVec2(0, rowH))) {
                        m_selectedDiskKf = idx;
                    }
                    ImGui::PopID();
                };
                char buf[96];
                switch (m_diskKfTrack) {
                    case DiskKfTrack::Rotation:
                        for (int i = 0; i < (int)diskRot().size(); ++i) {
                            const auto& e = diskRot()[i];
                            snprintf(buf, sizeof(buf),
                                     "#%d  t=%.2fs  dur=%.2fs  %.0f°",
                                     i, e.startTime, e.duration,
                                     e.targetAngle * 57.2957795f);
                            renderRow(i, buf);
                        }
                        break;
                    case DiskKfTrack::Scale:
                        for (int i = 0; i < (int)diskScale().size(); ++i) {
                            const auto& e = diskScale()[i];
                            snprintf(buf, sizeof(buf),
                                     "#%d  t=%.2fs  dur=%.2fs  %.2f×",
                                     i, e.startTime, e.duration, e.targetScale);
                            renderRow(i, buf);
                        }
                        break;
                    case DiskKfTrack::Move:
                        for (int i = 0; i < (int)diskMove().size(); ++i) {
                            const auto& e = diskMove()[i];
                            snprintf(buf, sizeof(buf),
                                     "#%d  t=%.2fs  dur=%.2fs  (%.2f,%.2f)",
                                     i, e.startTime, e.duration,
                                     e.target.x, e.target.y);
                            renderRow(i, buf);
                        }
                        break;
                }
                if (trackCount() == 0)
                    ImGui::TextDisabled("No keyframes yet — use + Add at Playhead.");
                ImGui::EndChild();
            }
            ImGui::Spacing();

            // Selected keyframe editor.
            const auto easingCombo = [](const char* label, DiskEasing& e) {
                const char* names[] = {"Linear", "SineInOut", "QuadInOut", "CubicInOut"};
                int cur = (int)e;
                if (ImGui::Combo(label, &cur, names, 4)) e = (DiskEasing)cur;
            };

            int sel = m_selectedDiskKf;
            if (sel >= 0 && sel < (int)trackCount()) {
                ImGui::Separator();
                ImGui::Text("Selected keyframe #%d", sel);
                ImGui::Spacing();

                auto editCommon = [&](double& startTime, double& duration, DiskEasing& easing) {
                    float st = (float)startTime;
                    if (ImGui::InputFloat("Start (s)", &st, 0.05f, 0.25f, "%.3f")) {
                        startTime = std::max(0.0, (double)st);
                        m_laneMaskDirty = true;
                    }
                    float du = (float)duration;
                    if (ImGui::InputFloat("Duration (s)", &du, 0.05f, 0.25f, "%.3f")) {
                        duration = std::max(0.0, (double)du);
                        m_laneMaskDirty = true;
                    }
                    easingCombo("Easing", easing);
                };

                bool remove = false;
                switch (m_diskKfTrack) {
                    case DiskKfTrack::Rotation: {
                        auto& ev = diskRot()[sel];
                        editCommon(ev.startTime, ev.duration, ev.easing);
                        float deg = ev.targetAngle * 57.2957795f;
                        if (ImGui::SliderFloat("Target angle (deg)", &deg, -360.f, 360.f, "%.1f")) {
                            ev.targetAngle = deg * 0.01745329252f;
                            m_laneMaskDirty = true;
                        }
                        if (ImGui::Button("Delete##kf", ImVec2(-1, 22))) remove = true;
                        if (remove) {
                            diskRot().erase(diskRot().begin() + sel);
                            m_selectedDiskKf = -1;
                            m_laneMaskDirty = true;
                        }
                        break;
                    }
                    case DiskKfTrack::Scale: {
                        auto& ev = diskScale()[sel];
                        editCommon(ev.startTime, ev.duration, ev.easing);
                        if (ImGui::SliderFloat("Target scale", &ev.targetScale, 0.1f, 3.0f, "%.2f×"))
                            m_laneMaskDirty = true;
                        if (ImGui::Button("Delete##kf", ImVec2(-1, 22))) remove = true;
                        if (remove) {
                            diskScale().erase(diskScale().begin() + sel);
                            m_selectedDiskKf = -1;
                            m_laneMaskDirty = true;
                        }
                        break;
                    }
                    case DiskKfTrack::Move: {
                        auto& ev = diskMove()[sel];
                        editCommon(ev.startTime, ev.duration, ev.easing);
                        float xy[2] = {ev.target.x, ev.target.y};
                        if (ImGui::SliderFloat2("Target XY (world)", xy, -5.f, 5.f, "%.2f")) {
                            ev.target = {xy[0], xy[1]};
                            m_laneMaskDirty = true;
                        }
                        if (ImGui::Button("Delete##kf", ImVec2(-1, 22))) remove = true;
                        if (remove) {
                            diskMove().erase(diskMove().begin() + sel);
                            m_selectedDiskKf = -1;
                            m_laneMaskDirty = true;
                        }
                        break;
                    }
                }
            } else if (trackCount() > 0) {
                ImGui::TextDisabled("Select a keyframe from the list above.");
            }

            ImGui::Spacing();
        }
    }

    // ── Scan Line Speed (Cytus mode only) ──────────────────────────────────
    if (gm.type == GameModeType::ScanLine &&
        ImGui::CollapsingHeader("Scan Line Speed")) {
        ImGui::Spacing();
        ImGui::TextWrapped("Speed multiplier keyframes. 1.0x = base BPM speed.");
        ImGui::Spacing();

        auto& ssList = scanSpeed();
        for (int i = 0; i < (int)ssList.size(); ++i) {
            char label[64];
            snprintf(label, sizeof(label), "#%d  t=%.2fs  %.2fx##ss%d",
                     i, ssList[i].startTime, ssList[i].targetSpeed, i);
            bool selected = (m_selectedScanSpeedKf == i);
            if (ImGui::Selectable(label, selected))
                m_selectedScanSpeedKf = i;
        }

        ImGui::Spacing();
        char addLabel[64];
        snprintf(addLabel, sizeof(addLabel), "+ Add at Playhead (t=%.2fs)##ss_add", m_sceneTime);
        if (ImGui::Button(addLabel, ImVec2(-1, 26))) {
            ScanSpeedEvent e{};
            e.startTime   = m_sceneTime;
            e.duration    = 1.0;
            e.targetSpeed = 1.0f;
            ssList.push_back(e);
            std::sort(ssList.begin(), ssList.end(),
                      [](const ScanSpeedEvent& a, const ScanSpeedEvent& b) {
                          return a.startTime < b.startTime; });
            m_selectedScanSpeedKf = -1;
            for (int i = 0; i < (int)ssList.size(); ++i)
                if (std::abs(ssList[i].startTime - m_sceneTime) < 1e-4)
                    m_selectedScanSpeedKf = i;
            m_scanPhaseDirty = true;
        }

        int sel = m_selectedScanSpeedKf;
        if (sel >= 0 && sel < (int)ssList.size()) {
            ImGui::Separator();
            auto& ev = ssList[sel];
            float st = (float)ev.startTime;
            if (ImGui::InputFloat("Start (s)##ss", &st, 0.05f, 0.25f, "%.3f")) {
                ev.startTime = std::max(0.0, (double)st);
                m_scanPhaseDirty = true;
            }
            float du = (float)ev.duration;
            if (ImGui::InputFloat("Duration (s)##ss", &du, 0.05f, 0.25f, "%.3f")) {
                ev.duration = std::max(0.0, (double)du);
                m_scanPhaseDirty = true;
            }
            if (ImGui::SliderFloat("Target Speed##ss", &ev.targetSpeed, 0.1f, 4.0f, "%.2fx"))
                m_scanPhaseDirty = true;

            const char* easingNames[] = {"Linear", "SineInOut", "QuadInOut", "CubicInOut"};
            int curEase = (int)ev.easing;
            if (ImGui::Combo("Easing##ss", &curEase, easingNames, 4)) {
                ev.easing = (DiskEasing)curEase;
                m_scanPhaseDirty = true;
            }

            if (ImGui::Button("Delete##ss_del", ImVec2(-1, 22))) {
                ssList.erase(ssList.begin() + sel);
                m_selectedScanSpeedKf = -1;
                m_scanPhaseDirty = true;
            }
        }
        ImGui::Spacing();
    }

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
        // Circle mode reserves a short strip at the top for Disk FX keyframes
        // (rotate / scale / move), so lane height is computed after that.
        const bool  isCircle  = (gm.type == GameModeType::Circle);
        const float diskFxH   = isCircle ? 34.f : 0.f;
        const float laneTop   = origin.y + diskFxH;
        const float laneAreaH = size.y - diskFxH;
        float trackH = laneAreaH / tc;

        // Track lane separators
        for (int i = 0; i <= tc; i++) {
            float y = laneTop + i * trackH;
            ImU32 col = (i == 0 || i == tc) ? IM_COL32(60, 80, 140, 160)
                                             : IM_COL32(40, 50, 80, 80);
            dl->AddLine(ImVec2(origin.x, y), ImVec2(pMax.x, y), col, 1.f);
        }

        // Track number labels (left side)
        for (int i = 0; i < tc; i++) {
            float y = laneTop + (i + 0.5f) * trackH;
            char label[8]; snprintf(label, sizeof(label), "%d", i + 1);
            dl->AddText(ImVec2(origin.x + 4, y - 6),
                        IM_COL32(80, 100, 150, 140), label);
        }

        // ── Circle mode: Disk FX strip + lane gray-out overlay ──────────
        if (isCircle) {
            // Three tiny sub-lanes (Rot/Scale/Move) in the top strip.
            const float subH = (diskFxH - 4.f) / 3.f;
            const ImU32 rotCol   = IM_COL32(220, 120, 240, 220);
            const ImU32 scaleCol = IM_COL32(120, 220, 180, 220);
            const ImU32 moveCol  = IM_COL32(240, 180, 100, 220);
            const ImU32 rotColDim   = IM_COL32(220, 120, 240, 120);
            const ImU32 scaleColDim = IM_COL32(120, 220, 180, 120);
            const ImU32 moveColDim  = IM_COL32(240, 180, 100, 120);

            auto drawStripBg = [&](float y0, const char* label, ImU32 lblCol) {
                dl->AddRectFilled(ImVec2(origin.x, y0), ImVec2(pMax.x, y0 + subH),
                                  IM_COL32(25, 25, 45, 255));
                dl->AddLine(ImVec2(origin.x, y0 + subH), ImVec2(pMax.x, y0 + subH),
                            IM_COL32(60, 60, 90, 120), 1.f);
                dl->AddText(ImVec2(origin.x + 4, y0 + 1), lblCol, label);
            };
            const float rotY   = origin.y + 2.f;
            const float scaleY = rotY   + subH;
            const float moveY  = scaleY + subH;
            drawStripBg(rotY,   "ROT",   IM_COL32(220, 160, 255, 200));
            drawStripBg(scaleY, "SCL",   IM_COL32(180, 255, 220, 200));
            drawStripBg(moveY,  "MOV",   IM_COL32(255, 220, 160, 200));

            const ImVec2 mp = ImGui::GetIO().MousePos;
            const bool   mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            const bool   rightClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

            auto drawTrack = [&](auto& list, float yTop, ImU32 col, ImU32 colDim,
                                 DiskKfTrack trackId) {
                for (int idx = 0; idx < (int)list.size(); ++idx) {
                    auto& ev = list[idx];
                    float bx0 = origin.x + ((float)ev.startTime - startTime) * m_timelineZoom;
                    float bx1 = bx0 + (float)ev.duration * m_timelineZoom;
                    if (bx1 < origin.x || bx0 > pMax.x) continue;
                    bool selected = (m_diskKfTrack == trackId && m_selectedDiskKf == idx);
                    ImU32 fill = selected ? col : colDim;
                    dl->AddRectFilled(ImVec2(std::max(bx0, origin.x), yTop + 2),
                                      ImVec2(std::min(bx1, pMax.x),  yTop + subH - 2),
                                      fill, 3.f);
                    if (selected) {
                        dl->AddRect(ImVec2(std::max(bx0, origin.x), yTop + 2),
                                    ImVec2(std::min(bx1, pMax.x),  yTop + subH - 2),
                                    IM_COL32(255, 255, 255, 255), 3.f, 0, 2.f);
                    }
                    dl->AddLine(ImVec2(bx1 - 1, yTop + 3), ImVec2(bx1 - 1, yTop + subH - 3),
                                IM_COL32(255, 255, 255, 220), 2.f);
                    const bool overBar = (mp.x >= bx0 && mp.x <= bx1 &&
                                          mp.y >= yTop && mp.y <= yTop + subH);
                    if (overBar) {
                        if (mouseClicked) {
                            m_diskKfTrack    = trackId;
                            m_selectedDiskKf = idx;
                        }
                        if (rightClicked) {
                            list.erase(list.begin() + idx);
                            if (m_diskKfTrack == trackId && m_selectedDiskKf >= (int)list.size())
                                m_selectedDiskKf = -1;
                            m_laneMaskDirty = true;
                            return;
                        }
                    }
                }
            };
            drawTrack(diskRot(),   rotY,   rotCol,   rotColDim,   DiskKfTrack::Rotation);
            drawTrack(diskScale(), scaleY, scaleCol, scaleColDim, DiskKfTrack::Scale);
            drawTrack(diskMove(),  moveY,  moveCol,  moveColDim,  DiskKfTrack::Move);

            // ── Lane gray-out overlay for disabled segments ─────────────
            // Mask timeline is rebuilt lazily from the current keyframes.
            if (m_laneMaskDirty) rebuildLaneMaskTimeline();
            if (!m_laneMaskTimeline.empty() && tc > 0 && tc <= 32) {
                for (size_t si = 0; si < m_laneMaskTimeline.size(); ++si) {
                    double tBeg = m_laneMaskTimeline[si].startTime;
                    double tEnd = (si + 1 < m_laneMaskTimeline.size())
                                  ? m_laneMaskTimeline[si + 1].startTime
                                  : (double)endTime;
                    uint32_t mask = m_laneMaskTimeline[si].mask;
                    float bx0 = origin.x + ((float)tBeg - startTime) * m_timelineZoom;
                    float bx1 = origin.x + ((float)tEnd - startTime) * m_timelineZoom;
                    if (bx1 < origin.x || bx0 > pMax.x) continue;
                    bx0 = std::max(bx0, origin.x);
                    bx1 = std::min(bx1, pMax.x);
                    for (int lane = 0; lane < tc; ++lane) {
                        if (mask & (1u << lane)) continue;
                        float ly0 = laneTop + lane       * trackH;
                        float ly1 = laneTop + (lane + 1) * trackH;
                        dl->AddRectFilled(ImVec2(bx0, ly0), ImVec2(bx1, ly1),
                                          IM_COL32(80, 80, 80, 140));
                        // Diagonal hatching so it reads as "disabled".
                        for (float xh = bx0; xh < bx1; xh += 8.f) {
                            dl->AddLine(ImVec2(xh, ly0), ImVec2(xh + 8.f, ly1),
                                        IM_COL32(140, 140, 140, 120), 1.f);
                        }
                    }
                }
            }
        }
    }

    // Draw BPM change markers (orange band + label at top)
    for (size_t bi = 0; bi < m_bpmChanges.size(); bi++) {
        float bx = origin.x + (m_bpmChanges[bi].time - startTime) * m_timelineZoom;
        if (bx < origin.x - 60.f || bx > pMax.x + 10.f) continue;
        // Vertical dashed line (cyan)
        for (float y = origin.y; y < pMax.y; y += 6.f) {
            float y2 = std::min(y + 3.f, pMax.y);
            dl->AddLine(ImVec2(bx, y), ImVec2(bx, y2),
                        IM_COL32(0, 200, 220, 160), 1.5f);
        }
        // BPM label at top
        char bpmLabel[32];
        snprintf(bpmLabel, sizeof(bpmLabel), "%.0f", m_bpmChanges[bi].bpm);
        dl->AddRectFilled(ImVec2(bx - 1, origin.y), ImVec2(bx + 30, origin.y + 14),
                          IM_COL32(0, 160, 180, 200), 2.f);
        dl->AddText(ImVec2(bx + 2, origin.y), IM_COL32(255, 255, 255, 240), bpmLabel);
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
        // 2D Drop Notes or Circle: single region. Circle mode reserves the
        // top 34 px for the Disk FX strip, so note lanes start below it.
        const float diskFxH = (gm.type == GameModeType::Circle) ? 34.f : 0.f;
        const float laneTop  = origin.y + diskFxH;
        const float laneAreaH = size.y - diskFxH;
        float trackH = laneAreaH / tc;
        renderNotes(dl, origin, size, startTime, tc, trackH, laneTop, false);

        if (mouseInTimeline && m_noteTool != NoteTool::None) {
            ImVec2 mpos3 = ImGui::GetIO().MousePos;
            if (mpos3.y >= laneTop) {
                handleNotePlacement(origin, size, startTime, tc, false, trackH, laneTop, engine);
            }
        }
    }

    // ── Drag-recording preview ──────────────────────────────────────────────
    // While the user is dragging the Hold tool, draw the in-progress
    // waypoint polyline so they can see what they're authoring.
    if (m_holdDragging && m_noteTool == NoteTool::Hold && !m_holdDraft.waypoints.empty()) {
        float trkH;
        float regTop;
        if (gm.type == GameModeType::DropNotes && gm.dimension == DropDimension::ThreeD) {
            float gap2    = 4.f;
            float skyH2   = (size.y - gap2) * 0.4f;
            float gndTop2 = origin.y + skyH2 + gap2;
            trkH   = m_holdDraft.isSky ? (skyH2 / tc) : ((size.y - gap2) * 0.6f / tc);
            regTop = m_holdDraft.isSky ? origin.y : gndTop2;
        } else {
            trkH   = size.y / tc;
            regTop = origin.y;
        }
        const auto& wps = m_holdDraft.waypoints;
        for (size_t i = 0; i < wps.size(); ++i) {
            float t1 = m_holdDraft.time + wps[i].tOffset;
            float x1 = origin.x + (t1 - startTime) * m_timelineZoom;
            float y1 = regTop + (wps[i].lane + 0.5f) * trkH;
            float t2 = (i + 1 < wps.size())
                         ? m_holdDraft.time + wps[i + 1].tOffset
                         : m_holdDraft.endTime;
            float x2 = origin.x + (t2 - startTime) * m_timelineZoom;
            float y2 = regTop + (wps[i].lane + 0.5f) * trkH;
            dl->AddRectFilled(ImVec2(x1, y1 - 4), ImVec2(x2, y1 + 4),
                              IM_COL32(80, 220, 100, 130), 2.f);
            if (i + 1 < wps.size()) {
                float yNext = regTop + (wps[i + 1].lane + 0.5f) * trkH;
                dl->AddLine(ImVec2(x2, y1), ImVec2(x2, yNext),
                            IM_COL32(120, 240, 140, 220), 2.f);
                (void)y2;
            }
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
                    m_holdDragging  = false;
                    m_holdLastTrack = -1;
                    m_holdDraft     = EditorNote{};
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

    // Rebuild scan-line phase table lazily when speed events change
    if (m_scanPhaseDirty) rebuildScanPhaseTable();

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
            case EditorNoteType::Flick: return IM_COL32(255, 180, 80, 230);
        }
        return IM_COL32(100, 180, 255, 230);
    };
    auto noteColorDim = [](EditorNoteType t) -> ImU32 {
        switch (t) {
            case EditorNoteType::Tap: return IM_COL32(60, 110, 160, 160);
            case EditorNoteType::Hold: return IM_COL32(50, 140, 60, 160);
            case EditorNoteType::Slide: return IM_COL32(140, 80, 160, 160);
            case EditorNoteType::Flick: return IM_COL32(180, 120, 50, 160);
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
        float proj11 = std::abs(proj[1][1]);
        for (const auto& note : curNotes) {
            // Visibility window: a note is kept alive while any part of it
            // (head → tail for holds) is within the approach/hit range.
            // This way a hold whose head just passed stays fully on screen
            // until its tail crosses too, matching real music games.
            float dt = note.time - curTime;
            float tailDt = (note.type == EditorNoteType::Hold)
                             ? (note.endTime - curTime) : dt;
            if (tailDt < -0.3f || dt > lookAhead) continue;

            bool isSky = is3D && note.isSky;

            auto laneToWorldX = [&](float lane) {
                return (lane - (tc - 1) * 0.5f) * laneSpacing;
            };

            // ── Hold body (walks waypoints when present) ───────────────────
            if (note.type == EditorNoteType::Hold && note.endTime > note.time) {
                const float duration = note.endTime - note.time;
                const int N = note.waypoints.empty() ? 6 : 20;
                ImVec2 prevL{}, prevR{};
                bool havePrev = false;
                float worldY = isSky ? SKY_Y : 0.f;

                auto laneAt = [&](float tOff) -> float {
                    if (note.waypoints.empty())
                        return (float)note.track;
                    // Mirror evalHoldLaneAt waypoint logic inline (no HoldData
                    // conversion needed here — the math is small).
                    const auto& wps = note.waypoints;
                    if (tOff <= wps.front().tOffset) return (float)wps.front().lane;
                    if (tOff >= wps.back().tOffset)  return (float)wps.back().lane;
                    for (size_t wi = 1; wi < wps.size(); ++wi) {
                        const auto& a = wps[wi - 1];
                        const auto& b = wps[wi];
                        if (tOff > b.tOffset) continue;
                        float tLen = std::clamp(b.transitionLen, 0.f, b.tOffset - a.tOffset);
                        float tBeg = b.tOffset - tLen;
                        if (tOff <= tBeg || tLen <= 0.f)
                            return tOff >= b.tOffset ? (float)b.lane : (float)a.lane;
                        float u  = (tOff - tBeg) / tLen;
                        float la = (float)a.lane, lb = (float)b.lane;
                        switch (b.style) {
                            case EditorHoldTransition::Angle90: return lb;
                            case EditorHoldTransition::Curve: {
                                float s = u * u * (3.f - 2.f * u);
                                return la + (lb - la) * s;
                            }
                            case EditorHoldTransition::Rhomboid:
                                return la + (lb - la) * u;
                            default: return lb;
                        }
                    }
                    return (float)wps.back().lane;
                };

                for (int i = 0; i <= N; ++i) {
                    float tOff = (float)i / (float)N * duration;
                    float absDt = (note.time + tOff) - curTime;
                    float wz = -absDt * SCROLL_SPEED;
                    if (wz > 12.f || wz < -60.f) { havePrev = false; continue; }
                    float wx = laneToWorldX(laneAt(tOff));
                    glm::vec3 wp{wx, worldY, wz};
                    glm::vec4 wc = vp * glm::vec4(wp, 1.f);
                    if (wc.w <= 0.f) { havePrev = false; continue; }
                    float nwHere = laneSpacing * proj11 * size.y * 0.5f / wc.w;
                    float half = nwHere * 0.4f;
                    ImVec2 c = w2s(wp);
                    ImVec2 L(c.x - half, c.y);
                    ImVec2 R(c.x + half, c.y);
                    if (havePrev) {
                        dl->AddQuadFilled(prevL, prevR, R, L, noteColorDim(note.type));
                    }
                    prevL = L; prevR = R; havePrev = true;
                }
            }

            // ── Head quad ──────────────────────────────────────────────────
            float noteZ = -dt * SCROLL_SPEED;
            float worldX = laneToWorldX((float)note.track);
            float worldY = isSky ? SKY_Y : 0.f;
            glm::vec3 worldPos{worldX, worldY, noteZ};
            glm::vec4 clip = vp * glm::vec4(worldPos, 1.f);
            if (clip.w <= 0.f) continue;
            ImVec2 screen = w2s(worldPos);

            float nw = laneSpacing * proj11 * size.y * 0.5f / clip.w;
            if (nw < 2.f) continue;
            float nh = nw * 0.3f;

            dl->AddRectFilled(ImVec2(screen.x - nw / 2, screen.y - nh / 2),
                              ImVec2(screen.x + nw / 2, screen.y + nh / 2),
                              noteColor(note.type), 2.f);
        }

        const char* label = is3D ? "3D Drop Notes" : "2D Drop Notes";
        dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                    IM_COL32(200, 200, 200, 180), label);

    } else if (gm.type == GameModeType::Circle) {
        // ── Circle mode ─────────────────────────────────────────────────────
        // Sample the live disk pose from the current keyframes so that
        // scrubbing the playhead animates the scene preview exactly like
        // the runtime renderer does.
        glm::vec2 diskC   = sampleDiskCenter  (curTime, diskMove());
        float     diskS   = sampleDiskScale   (curTime, diskScale());
        float     diskRot_= sampleDiskRotation(curTime, diskRot());

        float baseCX = origin.x + size.x * 0.5f;
        float baseCY = origin.y + size.y * 0.52f;
        float minDim = (size.x < size.y) ? size.x : size.y;
        float baseOuterR = minDim * 0.44f;

        // Preview uses a "world unit = (baseOuterR / kBaseRadius) pixels"
        // mapping so move-keyframe world XY drives the preview center
        // proportionally.  Y is flipped because ImGui y grows downward.
        const float worldToPx = baseOuterR / kBaseRadius;
        float centerX = baseCX + diskC.x * worldToPx;
        float centerY = baseCY - diskC.y * worldToPx;
        float outerR  = baseOuterR * diskS;
        float innerR  = outerR * 0.28f;

        // Draw rings and radial lines
        dl->AddCircle(ImVec2(centerX, centerY), outerR,
                      IM_COL32(180, 140, 60, 220), 64, 2.5f);
        dl->AddCircle(ImVec2(centerX, centerY), innerR,
                      IM_COL32(140, 120, 80, 160), 48, 1.5f);
        dl->AddCircleFilled(ImVec2(centerX, centerY), innerR * 0.3f,
                            IM_COL32(100, 80, 50, 120));

        // Gameplay LanotaRenderer places lane 0 at 12 o'clock and counts
        // clockwise (matches onRender: angle = π/2 − lane·Δ + diskRotation).
        // ImGui y grows downward, so clockwise in screen space corresponds
        // to *increasing* angle in this coordinate system. We negate the
        // sampled disk rotation when mapping to screen so a positive target
        // angle spins the disk counter-clockwise on-screen, matching the
        // runtime renderer's y-flip.
        float angleStep = 6.2831853f / tc;
        auto laneAngle = [&](float laneIdx) {
            // screen angle: start at -π/2 (top), increase clockwise by lane
            return -1.5707963f + laneIdx * angleStep - diskRot_;
        };

        for (int i = 0; i < tc; i++) {
            float angle = laneAngle((float)i);
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

            // Wider notes extend clockwise from the authored lane: a span-S
            // note at lane N covers lanes N..N+S-1. aL is the CCW edge of
            // lane N; aR is the CW edge of lane N+S-1.
            int track = note.track % tc;
            int span  = std::clamp(note.laneSpan, 1, 3);
            float aL = laneAngle((float)track);
            float aR = laneAngle((float)(track + span));

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
        // BPM-driven scan line (1 bar per sweep, dominant BPM, 120 fallback).
        // Coordinate helpers — every conversion between the normalized
        // scan-space stored on EditorNote and the ImGui local rect must go
        // through these so resizing the scene stays proportional.
        auto scanToLocal = [&](float nx, float ny) -> ImVec2 {
            return ImVec2(origin.x + nx * size.x, origin.y + ny * size.y);
        };
        float frac    = scanLineFrac(curTime);
        bool  goingUp = scanLineGoingUp(curTime);
        float scanY   = scanToLocal(0.f, frac).y;

        // Scan line (glow + core)
        dl->AddLine(ImVec2(origin.x + 10, scanY), ImVec2(pMax.x - 10, scanY),
                    IM_COL32(0, 200, 255, 60), 6.f);
        dl->AddLine(ImVec2(origin.x + 10, scanY), ImVec2(pMax.x - 10, scanY),
                    IM_COL32(255, 255, 255, 220), 2.f);

        // Direction arrow at right edge
        {
            float ax = pMax.x - 18.f;
            float ay = scanY;
            if (goingUp) {
                dl->AddTriangleFilled(ImVec2(ax, ay - 10), ImVec2(ax - 7, ay + 2),
                                      ImVec2(ax + 7, ay + 2),
                                      IM_COL32(255, 255, 255, 200));
            } else {
                dl->AddTriangleFilled(ImVec2(ax, ay + 10), ImVec2(ax - 7, ay - 2),
                                      ImVec2(ax + 7, ay - 2),
                                      IM_COL32(255, 255, 255, 200));
            }
        }

        // ── Cytus-style page visibility ───────────────────────────────────
        // Each sweep (period T_sweep) is a "page". A note belongs to the
        // page containing its head time, and is visible ONLY while the
        // playhead is within that same page — no cross-turn overlap. Within
        // a page the note scales/fades in as the scan line approaches it
        // and stays visible until the end of the page (or the tail + small
        // fade-out for notes that end near a turn).
        constexpr float scanTailPad = 0.25f; // seconds after tail to fade out
        const float T_sweep = scanLinePeriod();

        auto pageIdx = [&](float t) -> int {
            return T_sweep > 1e-4f ? (int)std::floor(t / T_sweep) : 0;
        };
        const int curPage = pageIdx(curTime);

        auto computePageVis = [&](const EditorNote& n,
                                  float& outAlpha, float& outScale) -> bool {
            // Drop legacy/lane-authored notes with no scan position — they
            // would otherwise all stack at exactly (0,0) (the top-left).
            if (n.scanX < 0.001f && n.scanY < 0.001f &&
                n.scanEndY < 0.f && n.scanPath.empty())
                return false;

            const int notePage = pageIdx(n.time);
            if (n.type == EditorNoteType::Hold && n.scanHoldSweeps > 0) {
                const int endPage = pageIdx(n.endTime);
                if (curPage < notePage || curPage > endPage) return false;
            } else {
                if (notePage != curPage) return false;
            }

            const float pageStart = (float)notePage * T_sweep;
            const float pageEnd   = pageStart + T_sweep;
            const float tailTime  = (n.type == EditorNoteType::Hold ||
                                     n.type == EditorNoteType::Slide)
                                    ? std::max(n.endTime, n.time) : n.time;

            // Approach phase: from pageStart up to note.time the note
            // grows from a small seed to full size. Matches Cytus's
            // "scan line approaches, note appears and scales up".
            if (curTime <= n.time) {
                float denom = std::max(0.0001f, n.time - pageStart);
                float u = std::clamp((curTime - pageStart) / denom, 0.f, 1.f);
                outScale = 0.30f + 0.70f * u;
                outAlpha = 0.25f + 0.75f * u;
                return true;
            }

            // Post-hit phase: for Tap/Flick fade out quickly; for
            // Hold/Slide stay full-visible until tailTime, then fade.
            if (curTime <= tailTime) {
                outScale = 1.f;
                outAlpha = 1.f;
                return true;
            }
            float tailDt = curTime - tailTime;
            if (tailDt > scanTailPad || curTime > pageEnd) return false;
            outScale = 1.f;
            outAlpha = std::clamp(1.f - tailDt / scanTailPad, 0.f, 1.f);
            return true;
        };

        // ── Catmull-Rom tessellated slide path ─────────────────────────────
        // Each (p[i], p[i+1]) segment is interpolated with (p[i-1], p[i+2])
        // as tangent anchors; endpoints are duplicated. Produces a smooth
        // curve that still passes through every authored control point.
        auto drawSmoothPath = [&](const std::vector<std::pair<float,float>>& pts,
                                  ImU32 col, float thickness) {
            if (pts.size() < 2) return;
            constexpr int SUBDIV = 12;
            auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
            auto cr = [&](float p0, float p1, float p2, float p3, float t) {
                float t2 = t * t, t3 = t2 * t;
                return 0.5f * ((2.f * p1) +
                               (-p0 + p2) * t +
                               (2.f*p0 - 5.f*p1 + 4.f*p2 - p3) * t2 +
                               (-p0 + 3.f*p1 - 3.f*p2 + p3) * t3);
            };
            ImVec2 prev = scanToLocal(pts.front().first, pts.front().second);
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                const auto& p1 = pts[i];
                const auto& p2 = pts[i + 1];
                const auto& p0 = (i == 0) ? p1 : pts[i - 1];
                const auto& p3 = (i + 2 < pts.size()) ? pts[i + 2] : p2;
                for (int s = 1; s <= SUBDIV; ++s) {
                    float t = (float)s / (float)SUBDIV;
                    float nx = cr(p0.first,  p1.first,  p2.first,  p3.first,  t);
                    float ny = cr(p0.second, p1.second, p2.second, p3.second, t);
                    ImVec2 cur = scanToLocal(nx, ny);
                    dl->AddLine(prev, cur, col, thickness);
                    prev = cur;
                }
                (void)lerp;
            }
        };

        auto withAlpha = [](ImU32 col, float a) -> ImU32 {
            int aa = std::clamp((int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * a), 0, 255);
            return (col & ~IM_COL32_A_MASK) | ((ImU32)aa << IM_COL32_A_SHIFT);
        };

        for (const auto& note : curNotes) {
            float alpha = 1.f, scale = 1.f;
            if (!computePageVis(note, alpha, scale)) continue;

            ImVec2 head = scanToLocal(note.scanX, note.scanY);
            float nx = head.x, ny = head.y;
            ImU32 col     = withAlpha(noteColor(note.type), alpha);
            ImU32 colDim  = withAlpha(noteColorDim(note.type), alpha);
            ImU32 colOut  = withAlpha(IM_COL32(255,255,255,160), alpha);

            switch (note.type) {
                case EditorNoteType::Tap:
                    dl->AddCircleFilled(ImVec2(nx, ny), 10.f * scale, col);
                    dl->AddCircle(ImVec2(nx, ny), 11.f * scale, colOut, 0, 1.5f);
                    break;
                case EditorNoteType::Flick: {
                    bool up = scanLineGoingUp(note.time);
                    float s = 12.f * scale;
                    if (up) {
                        dl->AddTriangleFilled(ImVec2(nx, ny - s),
                                              ImVec2(nx - s * 0.85f, ny + s * 0.6f),
                                              ImVec2(nx + s * 0.85f, ny + s * 0.6f), col);
                    } else {
                        dl->AddTriangleFilled(ImVec2(nx, ny + s),
                                              ImVec2(nx - s * 0.85f, ny - s * 0.6f),
                                              ImVec2(nx + s * 0.85f, ny - s * 0.6f), col);
                    }
                    break;
                }
                case EditorNoteType::Hold: {
                    float hw = 6.f * scale;
                    if (note.scanHoldSweeps == 0) {
                        // Single-sweep hold: simple rectangle
                        float ey = scanToLocal(note.scanX, note.scanEndY).y;
                        dl->AddRectFilled(ImVec2(nx - hw, std::min(ny, ey)),
                                          ImVec2(nx + hw, std::max(ny, ey)),
                                          colDim, 2.f);
                        dl->AddCircleFilled(ImVec2(nx, ey), 6.f * scale, col);
                    } else {
                        // Multi-sweep hold: zigzag body segments
                        // Sweep 0: from start Y to the first turn boundary
                        bool sweepUp = scanLineGoingUp(note.time);
                        float segStartY = ny;
                        float turnY;
                        for (int s = 0; s <= note.scanHoldSweeps; ++s) {
                            if (s < note.scanHoldSweeps) {
                                // Full sweep segment to the turn boundary
                                turnY = scanToLocal(0.f, sweepUp ? 0.f : 1.f).y;
                            } else {
                                // Final sweep segment to the end Y
                                turnY = scanToLocal(note.scanX, note.scanEndY).y;
                            }
                            dl->AddRectFilled(
                                ImVec2(nx - hw, std::min(segStartY, turnY)),
                                ImVec2(nx + hw, std::max(segStartY, turnY)),
                                colDim, 2.f);
                            // Draw a small turn indicator at the boundary
                            if (s < note.scanHoldSweeps) {
                                dl->AddCircleFilled(ImVec2(nx, turnY), 3.f, col);
                            }
                            segStartY = turnY;
                            sweepUp = !sweepUp;
                        }
                        // Tail cap at final end Y
                        float ey = scanToLocal(note.scanX, note.scanEndY).y;
                        dl->AddCircleFilled(ImVec2(nx, ey), 6.f * scale, col);
                    }
                    // Head marker
                    dl->AddCircleFilled(ImVec2(nx, ny), 10.f * scale, col);
                    dl->AddCircle(ImVec2(nx, ny), 11.f * scale, colOut, 0, 1.5f);
                    break;
                }
                case EditorNoteType::Slide: {
                    // Draw straight lines between control points
                    if (note.scanPath.size() >= 2) {
                        ImVec2 prev = scanToLocal(note.scanPath[0].first,
                                                  note.scanPath[0].second);
                        for (size_t pi = 1; pi < note.scanPath.size(); ++pi) {
                            ImVec2 cur = scanToLocal(note.scanPath[pi].first,
                                                     note.scanPath[pi].second);
                            dl->AddLine(prev, cur, colDim, 4.f);
                            prev = cur;
                        }
                    }
                    // Head marker
                    dl->AddCircleFilled(ImVec2(nx, ny), 8.f * scale, col);
                    dl->AddCircle(ImVec2(nx, ny), 9.f * scale, colOut, 0, 1.5f);
                    // Sample/node markers at each control point after the head
                    if (note.scanPath.size() >= 2) {
                        ImU32 markerCol = withAlpha(IM_COL32(255,255,255,230), alpha);
                        for (size_t pi = 1; pi < note.scanPath.size(); ++pi) {
                            ImVec2 p = scanToLocal(note.scanPath[pi].first,
                                                   note.scanPath[pi].second);
                            dl->AddCircleFilled(p, 6.f * scale, markerCol);
                            dl->AddCircle(p, 7.f * scale, colOut, 0, 1.f);
                        }
                    }
                    break;
                }
            }
        }

        // ── In-progress hold preview ───────────────────────────────────────
        if (m_scanHoldAwaitEnd) {
            ImVec2 start = scanToLocal(m_scanHoldStartX, m_scanHoldStartY);
            float sx = start.x, sy = start.y;
            ImVec2 curMp = ImGui::GetMousePos();
            float ey = std::clamp(curMp.y, origin.y, pMax.y);

            // Determine the final sweep direction
            bool finalUp = m_scanHoldGoingUp;
            for (int i = 0; i < m_scanHoldExtraSweeps; ++i) finalUp = !finalUp;

            // Clamp end within the final sweep's range
            if (finalUp) ey = std::max(ey, origin.y);
            else          ey = std::min(ey, pMax.y);

            bool dirOk = true;
            if (m_scanHoldExtraSweeps == 0)
                dirOk = m_scanHoldGoingUp ? (ey <= sy) : (ey >= sy);

            ImU32 col  = dirOk ? IM_COL32(80, 220, 100, 200)
                               : IM_COL32(220, 80, 80, 200);

            // Ghost scan line at cursor Y
            ImU32 ghostCol = dirOk ? IM_COL32(80, 220, 140, 90)
                                   : IM_COL32(220, 80, 80, 90);
            dl->AddLine(ImVec2(origin.x + 10.f, ey),
                        ImVec2(pMax.x - 10.f, ey), ghostCol, 2.f);
            dl->AddLine(ImVec2(origin.x + 10.f, ey),
                        ImVec2(pMax.x - 10.f, ey),
                        IM_COL32(255, 255, 255, 30), 8.f);

            // Hold body preview — zigzag for multi-sweep
            float hw = 6.f;
            if (m_scanHoldExtraSweeps == 0) {
                dl->AddRectFilled(ImVec2(sx - hw, std::min(sy, ey)),
                                  ImVec2(sx + hw, std::max(sy, ey)),
                                  col, 2.f);
            } else {
                bool sweepUp = m_scanHoldGoingUp;
                float segStartY = sy;
                for (int s = 0; s <= m_scanHoldExtraSweeps; ++s) {
                    float segEndY;
                    if (s < m_scanHoldExtraSweeps) {
                        segEndY = scanToLocal(0.f, sweepUp ? 0.f : 1.f).y;
                    } else {
                        segEndY = ey;
                    }
                    dl->AddRectFilled(
                        ImVec2(sx - hw, std::min(segStartY, segEndY)),
                        ImVec2(sx + hw, std::max(segStartY, segEndY)),
                        col, 2.f);
                    if (s < m_scanHoldExtraSweeps)
                        dl->AddCircleFilled(ImVec2(sx, segEndY), 3.f,
                                            IM_COL32(255, 255, 255, 180));
                    segStartY = segEndY;
                    sweepUp = !sweepUp;
                }
            }

            // Sweep count indicator
            if (m_scanHoldExtraSweeps > 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "+%d sweeps", m_scanHoldExtraSweeps);
                dl->AddText(ImVec2(sx + 12.f, sy - 14.f),
                            IM_COL32(200, 255, 200, 220), buf);
            }

            // Head marker
            dl->AddCircleFilled(ImVec2(sx, sy), 10.f, col);

            // Projected end-time text near the cursor. Uses the same
            // normalization-safe inverse that the commit path will use so
            // "ETA" and the committed end time always agree.
            float endFrac = std::clamp((ey - origin.y) / std::max(1.f, size.y),
                                       0.f, 1.f);
            float endTime = scanLineTimeForFrac(m_scanHoldStartT, endFrac);
            endTime       = std::min(endTime, m_scanHoldTurnCap);
            char etaBuf[48];
            int mins = (int)(endTime / 60.f);
            float secs = endTime - (float)mins * 60.f;
            snprintf(etaBuf, sizeof(etaBuf), "ETA: %d:%06.3f", mins, secs);
            ImVec2 tsz = ImGui::CalcTextSize(etaBuf);
            ImVec2 txtPos(std::clamp(curMp.x + 14.f,
                                     origin.x + 4.f,
                                     pMax.x - tsz.x - 4.f),
                          std::clamp(ey - tsz.y - 4.f,
                                     origin.y + 4.f,
                                     pMax.y - tsz.y - 4.f));
            // Text shadow for legibility over any background
            dl->AddText(ImVec2(txtPos.x + 1, txtPos.y + 1),
                        IM_COL32(0, 0, 0, 200), etaBuf);
            dl->AddText(txtPos, dirOk ? IM_COL32(180, 255, 200, 255)
                                      : IM_COL32(255, 180, 180, 255), etaBuf);
        }

        // ── In-progress slide preview ──────────────────────────────────────
        if (m_scanSlideDragging && !m_scanSlideDraft.scanPath.empty()) {
            const auto& draftPath = m_scanSlideDraft.scanPath;
            // Draw straight lines between placed control points
            for (size_t i = 1; i < draftPath.size(); ++i) {
                ImVec2 a = scanToLocal(draftPath[i - 1].first, draftPath[i - 1].second);
                ImVec2 b = scanToLocal(draftPath[i].first, draftPath[i].second);
                dl->AddLine(a, b, IM_COL32(220, 130, 255, 230), 4.f);
            }
            // Draw a preview line from last point to current mouse position
            ImVec2 last = scanToLocal(draftPath.back().first, draftPath.back().second);
            ImVec2 mousePos = ImGui::GetMousePos();
            float cmx = std::clamp(mousePos.x, origin.x, pMax.x);
            float cmy = std::clamp(mousePos.y, origin.y, pMax.y);
            dl->AddLine(last, ImVec2(cmx, cmy), IM_COL32(220, 130, 255, 100), 2.f);
            // Head marker
            ImVec2 head = scanToLocal(draftPath[0].first, draftPath[0].second);
            dl->AddCircleFilled(head, 8.f, IM_COL32(220, 130, 255, 230));
            // Node markers at placed control points
            for (size_t i = 1; i < draftPath.size(); ++i) {
                ImVec2 p = scanToLocal(draftPath[i].first, draftPath[i].second);
                dl->AddCircleFilled(p, 6.f, IM_COL32(255, 255, 255, 220));
            }
        }

        dl->AddText(ImVec2(origin.x + 8, origin.y + 6),
                    IM_COL32(200, 200, 200, 180), "Scan Line Mode");

        // In-scene tool toolbar + mini instructions
        const char* toolLabel = "None";
        switch (m_noteTool) {
            case NoteTool::Tap:   toolLabel = "Tap";   break;
            case NoteTool::Flick: toolLabel = "Flick"; break;
            case NoteTool::Hold:  toolLabel = "Hold";  break;
            case NoteTool::Slide: toolLabel = "Slide"; break;
            default:              toolLabel = "None";  break;
        }
        char hud[96];
        snprintf(hud, sizeof(hud), "Tool: %s   BPM: %.1f", toolLabel,
                 m_dominantBpm > 0.f ? m_dominantBpm : 120.f);
        dl->AddText(ImVec2(origin.x + 8, origin.y + 22),
                    IM_COL32(200, 200, 200, 180), hud);

        // Dispatch input (uses ImGui::IsWindowHovered from the SEScene child)
        bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        handleScanLineInput(origin, size, curTime, hovered, engine);
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

// ── Scan Line helpers ───────────────────────────────────────────────────────
// Schedule: 1 bar per sweep at the dominant BPM (fallback 120). In a 4/4
// meter a bar is 4 beats, so T_sweep = 240/BPM seconds per sweep. Phase is
// derived from absolute song time via std::fmod on a double accumulator —
// this avoids the precision drift that would come from frame-by-frame time
// accumulation during prolonged playback. scanLineNextTurn returns exact
// integer multiples of T_sweep so sweep boundaries are bit-stable and free
// of logic gaps.

float SongEditor::scanLinePeriod() const {
    float bpm = m_dominantBpm > 0.f ? m_dominantBpm : 120.f;
    return 240.0f / bpm; // seconds per sweep = (60/bpm) * 4 beats
}

void SongEditor::rebuildScanPhaseTable() {
    m_scanPhaseTable.clear();
    m_scanPhaseDirty = false;
    auto& evts = const_cast<SongEditor*>(this)->scanSpeed();
    if (evts.empty()) return;

    double basePeriod = (double)scanLinePeriod();
    if (basePeriod <= 1e-6) return;

    // Sort events
    std::sort(evts.begin(), evts.end(),
              [](const ScanSpeedEvent& a, const ScanSpeedEvent& b) {
                  return a.startTime < b.startTime; });

    // Collect boundary times
    std::vector<double> boundaries;
    boundaries.push_back(0.0);
    for (auto& e : evts) {
        boundaries.push_back(e.startTime);
        boundaries.push_back(e.startTime + e.duration);
    }
    boundaries.push_back(600.0); // 10-minute ceiling
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
        [](double a, double b) { return std::abs(a - b) < 1e-9; }), boundaries.end());

    // Sample speed at time t (segment-based interpolation)
    auto sampleSpeed = [&](double t) -> double {
        if (evts.empty() || t < evts.front().startTime) return 1.0;
        size_t idx = 0;
        for (size_t i = 1; i < evts.size(); ++i)
            if (evts[i].startTime <= t) idx = i;
        const auto& cur = evts[idx];
        double prev = (idx == 0) ? 1.0 : (double)evts[idx - 1].targetSpeed;
        double segEnd = cur.startTime + cur.duration;
        if (cur.duration <= 1e-6 || t >= segEnd) return (double)cur.targetSpeed;
        float u = static_cast<float>((t - cur.startTime) / cur.duration);
        float e = applyDiskEasing(std::clamp(u, 0.f, 1.f), cur.easing);
        return prev + e * ((double)cur.targetSpeed - prev);
    };

    // Build table with Simpson's rule integration
    double accPhase = 0.0;
    m_scanPhaseTable.push_back({0.0, 0.0, sampleSpeed(0.0)});
    for (size_t bi = 1; bi < boundaries.size(); ++bi) {
        double t0 = boundaries[bi - 1], t1 = boundaries[bi];
        if (t1 <= t0) continue;
        constexpr int N = 16;
        double dt = (t1 - t0) / N;
        double integral = 0.0;
        for (int k = 0; k <= N; ++k) {
            double tk = t0 + k * dt;
            double w = (k == 0 || k == N) ? 1.0 : (k % 2 == 1) ? 4.0 : 2.0;
            integral += w * sampleSpeed(tk);
        }
        integral *= dt / 3.0;
        accPhase += integral / basePeriod;
        m_scanPhaseTable.push_back({t1, accPhase, sampleSpeed(t1)});
    }
}

double SongEditor::interpolateScanPhase(double t) const {
    double basePeriod = (double)scanLinePeriod();
    if (m_scanPhaseTable.empty()) return (t < 0 ? 0 : t) / basePeriod;
    if (t <= m_scanPhaseTable.front().time) return m_scanPhaseTable.front().phase;
    if (t >= m_scanPhaseTable.back().time) {
        const auto& last = m_scanPhaseTable.back();
        return last.phase + last.speed * (t - last.time) / basePeriod;
    }
    // Binary search
    size_t lo = 0, hi = m_scanPhaseTable.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (m_scanPhaseTable[mid].time <= t) lo = mid; else hi = mid;
    }
    const auto& a = m_scanPhaseTable[lo];
    const auto& b = m_scanPhaseTable[hi];
    double segDt = b.time - a.time;
    if (segDt < 1e-9) return a.phase;
    double frac = (t - a.time) / segDt;
    return a.phase + frac * (b.phase - a.phase);
}

float SongEditor::scanLineFrac(float t) const {
    if (m_scanPhaseTable.empty()) {
        const double T = (double)scanLinePeriod();
        if (T <= 1e-6) return 1.f;
        const double tt    = (t < 0.f) ? 0.0 : (double)t;
        const double phase = std::fmod(tt, 2.0 * T);
        if (phase < T) return (float)(1.0 - phase / T);
        return (float)((phase - T) / T);
    }
    double phase = interpolateScanPhase(t < 0.f ? 0.0 : (double)t);
    double cyc = std::fmod(phase, 2.0);
    if (cyc < 0.0) cyc += 2.0;
    if (cyc < 1.0) return static_cast<float>(1.0 - cyc);
    return static_cast<float>(cyc - 1.0);
}

bool SongEditor::scanLineGoingUp(float t) const {
    double phase;
    if (m_scanPhaseTable.empty()) {
        const double T = (double)scanLinePeriod();
        if (T <= 1e-6) return true;
        const double tt = (t < 0.f) ? 0.0 : (double)t;
        phase = std::fmod(tt, 2.0 * T) / T;
    } else {
        phase = interpolateScanPhase(t < 0.f ? 0.0 : (double)t);
    }
    double cyc = std::fmod(phase, 2.0);
    if (cyc < 0.0) cyc += 2.0;
    return cyc < 1.0;
}

float SongEditor::scanLineNextTurn(float t) const {
    // Find the next time where scan-line phase hits an integer.
    double basePeriod = (double)scanLinePeriod();
    if (basePeriod <= 1e-6) return t;
    double curPhase = interpolateScanPhase(t < 0.f ? 0.0 : (double)t);
    double nextInt = std::floor(curPhase) + 1.0;

    if (m_scanPhaseTable.empty()) {
        // Constant speed: time = phase * basePeriod
        return (float)(nextInt * basePeriod);
    }
    // Binary search the phase table for the time where phase == nextInt
    for (size_t i = 1; i < m_scanPhaseTable.size(); ++i) {
        if (m_scanPhaseTable[i].phase >= nextInt) {
            const auto& a = m_scanPhaseTable[i - 1];
            const auto& b = m_scanPhaseTable[i];
            double dp = b.phase - a.phase;
            if (dp < 1e-9) return (float)b.time;
            double frac = (nextInt - a.phase) / dp;
            return (float)(a.time + frac * (b.time - a.time));
        }
    }
    // Extrapolate
    const auto& last = m_scanPhaseTable.back();
    double remain = nextInt - last.phase;
    return (float)(last.time + remain * basePeriod / std::max(0.01, last.speed));
}

float SongEditor::scanLineTimeForFrac(float t, float frac) const {
    const double T = (double)scanLinePeriod();
    if (T <= 1e-6) return t;
    const float turn    = scanLineNextTurn(t);
    const bool  up      = scanLineGoingUp(t);
    const float curFrac = scanLineFrac(t);
    frac = std::clamp(frac, 0.f, 1.f);

    if (up && frac > curFrac)  return turn;
    if (!up && frac < curFrac) return turn;

    // Approximate: assume local speed ≈ constant over this small interval
    const double dFrac = std::abs((double)frac - (double)curFrac);
    const double dt    = dFrac * T;
    return (float)std::min((double)t + dt, (double)turn);
}

// ── handleScanLineInput ─────────────────────────────────────────────────────
// Called from the ScanLine branch of renderSceneView. Runs the state machine
// for the current tool. All click placement requires the cursor to be within
// a small vertical tolerance of the current scan line.

void SongEditor::handleScanLineInput(ImVec2 origin, ImVec2 size, float curTime,
                                     bool hovered, Engine* /*engine*/) {
    if (!m_song) return;

    // ── Coordinate-space helpers ──────────────────────────────────────────
    // Note data is stored in normalized scan space [0..1]²; the scene rect
    // can be resized arbitrarily. Every conversion between ImGui local
    // coordinates and stored note data must go through these.
    const float invW = 1.f / std::max(1.f, size.x);
    const float invH = 1.f / std::max(1.f, size.y);
    auto localToScan = [&](ImVec2 p) -> ImVec2 {
        return ImVec2((p.x - origin.x) * invW, (p.y - origin.y) * invH);
    };
    auto scanToLocal = [&](float nx, float ny) -> ImVec2 {
        return ImVec2(origin.x + nx * size.x, origin.y + ny * size.y);
    };

    ImVec2 mp       = ImGui::GetMousePos();
    ImVec2 pMax(origin.x + size.x, origin.y + size.y);
    bool   inRect   = (mp.x >= origin.x && mp.x <= pMax.x &&
                       mp.y >= origin.y && mp.y <= pMax.y);
    float  frac     = scanLineFrac(curTime);
    float  scanY    = origin.y + frac * size.y;
    bool   onLine   = hovered && inRect && std::abs(mp.y - scanY) < 10.f;
    bool   lmbClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool   lmbDown  = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool   rmbClick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    // Nothing to do if mouse isn't in the scene child.
    if (!hovered || !inRect) return;

    // ── Hold await-end state ────────────────────────────────────────────────
    // Mouse wheel extends the hold across scan-line direction changes.
    // The wheel direction that adds a sweep alternates: if the current
    // final sweep goes up, scroll up to extend; then scroll down for the
    // next sweep, etc. Wrong-direction scroll does nothing.
    if (m_scanHoldAwaitEnd) {
        // Cancel on RMB or ESC.
        if (rmbClick || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_scanHoldAwaitEnd = false;
            return;
        }

        // Determine the direction of the final (current) sweep
        // Sweep 0 = initial sweep direction; each extra sweep flips direction.
        bool finalSweepUp = m_scanHoldGoingUp;
        for (int i = 0; i < m_scanHoldExtraSweeps; ++i)
            finalSweepUp = !finalSweepUp;

        // Mouse wheel to add/remove extra sweeps
        float wheel = ImGui::GetIO().MouseWheel;
        if (std::abs(wheel) > 0.1f) {
            // The direction needed to ADD a sweep = the direction of the
            // NEXT sweep (opposite of the current final sweep).
            bool nextSweepUp = !finalSweepUp;
            bool scrollUp    = wheel > 0.f;
            if (scrollUp == nextSweepUp) {
                // Correct direction → add one more sweep
                m_scanHoldExtraSweeps++;
                finalSweepUp = !finalSweepUp;
            } else if (m_scanHoldExtraSweeps > 0) {
                // Opposite direction → remove last sweep (undo)
                m_scanHoldExtraSweeps--;
                finalSweepUp = !finalSweepUp;
            }
        }

        // Compute the time window for the final sweep.
        // Walk through turn points: turn[0] = first turn after start,
        // turn[i] = i-th turn after start.
        float finalSweepStart = m_scanHoldStartT;
        float finalSweepEnd   = m_scanHoldTurnCap;
        for (int i = 0; i < m_scanHoldExtraSweeps; ++i) {
            finalSweepStart = finalSweepEnd;
            finalSweepEnd   = scanLineNextTurn(finalSweepStart + 0.001f);
        }

        // Direction check: cursor must be in the correct direction within
        // the final sweep. For extra sweeps, the user clicks anywhere in
        // the scene Y range — the endpoint's Y is derived from the cursor.
        float sy;
        if (m_scanHoldExtraSweeps == 0) {
            sy = origin.y + m_scanHoldStartY * size.y;
        } else {
            // For multi-sweep, the starting Y of the final sweep is at the
            // turn boundary (0 or 1 depending on direction).
            float turnFrac = finalSweepUp ? 1.f : 0.f;
            sy = origin.y + turnFrac * size.y;
        }
        bool dirOk = finalSweepUp ? (mp.y <= sy + 2.f) : (mp.y >= sy - 2.f);
        if (!dirOk && m_scanHoldExtraSweeps == 0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
            return;
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (lmbClick) {
            // Commit hold
            ImVec2 mpClamped(std::clamp(mp.x, origin.x, pMax.x),
                             std::clamp(mp.y, origin.y, pMax.y));
            float endFrac = localToScan(mpClamped).y;
            if (finalSweepUp) endFrac = std::max(endFrac, 0.f);
            else               endFrac = std::min(endFrac, 1.f);

            float endTime = scanLineTimeForFrac(finalSweepStart, endFrac);
            endTime       = std::min(endTime, finalSweepEnd);

            EditorNote n{};
            n.type            = EditorNoteType::Hold;
            n.time            = m_scanHoldStartT;
            n.endTime         = endTime;
            n.scanX           = m_scanHoldStartX;
            n.scanY           = m_scanHoldStartY;
            n.scanEndY        = endFrac;
            n.scanHoldSweeps  = m_scanHoldExtraSweeps;
            notes().push_back(n);
            m_scanHoldAwaitEnd = false;
        }
        return;
    }

    // ── Slide authoring state ──────────────────────────────────────────────
    // LMB on scan line starts the slide. While LMB is held, RMB clicks
    // place control points (sample/tick nodes). The path is straight lines
    // between consecutive nodes (Cytus-style). Release LMB commits.
    // Each control point after the head is automatically a sample point.
    if (m_scanSlideDragging) {
        // Cancel on ESC
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_scanSlideDragging = false;
            m_scanSlideDraft    = {};
            return;
        }

        if (!lmbDown) {
            // Commit on LMB release if we have at least 2 points (head + 1 node).
            if (m_scanSlideDraft.scanPath.size() >= 2) {
                auto& draft = m_scanSlideDraft;
                float endFrac = draft.scanPath.back().second;
                float endTime = scanLineTimeForFrac(draft.time, endFrac);
                endTime       = std::min(endTime, m_scanSlideTurnCap);
                draft.endTime = endTime;

                // Auto-derive sample points from each control point after the head.
                // Each sample point's time offset = how far along the scan sweep
                // that control point's Y position is.
                draft.samplePoints.clear();
                float total = std::max(0.0001f, draft.endTime - draft.time);
                for (size_t pi = 1; pi < draft.scanPath.size(); ++pi) {
                    float ptFrac = draft.scanPath[pi].second;
                    float ptTime = scanLineTimeForFrac(draft.time, ptFrac);
                    ptTime       = std::min(ptTime, m_scanSlideTurnCap);
                    float offset = std::clamp(ptTime - draft.time, 0.f, total);
                    draft.samplePoints.push_back(offset);
                }
                std::sort(draft.samplePoints.begin(), draft.samplePoints.end());

                notes().push_back(draft);
            }
            m_scanSlideDragging = false;
            m_scanSlideDraft    = {};
            return;
        }

        // RMB click while LMB held → place a new control point / sample node
        if (rmbClick) {
            float mx = std::clamp(mp.x, origin.x, pMax.x);
            float my = std::clamp(mp.y, origin.y, pMax.y);

            // Direction enforcement: control point must continue in the
            // same direction as the scan line sweep (no crossing the turn).
            float sy = origin.y + m_scanSlideDraft.scanPath.back().second * size.y;
            bool dirOk = m_scanSlideGoingUp ? (my <= sy + 1.f) : (my >= sy - 1.f);
            if (!dirOk) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
                return;
            }

            // Clamp so the path doesn't pass the turn-around row.
            if (m_scanSlideGoingUp) my = std::max(my, origin.y);
            else                     my = std::min(my, pMax.y);

            ImVec2 n = localToScan(ImVec2(mx, my));
            m_scanSlideDraft.scanPath.emplace_back(n.x, n.y);
            m_scanSlideLastY = my;
        }

        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        return;
    }

    // ── Idle: show cursor state + handle fresh clicks ──────────────────────
    if (!onLine) {
        // If we're off the scan line and a tool is active, hint NotAllowed.
        if (m_noteTool != NoteTool::None && inRect)
            ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
        return;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (!lmbClick) return;

    ImVec2 mpN = localToScan(mp);
    float mxN = std::clamp(mpN.x, 0.f, 1.f);
    float myN = frac;  // lock Y to scan line

    switch (m_noteTool) {
        case NoteTool::Tap: {
            EditorNote n{};
            n.type  = EditorNoteType::Tap;
            n.time  = curTime;
            n.scanX = mxN;
            n.scanY = myN;
            notes().push_back(n);
            break;
        }
        case NoteTool::Flick: {
            EditorNote n{};
            n.type  = EditorNoteType::Flick;
            n.time  = curTime;
            n.scanX = mxN;
            n.scanY = myN;
            notes().push_back(n);
            break;
        }
        case NoteTool::Hold: {
            m_scanHoldAwaitEnd    = true;
            m_scanHoldStartX      = mxN;
            m_scanHoldStartY      = myN;
            m_scanHoldStartT      = curTime;
            m_scanHoldTurnCap     = scanLineNextTurn(curTime);
            m_scanHoldGoingUp     = scanLineGoingUp(curTime);
            m_scanHoldExtraSweeps = 0;
            break;
        }
        case NoteTool::Slide: {
            m_scanSlideDragging         = true;
            m_scanSlideGoingUp          = scanLineGoingUp(curTime);
            m_scanSlideTurnCap          = scanLineNextTurn(curTime);
            m_scanSlideLastY            = mp.y;
            m_scanSlideDraft            = {};
            m_scanSlideDraft.type       = EditorNoteType::Slide;
            m_scanSlideDraft.time       = curTime;
            m_scanSlideDraft.scanX      = mxN;
            m_scanSlideDraft.scanY      = myN;
            m_scanSlideDraft.scanPath.emplace_back(mxN, myN);
            break;
        }
        case NoteTool::None:
        default:
            // With no tool selected, click on a scan-line note to select it
            // (opens the Note Properties popup). Hit-tests tap/flick heads,
            // hold heads + bodies, and slide paths.
            for (size_t ni = 0; ni < notes().size(); ++ni) {
                const auto& n = notes()[ni];
                ImVec2 head = scanToLocal(n.scanX, n.scanY);
                float  nx = head.x, ny = head.y;
                bool hit = false;

                if (n.type == EditorNoteType::Tap ||
                    n.type == EditorNoteType::Flick) {
                    float dx = mp.x - nx, dy = mp.y - ny;
                    hit = (dx * dx + dy * dy < 14.f * 14.f);
                } else if (n.type == EditorNoteType::Hold) {
                    float ey  = scanToLocal(n.scanX, n.scanEndY).y;
                    float yLo = std::min(ny, ey);
                    float yHi = std::max(ny, ey);
                    hit = (std::abs(mp.x - nx) < 10.f &&
                           mp.y >= yLo - 4.f && mp.y <= yHi + 4.f);
                } else if (n.type == EditorNoteType::Slide) {
                    for (size_t i = 1; i < n.scanPath.size(); ++i) {
                        ImVec2 a = scanToLocal(n.scanPath[i - 1].first,
                                               n.scanPath[i - 1].second);
                        ImVec2 b = scanToLocal(n.scanPath[i].first,
                                               n.scanPath[i].second);
                        float ABx = b.x - a.x, ABy = b.y - a.y;
                        float APx = mp.x - a.x, APy = mp.y - a.y;
                        float len2 = ABx * ABx + ABy * ABy;
                        float u    = len2 > 1e-4f ? (APx * ABx + APy * ABy) / len2 : 0.f;
                        u = std::clamp(u, 0.f, 1.f);
                        float cx = a.x + u * ABx, cy = a.y + u * ABy;
                        float dx = mp.x - cx,     dy = mp.y - cy;
                        if (dx * dx + dy * dy < 64.f) { hit = true; break; }
                    }
                }

                if (hit) {
                    m_selectedNoteIdx = (int)ni;
                    break;
                }
            }
            break;
    }
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
    static ImageEditor s_imageEditor;

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

    // Populate timing points from BPM map
    if (!m_bpmChanges.empty()) {
        for (const auto& bc : m_bpmChanges) {
            TimingPoint tp;
            tp.time  = (double)bc.time;
            tp.bpm   = bc.bpm;
            tp.meter = 4;
            chart.timingPoints.push_back(tp);
        }
    } else if (m_dominantBpm > 0.f) {
        chart.timingPoints.push_back({0.0, m_dominantBpm, 4});
    }

    const auto& edNotes = notes();
    uint32_t id = 0;
    for (const auto& en : edNotes) {
        NoteEvent ev{};
        ev.time = (double)en.time;
        ev.id   = id++;

        int span = std::clamp(en.laneSpan, 1, 3);
        switch (en.type) {
            case EditorNoteType::Tap: {
                ev.type = NoteType::Tap;
                TapData td{};
                td.laneX    = (float)en.track;
                td.laneSpan = span;
                td.scanX    = en.scanX;
                td.scanY    = en.scanY;
                ev.data = std::move(td);
                break;
            }
            case EditorNoteType::Hold: {
                ev.type = NoteType::Hold;
                HoldData hd{};
                hd.laneX    = (float)en.track;
                hd.duration = en.endTime - en.time;
                hd.laneSpan = span;
                hd.scanX           = en.scanX;
                hd.scanY           = en.scanY;
                hd.scanEndY        = en.scanEndY;
                hd.scanHoldSweeps  = en.scanHoldSweeps;

                if (!en.waypoints.empty()) {
                    // New multi-waypoint path
                    hd.waypoints.reserve(en.waypoints.size());
                    for (const auto& w : en.waypoints) {
                        HoldWaypoint hw{};
                        hw.tOffset       = w.tOffset;
                        hw.lane          = w.lane;
                        hw.transitionLen = w.transitionLen;
                        hw.style         = (HoldTransition)(int)w.style;
                        hd.waypoints.push_back(hw);
                    }
                    hd.endLaneX = static_cast<float>(en.waypoints.back().lane);
                } else {
                    // Legacy single-transition (or straight) hold
                    hd.endLaneX = (en.endTrack < 0 || en.endTrack == en.track)
                                  ? -1.f
                                  : (float)en.endTrack;
                    hd.transition      = (HoldTransition)(int)en.transition;
                    hd.transitionLen   = en.transitionLen;
                    hd.transitionStart = en.transitionStart;
                }

                hd.samplePoints.reserve(en.samplePoints.size());
                for (float t : en.samplePoints)
                    hd.samplePoints.push_back({t});
                ev.data = hd;
                break;
            }
            case EditorNoteType::Slide: {
                ev.type = NoteType::Slide;
                TapData td{};
                td.laneX        = (float)en.track;
                td.laneSpan     = span;
                td.scanX        = en.scanX;
                td.scanY        = en.scanY;
                td.scanPath     = en.scanPath;
                td.duration     = std::max(0.f, en.endTime - en.time);
                td.samplePoints = en.samplePoints;
                ev.data = std::move(td);
                break;
            }
            case EditorNoteType::Flick: {
                ev.type = NoteType::Flick;
                FlickData fd{};
                fd.laneX     = (float)en.track;
                fd.direction = 0;
                fd.scanX     = en.scanX;
                fd.scanY     = en.scanY;
                ev.data = std::move(fd);
                break;
            }
        }
        chart.notes.push_back(ev);
    }

    // Sort by time
    std::sort(chart.notes.begin(), chart.notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.time < b.time; });

    // Disk animation (circle mode). Safe to emit for any mode — the
    // runtime simply ignores it when the renderer isn't LanotaRenderer.
    chart.diskAnimation.rotations = diskRot();
    chart.diskAnimation.moves     = diskMove();
    chart.diskAnimation.scales    = diskScale();

    chart.scanSpeedEvents = scanSpeed();

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

        // Write timing points (dynamic BPM)
        if (!chart.timingPoints.empty()) {
            f << " \"timing\": {\n";
            f << "   \"bpm\": " << chart.timingPoints[0].bpm << ",\n";
            if (chart.timingPoints.size() > 1) {
                f << "   \"bpm_changes\": [\n";
                for (size_t i = 0; i < chart.timingPoints.size(); i++) {
                    auto& tp = chart.timingPoints[i];
                    f << "     {\"time\": " << tp.time << ", \"bpm\": " << tp.bpm << "}";
                    if (i + 1 < chart.timingPoints.size()) f << ",";
                    f << "\n";
                }
                f << "   ]\n";
            } else {
                f << "   \"timeSignature\": \"4/4\"\n";
            }
            f << " },\n";
        }

        f << " \"notes\": [\n";
        for (size_t i = 0; i < chart.notes.size(); i++) {
            auto& n = chart.notes[i];
            f << "    {\"time\": " << n.time << ", \"type\": ";
            switch (n.type) {
                case NoteType::Tap:   f << "\"tap\"";   break;
                case NoteType::Hold:  f << "\"hold\"";  break;
                case NoteType::Slide: f << "\"slide\""; break;
                case NoteType::Flick: f << "\"flick\""; break;
                default:              f << "\"tap\"";    break;
            }
            f << ", \"lane\": ";
            int span = 1;
            if (auto* tap = std::get_if<TapData>(&n.data)) {
                f << tap->laneX;
                span = tap->laneSpan;
            } else if (auto* hold = std::get_if<HoldData>(&n.data)) {
                f << hold->laneX;
                span = hold->laneSpan;
            } else if (auto* flick = std::get_if<FlickData>(&n.data)) {
                f << flick->laneX;
            } else {
                f << 0;
            }
            if (n.type == NoteType::Hold) {
                if (auto* hold = std::get_if<HoldData>(&n.data)) {
                    f << ", \"duration\": " << hold->duration;

                    // Multi-waypoint path takes precedence over legacy fields.
                    if (!hold->waypoints.empty()) {
                        f << ", \"waypoints\": [";
                        for (size_t wi = 0; wi < hold->waypoints.size(); ++wi) {
                            const auto& w = hold->waypoints[wi];
                            const char* tname = "curve";
                            switch (w.style) {
                                case HoldTransition::Straight: tname = "straight"; break;
                                case HoldTransition::Angle90:  tname = "angle90";  break;
                                case HoldTransition::Curve:    tname = "curve";    break;
                                case HoldTransition::Rhomboid: tname = "rhomboid"; break;
                            }
                            if (wi) f << ", ";
                            f << "{\"t\": " << w.tOffset
                              << ", \"lane\": " << w.lane
                              << ", \"len\": " << w.transitionLen
                              << ", \"style\": \"" << tname << "\"}";
                        }
                        f << "]";
                    } else if (hold->endLaneX >= 0.f && hold->endLaneX != hold->laneX) {
                        f << ", \"endLane\": " << hold->endLaneX;
                        const char* tname = "straight";
                        switch (hold->transition) {
                            case HoldTransition::Angle90:  tname = "angle90";  break;
                            case HoldTransition::Curve:    tname = "curve";    break;
                            case HoldTransition::Rhomboid: tname = "rhomboid"; break;
                            default: break;
                        }
                        f << ", \"transition\": \"" << tname << "\"";
                        f << ", \"transitionLen\": " << hold->transitionLen;
                        if (hold->transitionStart >= 0.f)
                            f << ", \"transitionStart\": " << hold->transitionStart;
                    }
                    if (!hold->samplePoints.empty()) {
                        f << ", \"samples\": [";
                        for (size_t si = 0; si < hold->samplePoints.size(); ++si) {
                            if (si) f << ", ";
                            f << hold->samplePoints[si].tOffset;
                        }
                        f << "]";
                    }
                }
            }
            if (span != 1) f << ", \"laneSpan\": " << span;

            // ── Slide-specific duration + sample points ───────────────
            if (n.type == NoteType::Slide) {
                if (auto* tap = std::get_if<TapData>(&n.data)) {
                    if (tap->duration > 0.f)
                        f << ", \"duration\": " << tap->duration;
                    if (!tap->samplePoints.empty()) {
                        f << ", \"samples\": [";
                        for (size_t si = 0; si < tap->samplePoints.size(); ++si) {
                            if (si) f << ", ";
                            f << tap->samplePoints[si];
                        }
                        f << "]";
                    }
                }
            }

            // ── Scan-line coordinates (emitted when non-default) ──────
            // Uses a flat "scan" sub-object so old loaders that don't
            // know about it can simply ignore the extra key.
            {
                float sx = 0.f, sy = 0.f, sey = -1.f;
                int holdSweepsVal = 0;
                const std::vector<std::pair<float,float>>* spath = nullptr;
                if (auto* tap = std::get_if<TapData>(&n.data)) {
                    sx = tap->scanX; sy = tap->scanY;
                    if (!tap->scanPath.empty()) spath = &tap->scanPath;
                } else if (auto* hold = std::get_if<HoldData>(&n.data)) {
                    sx = hold->scanX; sy = hold->scanY; sey = hold->scanEndY;
                    holdSweepsVal = hold->scanHoldSweeps;
                } else if (auto* flick = std::get_if<FlickData>(&n.data)) {
                    sx = flick->scanX; sy = flick->scanY;
                }
                bool hasScan = (sx != 0.f || sy != 0.f || sey >= 0.f ||
                                holdSweepsVal > 0 || (spath && !spath->empty()));
                if (hasScan) {
                    f << ", \"scan\": {\"x\": " << sx << ", \"y\": " << sy;
                    if (sey >= 0.f) f << ", \"endY\": " << sey;
                    if (holdSweepsVal > 0) f << ", \"sweeps\": " << holdSweepsVal;
                    if (spath && !spath->empty()) {
                        f << ", \"path\": [";
                        for (size_t pi = 0; pi < spath->size(); ++pi) {
                            if (pi) f << ", ";
                            f << "[" << (*spath)[pi].first << ", "
                                     << (*spath)[pi].second << "]";
                        }
                        f << "]";
                    }
                    f << "}";
                }
            }

            f << "}";
            if (i + 1 < chart.notes.size()) f << ",";
            f << "\n";
        }
        f << "  ]";

        // ── Disk animation (circle mode) ─────────────────────────────
        // Only emit when any keyframe exists; keeps other modes' files clean.
        const auto& da = chart.diskAnimation;
        if (!da.rotations.empty() || !da.moves.empty() || !da.scales.empty()) {
            auto easingName = [](DiskEasing e) {
                switch (e) {
                    case DiskEasing::Linear:     return "linear";
                    case DiskEasing::QuadInOut:  return "quadInOut";
                    case DiskEasing::CubicInOut: return "cubicInOut";
                    case DiskEasing::SineInOut:
                    default:                     return "sineInOut";
                }
            };
            f << ",\n \"diskAnimation\": {\n";
            // rotations
            f << "   \"rotations\": [";
            for (size_t ri = 0; ri < da.rotations.size(); ++ri) {
                const auto& r = da.rotations[ri];
                if (ri) f << ", ";
                f << "{\"startTime\": " << r.startTime
                  << ", \"duration\": " << r.duration
                  << ", \"target\": "   << r.targetAngle
                  << ", \"easing\": \"" << easingName(r.easing) << "\"}";
            }
            f << "],\n";
            // moves
            f << "   \"moves\": [";
            for (size_t mi = 0; mi < da.moves.size(); ++mi) {
                const auto& m = da.moves[mi];
                if (mi) f << ", ";
                f << "{\"startTime\": " << m.startTime
                  << ", \"duration\": " << m.duration
                  << ", \"target\": ["  << m.target.x << ", " << m.target.y << "]"
                  << ", \"easing\": \"" << easingName(m.easing) << "\"}";
            }
            f << "],\n";
            // scales
            f << "   \"scales\": [";
            for (size_t si = 0; si < da.scales.size(); ++si) {
                const auto& s = da.scales[si];
                if (si) f << ", ";
                f << "{\"startTime\": " << s.startTime
                  << ", \"duration\": " << s.duration
                  << ", \"target\": "   << s.targetScale
                  << ", \"easing\": \"" << easingName(s.easing) << "\"}";
            }
            f << "]\n";
            f << " }";
        }

        // ── Scan-line speed events ───────────────────────────────────────
        if (!chart.scanSpeedEvents.empty()) {
            auto easingName2 = [](DiskEasing e) {
                switch (e) {
                    case DiskEasing::Linear:     return "linear";
                    case DiskEasing::QuadInOut:  return "quadInOut";
                    case DiskEasing::CubicInOut: return "cubicInOut";
                    case DiskEasing::SineInOut:
                    default:                     return "sineInOut";
                }
            };
            f << ",\n \"scanSpeedEvents\": [";
            for (size_t si = 0; si < chart.scanSpeedEvents.size(); ++si) {
                const auto& s = chart.scanSpeedEvents[si];
                if (si) f << ", ";
                f << "{\"startTime\": " << s.startTime
                  << ", \"duration\": " << s.duration
                  << ", \"targetSpeed\": " << s.targetSpeed
                  << ", \"easing\": \"" << easingName2(s.easing) << "\"}";
            }
            f << "]";
        }

        f << "\n}\n";
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
            m_holdDragging  = false;
            m_holdLastTrack = -1;
            m_holdDraft     = EditorNote{};
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
                m_holdDragging  = false;
                m_holdLastTrack = -1;
                m_holdDraft     = EditorNote{};
            }
        }
        if (active) ImGui::PopStyleColor(3);
        ImGui::SameLine();
    };

    toolBtn("Marker", NoteTool::None, ImVec4(0.5f, 0.4f, 0.2f, 1.f));
    toolBtn("Click",  NoteTool::Tap, ImVec4(0.2f, 0.5f, 0.8f, 1.f));
    toolBtn("Hold",   NoteTool::Hold, ImVec4(0.2f, 0.7f, 0.3f, 1.f));

    // Slide: not available for ScanLine; always available for 2D, Circle, and 3D ground
    // (3D sky restriction is handled at placement time)
    if (gm.type != GameModeType::ScanLine) {
        toolBtn("Slide", NoteTool::Slide, ImVec4(0.7f, 0.3f, 0.7f, 1.f));
    }

    // Show drag-recording indicator
    if (m_holdDragging && m_noteTool == NoteTool::Hold) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f),
                           "Recording... (drag, release to commit)");
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
                // Ensure absolute path so subprocess can find the file
                try { fullAudioPath = fs::canonical(fullAudioPath).string(); } catch (...) {}
                m_analyzer.setCallback([this](AudioAnalysisResult result) {
                    if (result.success) {
                        m_diffMarkers[(int)Difficulty::Easy]   = std::move(result.easyMarkers);
                        m_diffMarkers[(int)Difficulty::Medium] = std::move(result.mediumMarkers);
                        m_diffMarkers[(int)Difficulty::Hard]   = std::move(result.hardMarkers);
                        m_bpmChanges   = std::move(result.bpmChanges);
                        m_dominantBpm  = result.bpm;

                        // Build status message showing BPM info
                        std::string bpmInfo = "BPM: " + std::to_string((int)result.bpm);
                        if (m_bpmChanges.size() > 1)
                            bpmInfo += " (dynamic: " + std::to_string(m_bpmChanges.size()) + " sections)";
                        m_statusMsg   = "Beats analyzed! " + bpmInfo;
                        m_statusTimer = 5.f;
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
    if (io.KeyCtrl || io.KeyShift || io.KeyAlt) return;

    float mouseX = io.MousePos.x;
    float mouseY = io.MousePos.y;
    float rawTime = startTime + (mouseX - origin.x) / m_timelineZoom;
    float snappedTime = snapToMarker(rawTime);
    int   track = trackFromY(mouseY, regionTop, trackH, trackCount);

    // Reject placement when the target lane is disabled by a disk move
    // keyframe at the click's time.  Only meaningful in Circle mode —
    // laneMaskAt returns all-enabled for everything else.
    if (m_song && m_song->gameMode.type == GameModeType::Circle) {
        if (!isLaneEnabledAt(track, snappedTime)) {
            if (io.MouseClicked[0] || io.MouseClicked[1]) {
                m_statusMsg   = "Lane disabled at this time (disk moved off playable area).";
                m_statusTimer = 3.f;
            }
            return;
        }
    }

    // 3D sky: only Click and Hold
    NoteTool effectiveTool = m_noteTool;
    if (is3DSky && effectiveTool == NoteTool::Slide)
        return; // disallow

    if (effectiveTool == NoteTool::Hold) {
        // ── Drag-to-record ──────────────────────────────────────────────────
        // Press LMB at the start lane, drag through the timeline (each lane
        // crossing pushes a waypoint), release LMB to commit the hold.
        if (io.MouseClicked[0]) {
            // Start a new draft.
            m_holdDragging = true;
            m_holdDraft = EditorNote{};
            m_holdDraft.type     = EditorNoteType::Hold;
            m_holdDraft.time     = snappedTime;
            m_holdDraft.endTime  = snappedTime;
            m_holdDraft.track    = track;
            m_holdDraft.isSky    = is3DSky;
            m_holdDraft.laneSpan = m_defaultLaneSpan;
            m_holdDraft.waypoints.clear();
            m_holdDraft.waypoints.push_back({0.f, track, 0.f, EditorHoldTransition::Curve});
            m_holdLastTrack = track;
        }
        else if (m_holdDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Sample current cursor; advance endTime; push a waypoint when
            // the lane changes.
            float curTime = std::max(m_holdDraft.time, snappedTime);
            m_holdDraft.endTime = curTime;
            if (track != m_holdLastTrack) {
                float tOff = curTime - m_holdDraft.time;
                if (tOff <= 0.f) tOff = 0.0001f;
                // Default each lane-change to an instant snap; the author
                // can stretch transitionLen later in Note Properties.
                m_holdDraft.waypoints.push_back({tOff, track, 0.f, EditorHoldTransition::Curve});
                m_holdLastTrack = track;
            }
        }
        if (m_holdDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            // Commit the draft. A click without drag (no time elapsed and no
            // lane change) is treated as cancel — a hold needs duration.
            m_holdDraft.endTime = std::max(m_holdDraft.endTime, snappedTime);
            float dur = m_holdDraft.endTime - m_holdDraft.time;
            if (dur > 0.001f) {
                // Ensure the last waypoint ends at the hold's tail so the
                // body covers the full duration. If the user finished on the
                // same lane as the previous waypoint, just extend it.
                if (!m_holdDraft.waypoints.empty()) {
                    auto& last = m_holdDraft.waypoints.back();
                    if (last.lane == m_holdLastTrack && last.tOffset < dur) {
                        last.tOffset = dur;
                    } else if (last.tOffset < dur) {
                        m_holdDraft.waypoints.push_back({dur, m_holdLastTrack, 0.f,
                                                          EditorHoldTransition::Curve});
                    }
                }
                // Drop the waypoints vector if it's a degenerate straight hold
                // (single starting waypoint or two same-lane waypoints).
                bool flat = true;
                if (m_holdDraft.waypoints.size() >= 2) {
                    int firstLane = m_holdDraft.waypoints.front().lane;
                    for (auto& w : m_holdDraft.waypoints)
                        if (w.lane != firstLane) { flat = false; break; }
                }
                if (flat) m_holdDraft.waypoints.clear();
                notes().push_back(m_holdDraft);
                if (engine) engine->audio().playClickSfx();
            }
            m_holdDragging  = false;
            m_holdLastTrack = -1;
            m_holdDraft     = EditorNote{};
        }
    } else if (io.MouseClicked[0]) {
        // Tap placement: if the click lands inside an existing Hold's zone
        // (same lane at that moment in time), convert it into a sample point
        // on that hold — Bandori-style inline tick — instead of placing a
        // standalone tap note.
        if (effectiveTool == NoteTool::Tap) {
            auto editorHoldLaneAt = [](const EditorNote& h, float tOff) -> float {
                if (h.waypoints.empty()) {
                    const int endTrk = h.endTrack < 0 ? h.track : h.endTrack;
                    const float duration = std::max(0.f, h.endTime - h.time);
                    if (endTrk == h.track || h.transition == EditorHoldTransition::Straight)
                        return (float)h.track;
                    const float tLen = std::clamp(h.transitionLen, 0.f, duration);
                    const float tBegin = (h.transitionStart < 0.f)
                        ? std::max(0.f, duration - tLen)
                        : std::clamp(h.transitionStart, 0.f, std::max(0.f, duration - tLen));
                    const float tEnd = tBegin + tLen;
                    const float la = (float)h.track, lb = (float)endTrk;
                    if (tLen <= 0.f || tOff <= tBegin) return la;
                    if (tOff >= tEnd)                  return lb;
                    float u = (tOff - tBegin) / tLen;
                    if (h.transition == EditorHoldTransition::Angle90) return lb;
                    if (h.transition == EditorHoldTransition::Curve) {
                        float s = u * u * (3.f - 2.f * u);
                        return la + (lb - la) * s;
                    }
                    return la + (lb - la) * u;
                }
                const auto& wps = h.waypoints;
                if (tOff <= wps.front().tOffset) return (float)wps.front().lane;
                if (tOff >= wps.back().tOffset)  return (float)wps.back().lane;
                for (size_t wi = 1; wi < wps.size(); ++wi) {
                    const auto& a = wps[wi - 1];
                    const auto& b = wps[wi];
                    if (tOff > b.tOffset) continue;
                    float tLen = std::clamp(b.transitionLen, 0.f, b.tOffset - a.tOffset);
                    float tBeg = b.tOffset - tLen;
                    if (tOff <= tBeg || tLen <= 0.f)
                        return tOff >= b.tOffset ? (float)b.lane : (float)a.lane;
                    float u  = (tOff - tBeg) / tLen;
                    float la = (float)a.lane, lb = (float)b.lane;
                    switch (b.style) {
                        case EditorHoldTransition::Angle90: return lb;
                        case EditorHoldTransition::Curve: {
                            float s = u * u * (3.f - 2.f * u);
                            return la + (lb - la) * s;
                        }
                        case EditorHoldTransition::Rhomboid:
                            return la + (lb - la) * u;
                        default: return lb;
                    }
                }
                return (float)wps.back().lane;
            };

            for (auto& h : notes()) {
                if (h.type != EditorNoteType::Hold) continue;
                if (h.isSky != is3DSky) continue;
                if (snappedTime <= h.time || snappedTime >= h.endTime) continue;
                float tOff = snappedTime - h.time;
                float lane = editorHoldLaneAt(h, tOff);
                if ((int)(lane + 0.5f) != track) continue;
                h.samplePoints.push_back(tOff);
                std::sort(h.samplePoints.begin(), h.samplePoints.end());
                if (engine) engine->audio().playClickSfx();
                return;
            }
        }

        // Click or Slide: single click to place
        EditorNote note;
        note.type  = (effectiveTool == NoteTool::Tap) ? EditorNoteType::Tap : EditorNoteType::Slide;
        note.time  = snappedTime;
        note.track = track;
        note.isSky = is3DSky;
        note.laneSpan = m_defaultLaneSpan;
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
            case EditorNoteType::Flick: return IM_COL32(220, 150, 40, 80);
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

    ImGuiIO& io = ImGui::GetIO();
    // Note click-selection opens the Note Properties popup. Allowed in all
    // modes so authors can edit Hold transition/sample points in Bandori too.
    bool clickable = (m_noteTool == NoteTool::None)
                  && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt;

    for (size_t ni = 0; ni < notes().size(); ++ni) {
        const auto& note = notes()[ni];
        if (note.isSky != skyOnly) continue;

        float noteX = origin.x + (note.time - startTime) * pxPerSec;
        if (noteX < origin.x - 60 || noteX > pMaxX + 60) continue;

        float centerY = regionTop + (note.track + 0.5f) * trackH;
        float halfH   = trackH * 0.35f;

        // Circle mode: mark notes that live inside a disabled-lane segment
        // with a thick red outline so the author knows to move them.  The
        // note is still drawn (not deleted) so data isn't lost.
        bool inDisabledRegion = false;
        if (gm.type == GameModeType::Circle) {
            if (!isLaneEnabledAt(note.track, note.time))
                inDisabledRegion = true;
        }
        if (inDisabledRegion) {
            dl->AddRect(ImVec2(noteX - halfH - 3, centerY - halfH - 3),
                        ImVec2(noteX + halfH + 3, centerY + halfH + 3),
                        IM_COL32(255, 60, 60, 240), 3.f, 0, 3.f);
        }

        // Hit-test for selection. For a multi-waypoint Hold the bar crosses
        // multiple track rows, so the Y range spans every waypoint's lane.
        if (clickable && io.MouseClicked[0]) {
            float mx = io.MousePos.x, my = io.MousePos.y;
            float hitX = (note.type == EditorNoteType::Hold)
                ? origin.x + (note.endTime - startTime) * pxPerSec
                : noteX;
            float yLo = centerY - halfH;
            float yHi = centerY + halfH;
            if (note.type == EditorNoteType::Hold) {
                if (!note.waypoints.empty()) {
                    for (const auto& w : note.waypoints) {
                        float cy = regionTop + (w.lane + 0.5f) * trackH;
                        yLo = std::min(yLo, cy - halfH);
                        yHi = std::max(yHi, cy + halfH);
                    }
                } else if (note.endTrack >= 0 && note.endTrack != note.track) {
                    float endCY = regionTop + (note.endTrack + 0.5f) * trackH;
                    yLo = std::min(yLo, endCY - halfH);
                    yHi = std::max(yHi, endCY + halfH);
                }
            }
            if (mx >= noteX - halfH && mx <= hitX + halfH &&
                my >= yLo && my <= yHi) {
                m_selectedNoteIdx = (int)ni;
            }
        }

        // Judgment bands at the note start
        drawJudgmentBands(noteX, centerY, halfH, note.type);

        if (note.type == EditorNoteType::Hold) {
            float endX = origin.x + (note.endTime - startTime) * pxPerSec;
            const int endTrk = note.effectiveEndTrack();
            float endCenterY = regionTop + (endTrk + 0.5f) * trackH;

            // Judgment bands at the note end (on the end track)
            drawJudgmentBands(endX, endCenterY, halfH, note.type);

            // Hold bar body — for cross-lane holds, sample the transition and
            // draw short segments so the body visibly moves from start track
            // to end track using the authored style.
            const ImU32 fill   = IM_COL32(50, 180, 80, 120);
            const ImU32 stroke = IM_COL32(80, 220, 100, 220);
            const float barHalf = halfH * 0.6f;
            const float duration = note.endTime - note.time;

            // ── Multi-waypoint path ──────────────────────────────────────────
            if (!note.waypoints.empty()) {
                auto laneToY = [&](int lane) {
                    return regionTop + (lane + 0.5f) * trackH;
                };
                // Walk each waypoint segment a → b. Within each segment:
                //  - From a.tOffset to (b.tOffset - b.transitionLen): straight at lane a
                //  - Transition window ending at b.tOffset using b.style
                for (size_t wi = 1; wi < note.waypoints.size(); ++wi) {
                    const auto& a = note.waypoints[wi - 1];
                    const auto& b = note.waypoints[wi];
                    float yA = laneToY(a.lane);
                    float yB = laneToY(b.lane);
                    float xA = noteX + (a.tOffset / std::max(0.001f, duration)) * (endX - noteX);
                    float xB = noteX + (b.tOffset / std::max(0.001f, duration)) * (endX - noteX);
                    float tLen = std::clamp(b.transitionLen, 0.f, b.tOffset - a.tOffset);
                    float xTransBeg = xB - (tLen / std::max(0.001f, duration)) * (endX - noteX);

                    // Straight portion at lane a
                    if (xTransBeg > xA) {
                        dl->AddRectFilled(ImVec2(xA, yA - barHalf),
                                          ImVec2(xTransBeg, yA + barHalf),
                                          fill, 3.f);
                        dl->AddRect(ImVec2(xA, yA - barHalf),
                                    ImVec2(xTransBeg, yA + barHalf),
                                    stroke, 3.f, 0, 1.5f);
                    }
                    // Transition window
                    if (tLen > 0.f && a.lane != b.lane) {
                        const int N = 16;
                        std::vector<ImVec2> topPts, botPts;
                        topPts.reserve(N + 1); botPts.reserve(N + 1);
                        for (int i = 0; i <= N; ++i) {
                            float u = (float)i / (float)N;
                            float px = xTransBeg + (xB - xTransBeg) * u;
                            float py;
                            switch (b.style) {
                                case EditorHoldTransition::Angle90:
                                    py = (i == N) ? yB : yA;
                                    break;
                                case EditorHoldTransition::Curve: {
                                    float s = u * u * (3.f - 2.f * u);
                                    py = yA + (yB - yA) * s;
                                    break;
                                }
                                case EditorHoldTransition::Rhomboid:
                                    py = yA + (yB - yA) * u;
                                    break;
                                default:
                                    py = yA + (yB - yA) * u;
                                    break;
                            }
                            float extra = 0.f;
                            if (b.style == EditorHoldTransition::Rhomboid) {
                                float tri = 1.f - std::abs(2.f * u - 1.f);
                                extra = tri * std::abs(yB - yA) * 0.5f;
                            }
                            topPts.push_back(ImVec2(px, py - barHalf - extra));
                            botPts.push_back(ImVec2(px, py + barHalf + extra));
                        }
                        for (int i = 0; i < N; ++i) {
                            dl->AddQuadFilled(topPts[i], topPts[i + 1],
                                              botPts[i + 1], botPts[i], fill);
                        }
                        dl->AddPolyline(topPts.data(), N + 1, stroke, 0, 1.5f);
                        dl->AddPolyline(botPts.data(), N + 1, stroke, 0, 1.5f);
                    } else if (a.lane != b.lane) {
                        // Instant snap (no transitionLen): vertical connector
                        dl->AddLine(ImVec2(xB, yA), ImVec2(xB, yB), stroke, 1.5f);
                    }
                }
            } else if (endTrk == note.track) {
                // Straight: single rect.
                dl->AddRectFilled(ImVec2(noteX, centerY - barHalf),
                                  ImVec2(endX,  centerY + barHalf),
                                  fill, 3.f);
                dl->AddRect(ImVec2(noteX, centerY - barHalf),
                            ImVec2(endX,  centerY + barHalf),
                            stroke, 3.f, 0, 1.5f);
            } else {
                // Cross-lane: sample the transition and fill a polygon strip.
                const int N = 24;
                const float duration = note.endTime - note.time;
                const float tLen   = std::clamp(note.transitionLen, 0.f, duration);
                const float tBegin = (note.transitionStart < 0.f)
                                         ? std::max(0.f, duration - tLen)
                                         : std::clamp(note.transitionStart, 0.f, std::max(0.f, duration - tLen));
                const float tEnd   = tBegin + tLen;
                const float startY = centerY;
                const float endY   = endCenterY;
                const float dY     = endY - startY;

                auto yAt = [&](float tOff) -> float {
                    if (note.transition == EditorHoldTransition::Straight
                        || tLen <= 0.f || duration <= 0.f)
                        return startY;
                    if (tOff <= tBegin) return startY;
                    if (tOff >= tEnd)   return endY;
                    float u = (tOff - tBegin) / tLen;
                    switch (note.transition) {
                        case EditorHoldTransition::Angle90:
                            return endY;
                        case EditorHoldTransition::Curve: {
                            float s = u * u * (3.f - 2.f * u);
                            return startY + dY * s;
                        }
                        case EditorHoldTransition::Rhomboid:
                            return startY + dY * u;
                        default:
                            return startY;
                    }
                };
                // Rhomboid gets an inflated body around the transition midpoint.
                auto extraHalfAt = [&](float tOff) -> float {
                    if (note.transition != EditorHoldTransition::Rhomboid
                        || tLen <= 0.f)
                        return 0.f;
                    if (tOff <= tBegin || tOff >= tEnd) return 0.f;
                    float u = (tOff - tBegin) / tLen;
                    float tri = 1.f - std::abs(2.f * u - 1.f);
                    return tri * std::abs(dY) * 0.5f;
                };

                // Build top/bottom polylines, then draw connected quads.
                std::vector<ImVec2> topPts, botPts;
                topPts.reserve(N + 1); botPts.reserve(N + 1);
                for (int i = 0; i <= N; ++i) {
                    float tOff = (float)i / (float)N * duration;
                    float px = noteX + (tOff / std::max(0.001f, duration)) * (endX - noteX);
                    float cy = yAt(tOff);
                    float hh = barHalf + extraHalfAt(tOff);
                    topPts.push_back(ImVec2(px, cy - hh));
                    botPts.push_back(ImVec2(px, cy + hh));
                }
                for (int i = 0; i < N; ++i) {
                    dl->AddQuadFilled(topPts[i], topPts[i+1],
                                      botPts[i+1], botPts[i], fill);
                }
                // Stroke the top and bottom silhouettes
                dl->AddPolyline(topPts.data(), N + 1, stroke, 0, 1.5f);
                dl->AddPolyline(botPts.data(), N + 1, stroke, 0, 1.5f);
            }

            // Start cap on the starting track
            dl->AddRectFilled(ImVec2(noteX - 3, centerY - halfH),
                              ImVec2(noteX + 3, centerY + halfH),
                              IM_COL32(80, 220, 100, 255), 2.f);
            // End cap on the ending track (may be a different lane)
            dl->AddRectFilled(ImVec2(endX - 3, endCenterY - halfH),
                              ImVec2(endX + 3, endCenterY + halfH),
                              IM_COL32(80, 220, 100, 255), 2.f);

            // Sample-point markers: small yellow dots on the body.
            for (float sp : note.samplePoints) {
                if (sp <= 0.f || sp >= duration) continue;
                float tx = noteX + (sp / std::max(0.001f, duration)) * (endX - noteX);
                float ty;
                if (!note.waypoints.empty()) {
                    // Build a temporary HoldData proxy and reuse evalHoldLaneAt
                    HoldData proxy{};
                    proxy.laneX    = (float)note.track;
                    proxy.duration = duration;
                    proxy.waypoints.reserve(note.waypoints.size());
                    for (const auto& w : note.waypoints) {
                        HoldWaypoint hw{};
                        hw.tOffset       = w.tOffset;
                        hw.lane          = w.lane;
                        hw.transitionLen = w.transitionLen;
                        hw.style         = (HoldTransition)(int)w.style;
                        proxy.waypoints.push_back(hw);
                    }
                    float lane = evalHoldLaneAt(proxy, sp);
                    ty = regionTop + (lane + 0.5f) * trackH;
                } else if (endTrk == note.track) {
                    ty = centerY;
                } else {
                    // Reuse the same interpolation as the legacy body
                    const float tLen   = std::clamp(note.transitionLen, 0.f, duration);
                    const float tBegin = (note.transitionStart < 0.f)
                                            ? std::max(0.f, duration - tLen)
                                            : std::clamp(note.transitionStart, 0.f, std::max(0.f, duration - tLen));
                    const float tEnd   = tBegin + tLen;
                    const float dY     = endCenterY - centerY;
                    if (note.transition == EditorHoldTransition::Straight || tLen <= 0.f)
                        ty = centerY;
                    else if (sp <= tBegin) ty = centerY;
                    else if (sp >= tEnd)   ty = endCenterY;
                    else {
                        float u = (sp - tBegin) / tLen;
                        if (note.transition == EditorHoldTransition::Angle90)
                            ty = endCenterY;
                        else if (note.transition == EditorHoldTransition::Curve) {
                            float s = u * u * (3.f - 2.f * u);
                            ty = centerY + dY * s;
                        } else {
                            ty = centerY + dY * u;
                        }
                    }
                }
                dl->AddCircleFilled(ImVec2(tx, ty), 4.f, IM_COL32(255, 230, 80, 220));
                dl->AddCircle     (ImVec2(tx, ty), 5.f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
            }

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

        // Selection outline around the whole note row segment plus a width tag.
        if ((int)ni == m_selectedNoteIdx) {
            float endX = (note.type == EditorNoteType::Hold)
                ? origin.x + (note.endTime - startTime) * pxPerSec
                : noteX;
            dl->AddRect(ImVec2(noteX - halfH - 2, centerY - halfH - 2),
                        ImVec2(endX  + halfH + 2, centerY + halfH + 2),
                        IM_COL32(255, 220, 80, 255), 3.f, 0, 2.f);
            char tag[8];
            snprintf(tag, sizeof(tag), "x%d", note.laneSpan);
            dl->AddText(ImVec2(noteX - halfH, centerY - halfH - 14),
                        IM_COL32(255, 220, 80, 255), tag);
        }
    }
}

// ── Disk animation lane-mask helpers (Circle mode) ───────────────────────────
//
// When a move keyframe drifts the disk off the playable region, some angular
// lanes become unreachable.  The editor queries `laneMaskAt(time)` to gray
// out those lanes on the timeline and to reject new note placement on them.
//
// The mask is a 32-bit bitfield (one bit per lane).  We rebuild a piecewise-
// constant timeline lazily — any keyframe edit sets m_laneMaskDirty, and the
// next renderChartTimeline call recomputes it.  Sampling is done by walking
// the union of keyframe start/end times; between samples we assume the mask
// is constant (keyframes are piecewise-easy but the reachability boundary
// is monotonic within a segment, so sampling the endpoints catches when a
// lane crosses the boundary with good enough fidelity for editor visuals).

void SongEditor::rebuildLaneMaskTimeline() {
    m_laneMaskTimeline.clear();
    m_laneMaskDirty = false;
    if (!m_song) return;
    const GameModeConfig& gm = m_song->gameMode;
    if (gm.type != GameModeType::Circle) return;

    // Collect all interesting sample times: 0, every keyframe start, every
    // keyframe end (start + duration).  Sort + unique.  Between adjacent
    // samples the mask is treated as constant (start-of-interval value).
    std::vector<double> samples;
    samples.push_back(0.0);
    auto addTimes = [&](auto& list) {
        for (auto& e : list) {
            samples.push_back(e.startTime);
            samples.push_back(e.startTime + e.duration + 1e-4);
        }
    };
    addTimes(diskRot());
    addTimes(diskMove());
    addTimes(diskScale());
    std::sort(samples.begin(), samples.end());
    samples.erase(std::unique(samples.begin(), samples.end()), samples.end());

    int tc = gm.trackCount;
    for (double t : samples) {
        glm::vec2 c    = sampleDiskCenter  (t, diskMove());
        float     s    = sampleDiskScale   (t, diskScale());
        float     r    = sampleDiskRotation(t, diskRot());
        uint32_t  mask = laneMaskForTransform(tc, c, s, r);
        if (m_laneMaskTimeline.empty() || m_laneMaskTimeline.back().mask != mask)
            m_laneMaskTimeline.push_back({t, mask});
    }
    if (m_laneMaskTimeline.empty())
        m_laneMaskTimeline.push_back({0.0, laneMaskForTransform(tc, {0.f,0.f}, 1.f, 0.f)});
}

uint32_t SongEditor::laneMaskAt(double songTime) const {
    if (m_laneMaskTimeline.empty()) return 0xFFFFFFFFu;
    if (songTime <= m_laneMaskTimeline.front().startTime)
        return m_laneMaskTimeline.front().mask;
    for (size_t i = 1; i < m_laneMaskTimeline.size(); ++i)
        if (m_laneMaskTimeline[i].startTime > songTime)
            return m_laneMaskTimeline[i - 1].mask;
    return m_laneMaskTimeline.back().mask;
}

bool SongEditor::isLaneEnabledAt(int lane, double songTime) const {
    if (lane < 0 || lane >= 32) return true;
    return (laneMaskAt(songTime) & (1u << lane)) != 0;
}
