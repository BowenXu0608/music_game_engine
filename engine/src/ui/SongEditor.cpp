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

    // Chart file naming: <name>_<modeKey>_<difficulty>.json so every
    // (mode, difficulty) pair gets its own independent chart file.
    const char* modeKey(const GameModeConfig& gm) {
        switch (gm.type) {
            case GameModeType::DropNotes:
                return (gm.dimension == DropDimension::ThreeD) ? "drop3d" : "drop2d";
            case GameModeType::Circle:   return "circle";
            case GameModeType::ScanLine: return "scan";
        }
        return "unknown";
    }

    std::string chartRelPathFor(const std::string& songName,
                                const GameModeConfig& gm,
                                const char* diffSuffix) {
        return std::string("assets/charts/") + songName + "_"
             + modeKey(gm) + "_" + diffSuffix + ".json";
    }

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

    // Reachability predicate bounds: LanotaRenderer uses FOV_Y=60° with eye
    // at z=4, giving a fixed visible rect of ~±3.0 × ±2.31 at the z=0 hit
    // plane. A lane's hit point is reachable only if it lands inside that
    // rect — the bound must NOT grow with the disk radius, otherwise scaling
    // the disk up also scales the "playable" window and nothing is ever
    // gated. A small +0.15 margin prevents the default ring (r≈2.4) from
    // clipping its top/bottom lanes right at the boundary.
    constexpr float kFovHalfX = 3.0f;
    constexpr float kFovHalfY = 2.31f;

    uint32_t laneMaskForTransform(int trackCount,
                                  float outerR,
                                  const glm::vec2& center,
                                  float scale,
                                  float rotation) {
        if (trackCount <= 0) return 0xFFFFFFFFu;
        const float r = outerR * scale;
        const float halfX = kFovHalfX + 0.15f;
        const float halfY = kFovHalfY + 0.15f;
        uint32_t mask = 0;
        const int laneLimit = std::min(trackCount, 32);
        for (int lane = 0; lane < laneLimit; ++lane) {
            float a = kPi_ * 0.5f - (static_cast<float>(lane) / trackCount) * (kPi_ * 2.f) + rotation;
            float lx = center.x + std::cos(a) * r;
            float ly = center.y + std::sin(a) * r;
            if (std::abs(lx) > halfX || std::abs(ly) > halfY) continue;
            mask |= (1u << lane);
        }
        // Lanes 32+ can't fit in a uint32_t mask — treat them as always enabled
        // so high-track-count circle charts aren't silently gated.
        if (trackCount > 32) mask |= 0xFFFFFFFFu;
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

    // Prefer mode-keyed charts; each (mode, difficulty) is fully independent.
    reloadChartsForCurrentMode();
}

void SongEditor::loadChartFile(Difficulty diff, const std::string& chartRel) {
    if (!m_song || chartRel.empty()) return;
    {
        std::string fullPath = m_projectPath + "/" + chartRel;
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
                } else if (n.type == NoteType::Arc) {
                    en.type = EditorNoteType::Arc;
                    if (auto* arc = std::get_if<ArcData>(&n.data)) {
                        en.arcStartX = arc->startPos.x;
                        en.arcStartY = arc->startPos.y;
                        en.arcEndX   = arc->endPos.x;
                        en.arcEndY   = arc->endPos.y;
                        en.endTime   = en.time + arc->duration;
                        en.arcEaseX  = arc->curveXEase;
                        en.arcEaseY  = arc->curveYEase;
                        en.arcColor  = arc->color;
                        en.arcIsVoid = arc->isVoid;
                    }
                } else if (n.type == NoteType::ArcTap) {
                    en.type = EditorNoteType::ArcTap;
                    en.arcTapParent = -1; // resolved below
                    if (auto* tap = std::get_if<TapData>(&n.data)) {
                        en.arcStartX = tap->laneX;  // store position for matching
                        en.arcStartY = tap->scanY;
                    }
                } else {
                    en.type = EditorNoteType::Tap;
                }
                edNotes.push_back(en);
            }

            // ── Reconstruct ArcTap → Arc parent linkage ─────────────────
            // For each ArcTap, find the nearest Arc whose time range contains it.
            for (size_t i = 0; i < edNotes.size(); ++i) {
                auto& atn = edNotes[i];
                if (atn.type != EditorNoteType::ArcTap) continue;
                int bestArc = -1;
                float bestDist = 1e9f;
                for (size_t j = 0; j < edNotes.size(); ++j) {
                    const auto& arc = edNotes[j];
                    if (arc.type != EditorNoteType::Arc) continue;
                    if (atn.time < arc.time - 0.001f || atn.time > arc.endTime + 0.001f)
                        continue;
                    // Evaluate arc position at arctap time and compare
                    float dur = arc.endTime - arc.time;
                    float tP = (dur > 0.0001f) ? (atn.time - arc.time) / dur : 0.f;
                    glm::vec2 pos = evalArcEditor(arc, std::clamp(tP, 0.f, 1.f));
                    float dist = std::abs(pos.x - atn.arcStartX)
                               + std::abs(pos.y - atn.arcStartY);
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestArc  = (int)j;
                    }
                }
                atn.arcTapParent = bestArc;
            }

            // ── Auto-merge consecutive connected arc segments ──────────
            // If arc B starts where arc A ends (same color, positions match,
            // time matches), merge them into a single multi-waypoint arc.
            {
                constexpr float kTimeTol = 0.005f;
                constexpr float kPosTol  = 0.01f;
                std::vector<bool> merged(edNotes.size(), false);
                for (size_t i = 0; i < edNotes.size(); ++i) {
                    if (merged[i]) continue;
                    auto& base = edNotes[i];
                    if (base.type != EditorNoteType::Arc) continue;

                    // Initialize waypoints for this arc if not yet multi-waypoint
                    if (base.arcWaypoints.empty()) {
                        ArcWaypoint w0, w1;
                        w0.time = base.time; w0.x = base.arcStartX; w0.y = base.arcStartY;
                        w1.time = base.endTime; w1.x = base.arcEndX; w1.y = base.arcEndY;
                        w1.easeX = base.arcEaseX; w1.easeY = base.arcEaseY;
                        base.arcWaypoints = {w0, w1};
                    }

                    // Greedily chain subsequent arcs
                    bool found = true;
                    while (found) {
                        found = false;
                        const auto& lastWp = base.arcWaypoints.back();
                        for (size_t j = 0; j < edNotes.size(); ++j) {
                            if (j == i || merged[j]) continue;
                            auto& cand = edNotes[j];
                            if (cand.type != EditorNoteType::Arc) continue;
                            if (cand.arcColor != base.arcColor) continue;
                            if (std::abs(cand.time - lastWp.time) > kTimeTol) continue;
                            if (std::abs(cand.arcStartX - lastWp.x) > kPosTol) continue;
                            if (std::abs(cand.arcStartY - lastWp.y) > kPosTol) continue;

                            // Merge: append cand's end as a new waypoint
                            ArcWaypoint wp;
                            wp.time  = cand.endTime;
                            wp.x     = cand.arcEndX;
                            wp.y     = cand.arcEndY;
                            wp.easeX = cand.arcEaseX;
                            wp.easeY = cand.arcEaseY;
                            base.arcWaypoints.push_back(wp);
                            base.endTime = wp.time;
                            base.arcEndX = wp.x;
                            base.arcEndY = wp.y;
                            merged[j] = true;

                            // Update any ArcTap parents pointing to j
                            for (auto& n : edNotes) {
                                if (n.type == EditorNoteType::ArcTap && n.arcTapParent == (int)j)
                                    n.arcTapParent = (int)i;
                            }
                            found = true;
                            break;
                        }
                    }
                }
                // Remove merged arcs
                std::vector<EditorNote> cleaned;
                std::vector<int> indexMap(edNotes.size(), -1);
                for (size_t i = 0; i < edNotes.size(); ++i) {
                    if (!merged[i]) {
                        indexMap[i] = (int)cleaned.size();
                        cleaned.push_back(std::move(edNotes[i]));
                    }
                }
                // Fixup ArcTap parent indices
                for (auto& n : cleaned) {
                    if (n.type == EditorNoteType::ArcTap && n.arcTapParent >= 0) {
                        n.arcTapParent = indexMap[n.arcTapParent];
                    }
                }
                edNotes = std::move(cleaned);
            }

            // Disk animation (circle mode) — copy into the per-difficulty
            // editor state so the author can edit existing keyframes.
            m_diffDiskRot  [(int)diff] = chart.diskAnimation.rotations;
            m_diffDiskMove [(int)diff] = chart.diskAnimation.moves;
            m_diffDiskScale[(int)diff] = chart.diskAnimation.scales;
            m_diffScanSpeed[(int)diff] = chart.scanSpeedEvents;
            m_diffScanPages[(int)diff] = chart.scanPageOverrides;
            // Beat markers (saved per chart file for every mode).
            if (!chart.markers.empty())
                m_diffMarkers[(int)diff] = chart.markers;
            m_laneMaskDirty = true;
            m_scanPhaseDirty = true;
            m_scanPageTableDirty = true;

            std::cout << "[SongEditor] Loaded " << edNotes.size() << " notes from " << chartRel << "\n";
        } catch (...) {
            // Chart file doesn't exist or can't be parsed — start empty
        }
    }
}

void SongEditor::reloadChartsForCurrentMode() {
    if (!m_song) return;

    // Discard any in-memory notes/markers from the previous mode so a
    // fresh (mode, difficulty) pair always starts from its own file or empty.
    m_diffNotes.clear();
    m_diffMarkers.clear();
    m_diffDiskRot.clear();
    m_diffDiskMove.clear();
    m_diffDiskScale.clear();
    m_diffScanSpeed.clear();
    m_diffScanPages.clear();
    m_bpmChanges.clear();
    m_dominantBpm = 0.f;
    m_laneMaskDirty = true;
    m_scanPhaseDirty = true;
    m_scanPageTableDirty = true;
    m_scanCurrentPage = 0;

    const char* diffSuffix[] = {"easy", "medium", "hard"};
    Difficulty  diffs[]      = {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard};
    for (int d = 0; d < 3; ++d) {
        std::string rel = chartRelPathFor(m_song->name, m_song->gameMode, diffSuffix[d]);
        std::string full = m_projectPath + "/" + rel;
        if (fs::exists(full)) {
            loadChartFile(diffs[d], rel);
            switch (diffs[d]) {
                case Difficulty::Easy:   m_song->chartEasy   = rel; break;
                case Difficulty::Medium: m_song->chartMedium = rel; break;
                case Difficulty::Hard:   m_song->chartHard   = rel; break;
            }
        } else {
            // No file yet for this (mode, diff) — clear any stale SongInfo path.
            switch (diffs[d]) {
                case Difficulty::Easy:   m_song->chartEasy.clear();   break;
                case Difficulty::Medium: m_song->chartMedium.clear(); break;
                case Difficulty::Hard:   m_song->chartHard.clear();   break;
            }
        }
    }
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
        const bool is3DMode = (m_song->gameMode.type == GameModeType::DropNotes
                               && m_song->gameMode.dimension == DropDimension::ThreeD);
        const float arcHeightH = is3DMode ? m_heightCurveH : 0.f;
        float editableH = std::max(80.f, bodyH - waveformH - splitterThick - arcHeightH);
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

                // Marker (pointer) tool: click to select existing notes.
                {
                    bool on = (m_noteTool == NoteTool::None);
                    if (on) {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.45f, 0.25f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.55f, 0.30f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.35f, 0.20f, 1.f));
                    }
                    if (ImGui::Button("Select", ImVec2(58, 0))) {
                        m_noteTool = NoteTool::None;
                        m_scanHoldAwaitEnd  = false;
                        m_scanSlideDragging = false;
                    }
                    if (on) ImGui::PopStyleColor(3);
                    ImGui::SameLine();
                }

                // Beat-analysis block (mirrors renderNoteToolbar's version).
                ImGui::SameLine(0.f, 20.f);
                if (m_analyzer.isRunning()) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.f));
                    ImGui::Button("Analyzing...", ImVec2(100, 0));
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.5f, 0.2f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f,  0.6f, 0.25f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.1f,  0.4f, 0.15f, 1.f));
                    if (ImGui::Button("Analyze Beats", ImVec2(110, 0))) {
                        if (m_song && !m_song->audioFile.empty()) {
                            std::string fullAudioPath = m_projectPath + "/" + m_song->audioFile;
                            try { fullAudioPath = fs::canonical(fullAudioPath).string(); } catch (...) {}
                            m_analyzer.setCallback([this](AudioAnalysisResult result) {
                                if (result.success) {
                                    m_diffMarkers[(int)Difficulty::Easy]   = std::move(result.easyMarkers);
                                    m_diffMarkers[(int)Difficulty::Medium] = std::move(result.mediumMarkers);
                                    m_diffMarkers[(int)Difficulty::Hard]   = std::move(result.hardMarkers);
                                    m_bpmChanges   = std::move(result.bpmChanges);
                                    m_dominantBpm  = result.bpm;
                                    m_scanPageTableDirty = true;  // re-derive page durations from detected BPM
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
                if (ImGui::Button("Clear Markers", ImVec2(110, 0))) {
                    markers().clear();
                }
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

        // ── Arc Height Curve Editor (3D mode only) ─────────────────────────
        if (is3DMode && arcHeightH > 0.f) {
            ImGui::BeginChild("SEArcHeight", ImVec2(0, arcHeightH), true,
                              ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                ImVec2 hOrigin = ImGui::GetCursorScreenPos();
                ImVec2 hSize(avail.x, avail.y);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                renderArcHeightEditor(dl, hOrigin, hSize);
                ImGui::Dummy(hSize);
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
                (sel.type == EditorNoteType::Tap)    ? "Tap"    :
                (sel.type == EditorNoteType::Hold)   ? "Hold"   :
                (sel.type == EditorNoteType::Flick)  ? "Flick"  :
                (sel.type == EditorNoteType::Arc)    ? "Arc"    :
                (sel.type == EditorNoteType::ArcTap) ? "ArcTap" : "Slide";
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

            // ── Arc properties ──────────────────────────────────────────────
            if (sel.type == EditorNoteType::Arc) {
                ImGui::Text("Duration: %.3f s", sel.endTime - sel.time);
                ImGui::Text("Waypoints: %d", (int)sel.arcWaypoints.size());
                ImGui::Separator();

                ImGui::Text("Appearance");
                bool isCyan = (sel.arcColor == 0);
                if (ImGui::RadioButton("Cyan (0)##arc", isCyan))  sel.arcColor = 0;
                ImGui::SameLine();
                if (ImGui::RadioButton("Pink (1)##arc", !isCyan)) sel.arcColor = 1;
                ImGui::Checkbox("Void (no input)##arc", &sel.arcIsVoid);
                ImGui::Spacing();
                ImGui::Separator();

                // Easing names/values shared by all waypoint combos
                const char* easeNames[] = {
                    "s (linear)", "b (bezier)", "si (sine-in)", "so (sine-out)",
                    "sisi", "siso", "sosi", "soso"
                };
                const float easeVals[] = { 0.f, 1.f, 2.f, -2.f, 3.f, -3.f, 4.f, -4.f };
                constexpr int easeCount = 8;
                auto findEaseIdx = [&](float v) -> int {
                    for (int i = 0; i < easeCount; ++i)
                        if (easeVals[i] == v) return i;
                    return 0;
                };

                if (sel.arcWaypoints.size() >= 2) {
                    // ── Waypoint table ──────────────────────────────────────
                    ImGui::Text("Waypoints (drag heights in Height panel)");
                    int removeWp = -1;
                    for (int wi = 0; wi < (int)sel.arcWaypoints.size(); ++wi) {
                        auto& wp = sel.arcWaypoints[wi];
                        ImGui::PushID(wi);
                        ImGui::Text("#%d  t=%.3fs  X=%.2f  Y=%.2f", wi, wp.time, wp.x, wp.y);

                        // Per-segment easing (segment from previous to this waypoint)
                        if (wi > 0) {
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(100);
                            int exIdx = findEaseIdx(wp.easeX);
                            if (ImGui::Combo("eX##wp", &exIdx, easeNames, easeCount))
                                wp.easeX = easeVals[exIdx];
                            ImGui::SameLine();
                            ImGui::SetNextItemWidth(100);
                            int eyIdx = findEaseIdx(wp.easeY);
                            if (ImGui::Combo("eY##wp", &eyIdx, easeNames, easeCount))
                                wp.easeY = easeVals[eyIdx];
                        }

                        // Delete waypoint (only if >2 remain)
                        if (sel.arcWaypoints.size() > 2 && wi > 0
                            && wi < (int)sel.arcWaypoints.size() - 1) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("x##wp")) removeWp = wi;
                        }
                        ImGui::PopID();
                    }
                    if (removeWp >= 0) {
                        sel.arcWaypoints.erase(sel.arcWaypoints.begin() + removeWp);
                        // Sync times
                        sel.time    = sel.arcWaypoints.front().time;
                        sel.endTime = sel.arcWaypoints.back().time;
                        sel.arcStartX = sel.arcWaypoints.front().x;
                        sel.arcStartY = sel.arcWaypoints.front().y;
                        sel.arcEndX   = sel.arcWaypoints.back().x;
                        sel.arcEndY   = sel.arcWaypoints.back().y;
                    }

                    // Button to convert to legacy (flatten to 2 endpoints)
                    if (ImGui::Button("Flatten to 2 endpoints##arc")) {
                        ArcWaypoint first = sel.arcWaypoints.front();
                        ArcWaypoint last  = sel.arcWaypoints.back();
                        sel.arcWaypoints.clear();
                        sel.arcStartX = first.x; sel.arcStartY = first.y;
                        sel.arcEndX   = last.x;  sel.arcEndY   = last.y;
                        sel.arcEaseX  = 0.f;     sel.arcEaseY  = 0.f;
                    }
                } else {
                    // ── Legacy 2-endpoint editing ───────────────────────────
                    ImGui::Text("Position (0..1)");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("Start X##arc", &sel.arcStartX, 0.f, 1.f, "%.3f");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("End X##arc",   &sel.arcEndX,   0.f, 1.f, "%.3f");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("Start Y##arc", &sel.arcStartY, 0.f, 1.f, "%.3f");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("End Y##arc",   &sel.arcEndY,   0.f, 1.f, "%.3f");
                    ImGui::Spacing();

                    ImGui::Text("Easing");
                    int xIdx = findEaseIdx(sel.arcEaseX);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("X Easing##arc", &xIdx, easeNames, easeCount))
                        sel.arcEaseX = easeVals[xIdx];
                    int yIdx = findEaseIdx(sel.arcEaseY);
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("Y Easing##arc", &yIdx, easeNames, easeCount))
                        sel.arcEaseY = easeVals[yIdx];

                    // Button to convert legacy to multi-waypoint
                    if (ImGui::Button("Convert to waypoints##arc")) {
                        ensureArcWaypoints(sel);
                    }
                }
                ImGui::Spacing();
                ImGui::Separator();

                // List child ArcTaps
                ImGui::Text("ArcTaps on this arc:");
                bool hasChildren = false;
                for (size_t i = 0; i < notes().size(); ++i) {
                    if (notes()[i].type == EditorNoteType::ArcTap
                        && notes()[i].arcTapParent == m_selectedNoteIdx) {
                        hasChildren = true;
                        ImGui::PushID((int)i);
                        ImGui::BulletText("t = %.3f s", notes()[i].time);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Select"))
                            m_selectedNoteIdx = (int)i;
                        ImGui::PopID();
                    }
                }
                if (!hasChildren)
                    ImGui::TextDisabled("(none)");
                ImGui::Spacing();
                ImGui::Separator();
            }

            // ── ArcTap properties ──────────────────────────────────────────
            if (sel.type == EditorNoteType::ArcTap) {
                if (sel.arcTapParent >= 0 && sel.arcTapParent < (int)notes().size()) {
                    const auto& parent = notes()[sel.arcTapParent];
                    if (parent.type == EditorNoteType::Arc) {
                        float dur = parent.endTime - parent.time;
                        float tP = std::clamp((sel.time - parent.time) / std::max(0.001f, dur), 0.f, 1.f);
                        glm::vec2 pos = evalArcEditor(parent, tP);
                        ImGui::Text("Parent Arc: #%d", sel.arcTapParent);
                        ImGui::Text("Position: (%.3f, %.3f)", pos.x, pos.y);
                        ImGui::Spacing();
                        if (ImGui::SmallButton("Select Parent Arc"))
                            m_selectedNoteIdx = sel.arcTapParent;
                    } else {
                        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                                           "Parent #%d is not an Arc!", sel.arcTapParent);
                    }
                } else {
                    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                                       "Orphan ArcTap (parent missing)");
                }
                ImGui::Spacing();
                ImGui::Separator();
            }

            if (ImGui::Button("Delete Note", ImVec2(-1, 26))) {
                int idx = m_selectedNoteIdx;
                notes().erase(notes().begin() + idx);
                fixupArcTapParents(idx);
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
        if (ImGui::Button(opt.label, ImVec2(-1, 42))) {
            if (gm.type != opt.type) {
                // Persist the current mode's charts before swapping so each
                // (mode, difficulty) pair stays independent on disk.
                exportAllCharts();
                gm.type = opt.type;
                reloadChartsForCurrentMode();
            }
        }
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
        if (ImGui::Button("2D - Ground Only", ImVec2(w, 36))) {
            if (gm.dimension != DropDimension::TwoD) {
                exportAllCharts();
                gm.dimension = DropDimension::TwoD;
                reloadChartsForCurrentMode();
            }
        }
        if (is2D) ImGui::PopStyleColor(3);

        ImGui::SameLine();

        if (is3D) {
            ImGui::PushStyleColor(ImGuiCol_Button,       ImVec4(0.2f, 0.4f, 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.9f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.35f, 0.75f, 1.0f));
        }
        if (ImGui::Button("3D - Ground + Sky", ImVec2(w, 36))) {
            if (gm.dimension != DropDimension::ThreeD) {
                exportAllCharts();
                gm.dimension = DropDimension::ThreeD;
                reloadChartsForCurrentMode();
            }
        }
        if (is3D) ImGui::PopStyleColor(3);

        // Sky height slider (3D mode only)
        if (is3D) {
            ImGui::Spacing();
            ImGui::Text("Sky Height");
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##skyHeight", &gm.skyHeight, -1.f, 3.f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("World Y of the sky judgment line.\n"
                                  "Arc height [0..1] maps from ground to this value.");
        }

        ImGui::Spacing();
    }

    // ── Circle Mode Disk Defaults ────────────────────────────────────────────
    if (gm.type == GameModeType::Circle) {
        ImGui::Spacing();
        ImGui::Text("Disk Layout");
        ImGui::Separator();

        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##diskInner",   &gm.diskInnerRadius,  0.2f, 3.0f, "Inner radius (spawn): %.2f")) m_laneMaskDirty = true;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##diskBase",    &gm.diskBaseRadius,   1.0f, 6.0f, "Hit ring radius: %.2f"))       m_laneMaskDirty = true;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##diskSpacing", &gm.diskRingSpacing,  0.1f, 1.5f, "Extra ring spacing: %.2f"))    m_laneMaskDirty = true;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##diskScale",   &gm.diskInitialScale, 0.3f, 5.0f, "Initial scale: %.2f"))         m_laneMaskDirty = true;
        if (ImGui::SmallButton("Reset disk defaults")) {
            gm.diskInnerRadius  = 0.9f;
            gm.diskBaseRadius   = 2.4f;
            gm.diskRingSpacing  = 0.6f;
            gm.diskInitialScale = 1.0f;
            m_laneMaskDirty = true;
        }
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
        m_laneMaskDirty = true;
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
                        if (ImGui::SliderFloat("Target scale", &ev.targetScale, 0.1f, 5.0f, "%.2f×"))
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

        // FC image picker — asset drag-drop only
        {
            const ImVec2 slotSz(96, 96);
            ImVec2 slotPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##fcSlot", slotSz);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 slotMax(slotPos.x + slotSz.x, slotPos.y + slotSz.y);
            VkDescriptorSet fcThumb = gm.fcImage.empty() ? VK_NULL_HANDLE : getThumb(gm.fcImage);
            if (fcThumb) {
                dl->AddImage((ImTextureID)(uint64_t)fcThumb, slotPos, slotMax);
            } else {
                dl->AddRectFilled(slotPos, slotMax, IM_COL32(35, 35, 50, 255), 4.f);
                const char* hint = "Drag asset here";
                ImVec2 ts = ImGui::CalcTextSize(hint);
                dl->AddText(ImVec2(slotPos.x + (slotSz.x - ts.x) * 0.5f,
                                    slotPos.y + (slotSz.y - ts.y) * 0.5f),
                            IM_COL32(140, 140, 160, 200), hint);
            }
            dl->AddRect(slotPos, slotMax, IM_COL32(120, 120, 160, 180), 4.f, 0, 1.5f);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    gm.fcImage = std::string(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (!gm.fcImage.empty()) {
                ImGui::TextDisabled("%s", gm.fcImage.c_str());
                if (ImGui::Button("Clear##fcClear")) gm.fcImage.clear();
            } else {
                ImGui::TextDisabled("(no image)");
            }
            ImGui::EndGroup();
        }

        ImGui::Spacing();

        // ── All Perfect (AP) ────────────────────────────────────────────────
        ImGui::Text("All Perfect (AP)");
        ImGui::TextDisabled("Every note judged Perfect");

        // AP image picker — asset drag-drop only
        {
            const ImVec2 slotSz(96, 96);
            ImVec2 slotPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##apSlot", slotSz);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 slotMax(slotPos.x + slotSz.x, slotPos.y + slotSz.y);
            VkDescriptorSet apThumb = gm.apImage.empty() ? VK_NULL_HANDLE : getThumb(gm.apImage);
            if (apThumb) {
                dl->AddImage((ImTextureID)(uint64_t)apThumb, slotPos, slotMax);
            } else {
                dl->AddRectFilled(slotPos, slotMax, IM_COL32(35, 35, 50, 255), 4.f);
                const char* hint = "Drag asset here";
                ImVec2 ts = ImGui::CalcTextSize(hint);
                dl->AddText(ImVec2(slotPos.x + (slotSz.x - ts.x) * 0.5f,
                                    slotPos.y + (slotSz.y - ts.y) * 0.5f),
                            IM_COL32(140, 140, 160, 200), hint);
            }
            dl->AddRect(slotPos, slotMax, IM_COL32(120, 120, 160, 180), 4.f, 0, 1.5f);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    gm.apImage = std::string(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (!gm.apImage.empty()) {
                ImGui::TextDisabled("%s", gm.apImage.c_str());
                if (ImGui::Button("Clear##apClear")) gm.apImage.clear();
            } else {
                ImGui::TextDisabled("(no image)");
            }
            ImGui::EndGroup();
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

        // Render arc ribbons + arctap diamonds in sky region (Arcaea layout)
        renderArcNotes(dl, origin, size, startTime, tc, skyTrackH, skyTop);

        // Handle note placement in sky or ground based on mouse Y
        if (mouseInTimeline && m_noteTool != NoteTool::None) {
            ImVec2 mpos2 = ImGui::GetIO().MousePos;
            if (m_noteTool == NoteTool::Arc) {
                // Arc: authored in the sky region (purple area)
                handleArcPlacement(origin, size, startTime, tc, skyTrackH, skyTop);
            } else if (m_noteTool == NoteTool::ArcTap) {
                // ArcTap: snap to arc in sky region
                handleArcTapPlacement(origin, size, startTime, tc, skyTrackH, skyTop);
            } else if (mpos2.y < gndTop) {
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
                    fixupArcTapParents(bestIdx);
                    m_holdDragging  = false;
                    m_holdLastTrack = -1;
                    m_holdDraft     = EditorNote{};
                    deletedNote = true;
                    if (m_selectedNoteIdx == bestIdx)
                        m_selectedNoteIdx = -1;
                    else if (m_selectedNoteIdx > bestIdx)
                        m_selectedNoteIdx--;
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
            case EditorNoteType::Tap:    return IM_COL32(100, 180, 255, 230);
            case EditorNoteType::Hold:   return IM_COL32(80, 220, 100, 230);
            case EditorNoteType::Slide:  return IM_COL32(220, 130, 255, 230);
            case EditorNoteType::Flick:  return IM_COL32(255, 180, 80, 230);
            case EditorNoteType::Arc:    return IM_COL32(80, 200, 255, 230);
            case EditorNoteType::ArcTap: return IM_COL32(255, 180, 60, 230);
        }
        return IM_COL32(100, 180, 255, 230);
    };
    auto noteColorDim = [](EditorNoteType t) -> ImU32 {
        switch (t) {
            case EditorNoteType::Tap:    return IM_COL32(60, 110, 160, 160);
            case EditorNoteType::Hold:   return IM_COL32(50, 140, 60, 160);
            case EditorNoteType::Slide:  return IM_COL32(140, 80, 160, 160);
            case EditorNoteType::Flick:  return IM_COL32(180, 120, 50, 160);
            case EditorNoteType::Arc:    return IM_COL32(50, 130, 170, 160);
            case EditorNoteType::ArcTap: return IM_COL32(180, 120, 40, 160);
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
            // Arcs and ArcTaps are drawn by the dedicated sky block below —
            // skip the lane-rect path so we don't duplicate them as drops.
            if (note.type == EditorNoteType::Arc ||
                note.type == EditorNoteType::ArcTap) continue;
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

        // ── Arcs + ArcTaps (Arcaea-style, sky-space ribbons) ───────────────
        if (is3D) {
            float laneHalfW = (tc * 0.5f) * laneSpacing;
            auto arcToWorld = [&](float ax, float ay, float dt) -> glm::vec3 {
                float wx = (ax * 2.f - 1.f) * laneHalfW;
                float wy = ay * SKY_Y;
                float wz = -dt * SCROLL_SPEED;
                return {wx, wy, wz};
            };
            for (const auto& note : curNotes) {
                if (note.type != EditorNoteType::Arc) continue;
                if (note.arcIsVoid) continue;
                float dur = note.endTime - note.time;
                if (dur < 0.0001f) continue;
                float headDt = note.time - curTime;
                float tailDt = note.endTime - curTime;
                if (tailDt < -0.3f || headDt > lookAhead) continue;

                constexpr int ARC_SAMPLES = 20;
                ImVec2 prevPx{};
                bool havePrev = false;
                ImU32 acol = (note.arcColor == 0)
                    ? IM_COL32(80, 200, 255, note.arcIsVoid ? 120 : 230)
                    : IM_COL32(255, 100, 180, note.arcIsVoid ? 120 : 230);
                for (int si = 0; si <= ARC_SAMPLES; ++si) {
                    float u = (float)si / (float)ARC_SAMPLES;
                    glm::vec2 p = evalArcEditor(note, u);
                    float sampleDt = (note.time + u * dur) - curTime;
                    if (sampleDt < -0.3f || sampleDt > lookAhead) {
                        havePrev = false; continue;
                    }
                    glm::vec3 wp = arcToWorld(p.x, p.y, sampleDt);
                    glm::vec4 clip = vp * glm::vec4(wp, 1.f);
                    if (clip.w <= 0.f) { havePrev = false; continue; }
                    ImVec2 px = w2s(wp);
                    if (havePrev)
                        dl->AddLine(prevPx, px, acol, 3.f);
                    prevPx = px;
                    havePrev = true;
                }
            }

            for (const auto& note : curNotes) {
                if (note.type != EditorNoteType::ArcTap) continue;
                if (note.arcTapParent < 0 ||
                    note.arcTapParent >= (int)curNotes.size()) continue;
                const auto& parent = curNotes[note.arcTapParent];
                if (parent.type != EditorNoteType::Arc) continue;
                float pdur = parent.endTime - parent.time;
                if (pdur < 0.0001f) continue;
                float dt = note.time - curTime;
                if (dt < -0.2f || dt > lookAhead) continue;
                float u = std::clamp((note.time - parent.time) / pdur, 0.f, 1.f);
                glm::vec2 p = evalArcEditor(parent, u);
                glm::vec3 wp{(p.x * 2.f - 1.f) * laneHalfW, p.y * SKY_Y, -dt * SCROLL_SPEED};
                glm::vec4 clip = vp * glm::vec4(wp, 1.f);
                if (clip.w <= 0.f) continue;
                ImVec2 c = w2s(wp);
                float r = laneSpacing * proj11 * size.y * 0.5f / clip.w * 0.35f;
                if (r < 3.f) r = 3.f;
                ImVec2 pts[4] = {
                    ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y),
                    ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)
                };
                dl->AddConvexPolyFilled(pts, 4, IM_COL32(255, 180, 60, 240));
                dl->AddPolyline(pts, 4, IM_COL32(255, 240, 200, 220),
                                ImDrawFlags_Closed, 1.5f);
            }
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
        const float kBaseRadius = m_song && m_song->gameMode.diskBaseRadius > 0.f
                                   ? m_song->gameMode.diskBaseRadius : 2.4f;
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
        // ── Scan Line mode (paginated) ──────────────────────────────────────
        // A "page" = one sweep of the scan line. The scene shows exactly one
        // page at a time with a header strip for navigation + per-page speed.
        // Page 0 sweeps bottom→top (scan line at y=1 at startTime, y=0 at end).
        if (m_scanPageTableDirty) rebuildScanPageTable();

        if (m_scanPageTable.empty()) {
            dl->AddRectFilled(origin, pMax, IM_COL32(14, 14, 20, 255));
            dl->AddText(ImVec2(origin.x + 12, origin.y + 12),
                        IM_COL32(200, 200, 200, 200),
                        "Scan Line Mode — load audio to generate pages.");
        } else {
            if (m_scanCurrentPage < 0) m_scanCurrentPage = 0;
            if (m_scanCurrentPage >= (int)m_scanPageTable.size())
                m_scanCurrentPage = (int)m_scanPageTable.size() - 1;

            // Auto-advance the page to match the current song time whenever
            // curTime falls outside the active page's range. Covers playback,
            // scrubbing, and any manual time edits. Manual Prev/Next/edge-
            // flip navigation keeps its target by also seeking m_sceneTime
            // below, so this sync doesn't snap back.
            {
                const auto& pCheck = m_scanPageTable[m_scanCurrentPage];
                double pStart = pCheck.startTime;
                double pEnd   = pCheck.startTime + pCheck.duration;
                if ((double)curTime < pStart - 1e-4 ||
                    (double)curTime > pEnd   + 1e-4)
                {
                    int autoP = scanPageForTime((double)curTime);
                    if (autoP != m_scanCurrentPage) m_scanCurrentPage = autoP;
                }
            }

            const ScanPageInfo& page = m_scanPageTable[m_scanCurrentPage];
            const float headerH  = 36.f;
            const ImVec2 bodyOrigin(origin.x, origin.y + headerH);
            const ImVec2 bodySize(size.x, std::max(10.f, size.y - headerH));
            const ImVec2 bodyMax(bodyOrigin.x + bodySize.x,
                                 bodyOrigin.y + bodySize.y);

            // Body background
            dl->AddRectFilled(bodyOrigin, bodyMax, IM_COL32(12, 12, 18, 255));

            // Navigation header (Prev/Next, label, speed input, Place All).
            renderScanPageNav(origin, size.x, engine);

            // Normalized→local converters for the body rect.
            auto scanToLocal = [&](float nx, float ny) -> ImVec2 {
                return ImVec2(bodyOrigin.x + nx * bodySize.x,
                              bodyOrigin.y + ny * bodySize.y);
            };

            // Beat grid: 4 subdivisions
            for (int k = 1; k <= 3; ++k) {
                float y = bodyOrigin.y + (float)k / 4.f * bodySize.y;
                dl->AddLine(ImVec2(bodyOrigin.x + 2, y),
                            ImVec2(bodyMax.x    - 2, y),
                            IM_COL32(60, 60, 80, 110), 1.f);
            }

            // Direction arrow indicator at the "end" edge of the page.
            {
                float ax = bodyMax.x - 18.f;
                float endY = page.goingUp ? bodyOrigin.y + 14.f
                                          : bodyMax.y    - 14.f;
                ImU32 dcol = IM_COL32(255, 255, 255, 180);
                if (page.goingUp) {
                    dl->AddTriangleFilled(ImVec2(ax, endY - 6),
                                          ImVec2(ax - 5, endY + 4),
                                          ImVec2(ax + 5, endY + 4), dcol);
                } else {
                    dl->AddTriangleFilled(ImVec2(ax, endY + 6),
                                          ImVec2(ax - 5, endY - 4),
                                          ImVec2(ax + 5, endY - 4), dcol);
                }
            }

            // AI beat-recommendation ticks (dashed, orange).
            {
                const double pStart = page.startTime;
                const double pEnd   = page.startTime + page.duration;
                ImU32 mkCol = IM_COL32(255, 140, 60, 140);
                for (float t : markers()) {
                    if ((double)t < pStart || (double)t > pEnd) continue;
                    float y01 = scanPageTimeToY(m_scanCurrentPage, (double)t);
                    float y   = bodyOrigin.y + y01 * bodySize.y;
                    float xS = bodyOrigin.x + 4.f;
                    float xE = bodyMax.x    - 4.f;
                    for (float x = xS; x < xE; x += 14.f) {
                        float x2 = std::min(x + 8.f, xE);
                        dl->AddLine(ImVec2(x, y), ImVec2(x2, y), mkCol, 1.f);
                    }
                }
            }

            // Scan line: playback position (only when song time is in this page).
            {
                const double pStart = page.startTime;
                const double pEnd   = page.startTime + page.duration;
                if ((double)curTime >= pStart && (double)curTime <= pEnd) {
                    float y01   = scanPageTimeToY(m_scanCurrentPage,
                                                  (double)curTime);
                    float scanY = bodyOrigin.y + y01 * bodySize.y;
                    dl->AddLine(ImVec2(bodyOrigin.x + 10, scanY),
                                ImVec2(bodyMax.x    - 10, scanY),
                                IM_COL32(0, 200, 255, 60), 6.f);
                    dl->AddLine(ImVec2(bodyOrigin.x + 10, scanY),
                                ImVec2(bodyMax.x    - 10, scanY),
                                IM_COL32(255, 255, 255, 220), 2.f);
                }
            }

            // Cursor-follow scan line: tracks the mouse Y when hovering the
            // scene body. Gives the user a live preview of where a click
            // would place a note in time, and pairs with the edge auto-flip.
            {
                ImVec2 mposPreview = ImGui::GetMousePos();
                bool insideBody = (mposPreview.x >= bodyOrigin.x &&
                                   mposPreview.x <= bodyMax.x &&
                                   mposPreview.y >= bodyOrigin.y &&
                                   mposPreview.y <= bodyMax.y);
                if (insideBody) {
                    float cy = mposPreview.y;
                    dl->AddLine(ImVec2(bodyOrigin.x + 10, cy),
                                ImVec2(bodyMax.x    - 10, cy),
                                IM_COL32(255, 200, 80, 80), 5.f);
                    dl->AddLine(ImVec2(bodyOrigin.x + 10, cy),
                                ImVec2(bodyMax.x    - 10, cy),
                                IM_COL32(255, 240, 180, 180), 1.5f);
                    // Show the cursor's projected song time.
                    float  y01    = std::clamp((cy - bodyOrigin.y) / std::max(1.f, bodySize.y), 0.f, 1.f);
                    double tPrev  = scanPageYToTime(m_scanCurrentPage, y01);
                    char tbuf[48];
                    int mins = (int)(tPrev / 60.0);
                    double secs = tPrev - mins * 60.0;
                    snprintf(tbuf, sizeof(tbuf), "t=%d:%06.3f", mins, secs);
                    ImVec2 tsz = ImGui::CalcTextSize(tbuf);
                    ImVec2 tp(std::clamp(mposPreview.x + 12.f,
                                         bodyOrigin.x + 4.f,
                                         bodyMax.x - tsz.x - 4.f),
                              std::clamp(cy - tsz.y - 4.f,
                                         bodyOrigin.y + 4.f,
                                         bodyMax.y - tsz.y - 4.f));
                    dl->AddText(ImVec2(tp.x + 1, tp.y + 1),
                                IM_COL32(0, 0, 0, 200), tbuf);
                    dl->AddText(tp, IM_COL32(255, 230, 180, 240), tbuf);
                }
            }

            // Helpers: page-edge Y values (body-local).
            auto startEdgeY = [&]() { return page.goingUp ? bodyMax.y : bodyOrigin.y; };
            auto endEdgeY   = [&]() { return page.goingUp ? bodyOrigin.y : bodyMax.y; };
            // Draw a cross-page marker glyph. `fromPrev`=true draws at start edge
            // (▲ pointing into time-forward direction); false draws at end edge.
            auto drawCrossMarker = [&](float xN, bool fromPrev, ImU32 col) {
                float cx = bodyOrigin.x + std::clamp(xN, 0.01f, 0.99f) * bodySize.x;
                float cy = fromPrev ? startEdgeY() : endEdgeY();
                // Triangle points in time-forward direction (start→end).
                float step = (endEdgeY() - startEdgeY()) > 0 ? +8.f : -8.f;
                if (!fromPrev) step = -step;
                ImVec2 apex(cx, cy + step);
                dl->AddTriangleFilled(apex,
                                      ImVec2(cx - 7.f, cy),
                                      ImVec2(cx + 7.f, cy),
                                      col);
            };

            auto withAlpha = [](ImU32 col, float a) -> ImU32 {
                int aa = std::clamp((int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * a),
                                    0, 255);
                return (col & ~IM_COL32_A_MASK) | ((ImU32)aa << IM_COL32_A_SHIFT);
            };

            // ── Render notes overlapping this page ──────────────────────────
            for (const auto& note : curNotes) {
                // Skip legacy lane-only notes (no scan position).
                if (note.scanX < 0.001f && note.scanY < 0.001f &&
                    note.scanEndY < 0.f && note.scanPath.empty())
                    continue;

                const double pStart = page.startTime;
                const double pEnd   = page.startTime + page.duration;

                // Time range occupied by this note (endTime is 0 for taps/flicks).
                double nStart = note.time;
                double nEnd   = (note.endTime > note.time) ? note.endTime : nStart;

                // Skip if the note doesn't intersect this page's time range.
                if (nEnd < pStart || nStart > pEnd) continue;

                ImU32 col    = noteColor(note.type);
                ImU32 colDim = noteColorDim(note.type);
                ImU32 colOut = IM_COL32(255, 255, 255, 160);

                // Head position on the current page (if the head is on this page).
                bool headOnPage = (nStart >= pStart && nStart <= pEnd);

                switch (note.type) {
                    case EditorNoteType::Tap:
                        if (headOnPage) {
                            float y01 = scanPageTimeToY(m_scanCurrentPage, nStart);
                            ImVec2 h = scanToLocal(note.scanX, y01);
                            dl->AddCircleFilled(h, 10.f, col);
                            dl->AddCircle(h, 11.f, colOut, 0, 1.5f);
                        }
                        break;
                    case EditorNoteType::Flick:
                        if (headOnPage) {
                            float y01 = scanPageTimeToY(m_scanCurrentPage, nStart);
                            ImVec2 h = scanToLocal(note.scanX, y01);
                            float s = 12.f;
                            bool up = page.goingUp;
                            if (up) {
                                dl->AddTriangleFilled(
                                    ImVec2(h.x, h.y - s),
                                    ImVec2(h.x - s * 0.85f, h.y + s * 0.6f),
                                    ImVec2(h.x + s * 0.85f, h.y + s * 0.6f), col);
                            } else {
                                dl->AddTriangleFilled(
                                    ImVec2(h.x, h.y + s),
                                    ImVec2(h.x - s * 0.85f, h.y - s * 0.6f),
                                    ImVec2(h.x + s * 0.85f, h.y - s * 0.6f), col);
                            }
                        }
                        break;
                    case EditorNoteType::Hold: {
                        // Clip body to this page's [pStart, pEnd] window.
                        double bodyStart = std::max((double)nStart, pStart);
                        double bodyEnd   = std::min((double)nEnd,   pEnd);
                        if (bodyEnd > bodyStart) {
                            float ya = scanPageTimeToY(m_scanCurrentPage, bodyStart);
                            float yb = scanPageTimeToY(m_scanCurrentPage, bodyEnd);
                            ImVec2 a = scanToLocal(note.scanX, ya);
                            ImVec2 b = scanToLocal(note.scanX, yb);
                            float hw = 6.f;
                            dl->AddRectFilled(
                                ImVec2(a.x - hw, std::min(a.y, b.y)),
                                ImVec2(a.x + hw, std::max(a.y, b.y)),
                                colDim, 2.f);
                        }
                        // Head marker (if on this page).
                        if (headOnPage) {
                            float y01 = scanPageTimeToY(m_scanCurrentPage, nStart);
                            ImVec2 h = scanToLocal(note.scanX, y01);
                            dl->AddCircleFilled(h, 10.f, col);
                            dl->AddCircle(h, 11.f, colOut, 0, 1.5f);
                        }
                        // Tail cap (if on this page).
                        if (nEnd >= pStart && nEnd <= pEnd) {
                            float y01 = scanPageTimeToY(m_scanCurrentPage, nEnd);
                            ImVec2 t = scanToLocal(note.scanX, y01);
                            dl->AddCircleFilled(t, 6.f, col);
                        }
                        // Cross-page markers.
                        if (nStart < pStart)
                            drawCrossMarker(note.scanX, true,
                                            IM_COL32(180, 220, 255, 220));
                        if (nEnd > pEnd)
                            drawCrossMarker(note.scanX, false,
                                            IM_COL32(180, 220, 255, 220));
                        break;
                    }
                    case EditorNoteType::Slide: {
                        // Draw only the segments that intersect this page.
                        const double slideDur = std::max(0.0, nEnd - nStart);
                        if (note.scanPath.size() >= 2 && slideDur > 0.0) {
                            const int N = (int)note.scanPath.size();
                            for (int i = 0; i + 1 < N; ++i) {
                                // Approximate each control-point's absolute time
                                // as uniform along the path (matches authoring
                                // when nodes were placed with monotonic time).
                                double t0 = nStart + slideDur * ((double)i / (N - 1));
                                double t1 = nStart + slideDur * ((double)(i + 1) / (N - 1));
                                // Skip if the segment is entirely outside this page.
                                if (t1 < pStart || t0 > pEnd) continue;
                                ImVec2 a = scanToLocal(note.scanPath[i].first,
                                                       note.scanPath[i].second);
                                ImVec2 b = scanToLocal(note.scanPath[i + 1].first,
                                                       note.scanPath[i + 1].second);
                                dl->AddLine(a, b, colDim, 4.f);
                            }
                            // Control-point markers for nodes on this page.
                            for (int i = 0; i < N; ++i) {
                                double ti = nStart +
                                    slideDur * ((double)i / std::max(1, N - 1));
                                if (ti < pStart || ti > pEnd) continue;
                                ImVec2 p = scanToLocal(note.scanPath[i].first,
                                                       note.scanPath[i].second);
                                float r = (i == 0) ? 8.f : 6.f;
                                ImU32 c = (i == 0) ? col
                                                   : IM_COL32(255, 255, 255, 230);
                                dl->AddCircleFilled(p, r, c);
                                dl->AddCircle(p, r + 1.f, colOut, 0, 1.f);
                            }
                        } else if (headOnPage) {
                            float y01 = scanPageTimeToY(m_scanCurrentPage, nStart);
                            ImVec2 h = scanToLocal(note.scanX, y01);
                            dl->AddCircleFilled(h, 8.f, col);
                            dl->AddCircle(h, 9.f, colOut, 0, 1.5f);
                        }
                        if (nStart < pStart)
                            drawCrossMarker(note.scanX, true,
                                            IM_COL32(220, 180, 255, 220));
                        if (nEnd > pEnd)
                            drawCrossMarker(note.scanX, false,
                                            IM_COL32(220, 180, 255, 220));
                        (void)withAlpha;
                        break;
                    }
                    case EditorNoteType::Arc:
                    case EditorNoteType::ArcTap:
                        break;
                }
            }

            // ── In-progress hold preview ────────────────────────────────────
            if (m_scanHoldAwaitEnd) {
                const int startPage = m_scanHoldStartPage;
                const int endPageGuess = std::max(m_scanCurrentPage,
                    startPage + m_scanHoldExtraSweeps);
                // Draw head marker if on this page.
                if (startPage == m_scanCurrentPage) {
                    ImVec2 sp = scanToLocal(m_scanHoldStartX, m_scanHoldStartY);
                    dl->AddCircleFilled(sp, 10.f, IM_COL32(80, 220, 100, 220));
                }
                // Draw body segment from start (if on page) or from start edge,
                // down/up to cursor or end edge.
                ImVec2 mp = ImGui::GetMousePos();
                float my = std::clamp(mp.y, bodyOrigin.y, bodyMax.y);
                float sy = (startPage == m_scanCurrentPage)
                           ? scanToLocal(m_scanHoldStartX, m_scanHoldStartY).y
                           : startEdgeY();
                float ey = (m_scanCurrentPage >= endPageGuess) ? my : endEdgeY();
                float sx = scanToLocal(m_scanHoldStartX, 0.f).x;
                const float hw = 6.f;
                dl->AddRectFilled(ImVec2(sx - hw, std::min(sy, ey)),
                                  ImVec2(sx + hw, std::max(sy, ey)),
                                  IM_COL32(80, 220, 100, 180), 2.f);
                // Ghost scan line at cursor Y (only on the end page)
                if (m_scanCurrentPage >= endPageGuess) {
                    dl->AddLine(ImVec2(bodyOrigin.x + 10, my),
                                ImVec2(bodyMax.x - 10, my),
                                IM_COL32(80, 220, 140, 100), 2.f);
                }
                // Cross-page markers for the span.
                if (m_scanCurrentPage > startPage)
                    drawCrossMarker(m_scanHoldStartX, true,
                                    IM_COL32(80, 220, 140, 220));
                if (m_scanCurrentPage < endPageGuess)
                    drawCrossMarker(m_scanHoldStartX, false,
                                    IM_COL32(80, 220, 140, 220));
                // Extra-sweep badge
                if (m_scanHoldExtraSweeps > 0) {
                    char buf[48];
                    snprintf(buf, sizeof(buf), "+%d pages", m_scanHoldExtraSweeps);
                    dl->AddText(ImVec2(sx + 12.f, bodyOrigin.y + 6.f),
                                IM_COL32(200, 255, 200, 230), buf);
                }
            }

            // ── In-progress slide preview ───────────────────────────────────
            if (m_scanSlideDragging && !m_scanSlideDraft.scanPath.empty()) {
                const auto& draftPath  = m_scanSlideDraft.scanPath;
                const auto& draftPages = m_scanSlidePathPages;
                for (size_t i = 1; i < draftPath.size(); ++i) {
                    int pi0 = (i - 1 < draftPages.size())
                              ? draftPages[i - 1] : m_scanCurrentPage;
                    int pi1 = (i < draftPages.size())
                              ? draftPages[i] : m_scanCurrentPage;
                    if (pi0 != m_scanCurrentPage && pi1 != m_scanCurrentPage) continue;
                    ImVec2 a = scanToLocal(draftPath[i - 1].first,
                                           draftPath[i - 1].second);
                    ImVec2 b = scanToLocal(draftPath[i].first,
                                           draftPath[i].second);
                    dl->AddLine(a, b, IM_COL32(220, 130, 255, 230), 4.f);
                }
                // Node markers on the current page.
                for (size_t i = 0; i < draftPath.size(); ++i) {
                    int pi = (i < draftPages.size()) ? draftPages[i] : m_scanCurrentPage;
                    if (pi != m_scanCurrentPage) continue;
                    ImVec2 p = scanToLocal(draftPath[i].first, draftPath[i].second);
                    float r = (i == 0) ? 8.f : 6.f;
                    ImU32 c = (i == 0) ? IM_COL32(220, 130, 255, 230)
                                       : IM_COL32(255, 255, 255, 220);
                    dl->AddCircleFilled(p, r, c);
                }
                // Preview line to cursor on the current page.
                if (!draftPages.empty() && draftPages.back() == m_scanCurrentPage) {
                    ImVec2 last = scanToLocal(draftPath.back().first,
                                              draftPath.back().second);
                    ImVec2 mpos = ImGui::GetMousePos();
                    float cmx = std::clamp(mpos.x, bodyOrigin.x, bodyMax.x);
                    float cmy = std::clamp(mpos.y, bodyOrigin.y, bodyMax.y);
                    dl->AddLine(last, ImVec2(cmx, cmy),
                                IM_COL32(220, 130, 255, 100), 2.f);
                }
                // Cross-page markers when nodes span across this page.
                if (!draftPages.empty()) {
                    if (draftPages.front() < m_scanCurrentPage)
                        drawCrossMarker(draftPath.front().first, true,
                                        IM_COL32(220, 130, 255, 220));
                    if (draftPages.back() > m_scanCurrentPage)
                        drawCrossMarker(draftPath.back().first, false,
                                        IM_COL32(220, 130, 255, 220));
                }
            }

            // In-scene instructions (bottom-left of body).
            const char* toolLabel = "None";
            switch (m_noteTool) {
                case NoteTool::Tap:   toolLabel = "Tap";   break;
                case NoteTool::Flick: toolLabel = "Flick"; break;
                case NoteTool::Hold:  toolLabel = "Hold";  break;
                case NoteTool::Slide: toolLabel = "Slide"; break;
                default:              toolLabel = "None";  break;
            }
            char hud[128];
            snprintf(hud, sizeof(hud),
                     "Tool: %s  %s  (Alt: no snap)",
                     toolLabel, page.goingUp ? "^ up" : "v down");
            dl->AddText(ImVec2(bodyOrigin.x + 8, bodyMax.y - 20.f),
                        IM_COL32(200, 200, 200, 180), hud);

            // Dispatch input for the body rect only.
            bool hovered = ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            handleScanLinePageInput(bodyOrigin, bodySize, curTime, hovered, engine);
        }

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

// ── Scan-line page-table helpers ────────────────────────────────────────────
// See ScanPageUtils.h for the shared builder. The editor caches the table in
// m_scanPageTable and invalidates via m_scanPageTableDirty on every edit that
// could affect page boundaries (BPM, overrides, song duration, difficulty).

void SongEditor::rebuildScanPageTable() {
    m_scanPageTable.clear();
    if (!m_song) { m_scanPageTableDirty = false; return; }

    // Collect timing points: user-authored takes precedence; otherwise fall
    // back to AI-detected BPM changes when available (so a freshly analyzed
    // song gets page boundaries automatically).
    std::vector<TimingPoint> tps;
    // TODO: when user-authored timing is exposed in the editor it will live
    // in m_song or a sibling vector; for now we always reuse m_bpmChanges.
    if (!m_bpmChanges.empty()) {
        for (const auto& bc : m_bpmChanges) {
            TimingPoint tp{};
            tp.time = bc.time;
            tp.bpm  = bc.bpm;
            tp.meter = 4;
            tps.push_back(tp);
        }
    }

    double songEnd = m_waveformLoaded ? m_waveform.durationSeconds : 0.0;
    // Extend to the last note if audio length is unknown or shorter.
    for (const auto& en : notes()) {
        double t = (double)en.time;
        if (en.endTime > en.time) t = (double)en.endTime;
        if (t > songEnd) songEnd = t;
    }
    if (songEnd <= 0.0) songEnd = 180.0;  // safe default: 3 minutes
    songEnd += 2.0;                       // trailing tail

    float fallbackBpm = m_dominantBpm > 0.f ? m_dominantBpm : 120.f;
    m_scanPageTable = buildScanPageTable(tps, scanPages(), songEnd, fallbackBpm);

    // Regenerate runtime-facing speed events from overrides so legacy
    // consumers (m_scanPhaseTable, CytusRenderer) reflect edits immediately.
    auto regenerated = expandScanPagesToSpeedEvents(m_scanPageTable, scanPages());
    if (!regenerated.empty() || !scanPages().empty()) {
        scanSpeed() = regenerated;
        m_scanPhaseDirty = true;
    }

    if (m_scanCurrentPage >= (int)m_scanPageTable.size())
        m_scanCurrentPage = std::max(0, (int)m_scanPageTable.size() - 1);

    m_scanPageTableDirty = false;
}

int SongEditor::scanPageForTime(double t) const {
    if (m_scanPageTable.empty()) return 0;
    // Binary search for last page with startTime <= t.
    int lo = 0, hi = (int)m_scanPageTable.size() - 1;
    if (t <= m_scanPageTable.front().startTime) return 0;
    if (t >= m_scanPageTable.back().startTime)  return hi;
    while (lo + 1 < hi) {
        int mid = (lo + hi) / 2;
        if (m_scanPageTable[mid].startTime <= t) lo = mid;
        else                                      hi = mid;
    }
    return lo;
}

double SongEditor::scanPageYToTime(int pageIdx, float y01) const {
    if (pageIdx < 0 || pageIdx >= (int)m_scanPageTable.size()) return 0.0;
    const auto& p = m_scanPageTable[pageIdx];
    y01 = std::clamp(y01, 0.f, 1.f);
    // goingUp: scan line starts at bottom (y=1 at startTime) and rises to top
    // (y=0 at startTime+duration). goingDown: inverse.
    if (p.goingUp) return p.startTime + (1.0 - (double)y01) * p.duration;
    return p.startTime + (double)y01 * p.duration;
}

float SongEditor::scanPageTimeToY(int pageIdx, double t) const {
    if (pageIdx < 0 || pageIdx >= (int)m_scanPageTable.size()) return 0.f;
    const auto& p = m_scanPageTable[pageIdx];
    if (p.duration <= 1e-6) return 0.f;
    double u = (t - p.startTime) / p.duration;
    u = std::clamp(u, 0.0, 1.0);
    return p.goingUp ? (float)(1.0 - u) : (float)u;
}

float SongEditor::snapToScanMarker(float time, float tolerance) const {
    const auto& mk = markers();
    if (mk.empty() || tolerance <= 0.f) return time;
    float best      = time;
    float bestDelta = tolerance;
    for (float t : mk) {
        float d = std::abs(t - time);
        if (d <= bestDelta) { bestDelta = d; best = t; }
    }
    return best;
}

// ── renderScanPageNav ───────────────────────────────────────────────────────
// Top-strip navigation controls for the paginated Scan Line scene: Prev/Next
// buttons, page label, per-page speed InputFloat, and Place All (AI markers).

void SongEditor::renderScanPageNav(ImVec2 origin, float width, Engine* engine) {
    if (m_scanPageTable.empty()) return;
    const int nPages = (int)m_scanPageTable.size();
    m_scanCurrentPage = std::clamp(m_scanCurrentPage, 0, nPages - 1);
    ScanPageInfo& page = m_scanPageTable[m_scanCurrentPage];

    ImGui::SetCursorScreenPos(ImVec2(origin.x + 6, origin.y + 6));
    ImGui::PushID("ScanPageNav");

    // Prev
    {
        ImGui::BeginDisabled(m_scanCurrentPage <= 0);
        if (ImGui::ArrowButton("##prevPage", ImGuiDir_Left)) {
            m_scanCurrentPage = std::max(0, m_scanCurrentPage - 1);
            if (engine) engine->audio().stop();
            m_sceneTime = (float)m_scanPageTable[m_scanCurrentPage].startTime;
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();

    // Page label
    char buf[128];
    snprintf(buf, sizeof(buf), "Page %d/%d  BPM %.1f  dt %.2fs %s",
             m_scanCurrentPage + 1, nPages, page.bpm, page.duration,
             page.partialTail ? "(partial)" : "");
    ImGui::TextUnformatted(buf);
    ImGui::SameLine();

    // Next
    {
        ImGui::BeginDisabled(m_scanCurrentPage >= nPages - 1);
        if (ImGui::ArrowButton("##nextPage", ImGuiDir_Right)) {
            m_scanCurrentPage = std::min(nPages - 1, m_scanCurrentPage + 1);
            if (engine) engine->audio().stop();
            m_sceneTime = (float)m_scanPageTable[m_scanCurrentPage].startTime;
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();

    // Per-page speed input
    {
        float speed = page.speed;
        ImGui::SetNextItemWidth(96.f);
        if (ImGui::InputFloat("speed", &speed, 0.25f, 0.5f, "%.2fx")) {
            speed = std::clamp(speed, 0.25f, 4.f);
            // Update or remove the override
            auto& ov = scanPages();
            bool removed = false;
            if (std::abs(speed - 1.f) < 1e-3f) {
                for (auto it = ov.begin(); it != ov.end(); ) {
                    if (it->pageIndex == m_scanCurrentPage) {
                        it = ov.erase(it); removed = true;
                    } else ++it;
                }
                if (!removed) { /* no-op */ }
            } else {
                bool found = false;
                for (auto& o : ov)
                    if (o.pageIndex == m_scanCurrentPage) {
                        o.speed = speed; found = true; break;
                    }
                if (!found) ov.push_back({m_scanCurrentPage, speed});
            }
            m_scanPageTableDirty = true;
        }
    }
    ImGui::SameLine();

    // Place All (AI markers)
    {
        ImGui::BeginDisabled(markers().empty());
        if (ImGui::Button("Place All")) {
            int added = 0;
            auto& ns = notes();
            for (size_t i = 0; i < markers().size(); ++i) {
                float t = markers()[i];
                // De-dup: skip if an existing note already sits within 10ms of t.
                bool dup = false;
                for (const auto& n : ns) {
                    if (std::abs(n.time - t) < 0.01f) { dup = true; break; }
                }
                if (dup) continue;
                int pIdx = scanPageForTime((double)t);
                if (pIdx < 0 || pIdx >= (int)m_scanPageTable.size()) continue;

                EditorNote n{};
                n.type  = EditorNoteType::Tap;
                n.time  = t;
                // Alternate X across {0.25, 0.5, 0.75} to avoid a straight column.
                const float xs[3] = {0.5f, 0.25f, 0.75f};
                n.scanX = xs[added % 3];
                n.scanY = scanPageTimeToY(pIdx, (double)t);
                ns.push_back(n);
                ++added;
            }
            std::sort(ns.begin(), ns.end(),
                      [](const EditorNote& a, const EditorNote& b) {
                          return a.time < b.time;
                      });
        }
        ImGui::EndDisabled();
    }
    // Direction pill
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f),
                       page.goingUp ? "^ up" : "v down");

    ImGui::PopID();
    (void)width;
}

// ── handleScanLinePageInput ─────────────────────────────────────────────────
// Page-local input for the paginated Scan Line scene. Tools place notes
// anywhere in the body rect (no scan-line proximity gate). Clicks snap to
// AI beat markers unless Alt is held. Holds/slides may cross pages via
// Prev/Next navigation while the tool is in its "await end" / "dragging"
// state; the mouse wheel extends the hold span in page units.

void SongEditor::handleScanLinePageInput(ImVec2 origin, ImVec2 size, float curTime,
                                         bool hovered, Engine* engine) {
    if (!m_song || m_scanPageTable.empty()) return;
    if (m_scanCurrentPage < 0 ||
        m_scanCurrentPage >= (int)m_scanPageTable.size()) return;
    const ScanPageInfo& page = m_scanPageTable[m_scanCurrentPage];

    const float invW = 1.f / std::max(1.f, size.x);
    const float invH = 1.f / std::max(1.f, size.y);
    auto localToNorm = [&](ImVec2 p) -> ImVec2 {
        return ImVec2((p.x - origin.x) * invW, (p.y - origin.y) * invH);
    };

    ImVec2 mp     = ImGui::GetMousePos();
    ImVec2 pMax(origin.x + size.x, origin.y + size.y);
    bool   inRect = (mp.x >= origin.x && mp.x <= pMax.x &&
                     mp.y >= origin.y && mp.y <= pMax.y);
    bool   lmbClick = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool   lmbDown  = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool   rmbClick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    bool   altHeld  = ImGui::GetIO().KeyAlt;

    // Keyboard shortcuts for Prev/Next page (work even when not hovered).
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
        m_scanCurrentPage = std::min((int)m_scanPageTable.size() - 1,
                                     m_scanCurrentPage + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
        m_scanCurrentPage = std::max(0, m_scanCurrentPage - 1);

    // ── Auto page turning at the page edges ────────────────────────────────
    // Cursor at the time-forward edge → next page. Cursor at the start edge →
    // previous page. Gated on actual motion + a one-shot arm flag so a parked
    // cursor doesn't flip repeatedly. Disabled during playback (the scan
    // line's auto-advance owns the page there).
    {
        bool playing = (engine && engine->audio().isPlaying());
        ImVec2 md = ImGui::GetIO().MouseDelta;
        bool cursorMoving = (md.x * md.x + md.y * md.y) > 0.25f;
        if (hovered && cursorMoving && !playing) {
            constexpr float kEdge = 10.f;
            bool atEnd = page.goingUp
                ? (mp.y < origin.y + kEdge)
                : (mp.y > pMax.y - kEdge);
            bool atStart = page.goingUp
                ? (mp.y > pMax.y - kEdge)
                : (mp.y < origin.y + kEdge);
            bool hasNext = m_scanCurrentPage + 1 < (int)m_scanPageTable.size();
            bool hasPrev = m_scanCurrentPage > 0;
            auto seekToPageStart = [&]() {
                m_sceneTime = (float)m_scanPageTable[m_scanCurrentPage].startTime;
            };
            if (atEnd && hasNext && !m_scanPageEdgeArmed) {
                m_scanCurrentPage++;
                seekToPageStart();
                m_scanPageEdgeArmed = true;
            } else if (atStart && hasPrev && !m_scanPageEdgeArmed) {
                m_scanCurrentPage--;
                seekToPageStart();
                m_scanPageEdgeArmed = true;
            } else if (!atEnd && !atStart) {
                m_scanPageEdgeArmed = false;
            }
        } else if (!hovered || playing) {
            m_scanPageEdgeArmed = false;
        }
    }

    // ── Hold await-end state ────────────────────────────────────────────────
    if (m_scanHoldAwaitEnd) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_scanHoldAwaitEnd = false;
            return;
        }
        // Mouse wheel extends the hold span in pages.
        float wheel = ImGui::GetIO().MouseWheel;
        if (hovered && std::abs(wheel) > 0.1f) {
            if (wheel > 0.f) {
                int maxExtra = (int)m_scanPageTable.size() - 1 - m_scanHoldStartPage;
                m_scanHoldExtraSweeps = std::min(maxExtra,
                                                 m_scanHoldExtraSweeps + 1);
            } else if (m_scanHoldExtraSweeps > 0) {
                m_scanHoldExtraSweeps--;
            }
        }
        // Navigating Prev/Next while in await-end auto-extends the span to
        // at least cover the visited page.
        if (m_scanCurrentPage > m_scanHoldStartPage) {
            int needed = m_scanCurrentPage - m_scanHoldStartPage;
            if (needed > m_scanHoldExtraSweeps) m_scanHoldExtraSweeps = needed;
        }

        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (hovered && inRect && lmbClick) {
            // Commit: endpoint is at cursor on the currently displayed page.
            ImVec2 mpC(std::clamp(mp.x, origin.x, pMax.x),
                       std::clamp(mp.y, origin.y, pMax.y));
            ImVec2 nrm = localToNorm(mpC);
            float endY = std::clamp(nrm.y, 0.f, 1.f);
            double endTime = scanPageYToTime(m_scanCurrentPage, endY);
            if (!altHeld) endTime = (double)snapToScanMarker((float)endTime, 0.06f);

            double startTime = (double)m_scanHoldStartT;
            if (endTime < startTime + 0.02) endTime = startTime + 0.02;

            EditorNote n{};
            n.type             = EditorNoteType::Hold;
            n.time             = (float)startTime;
            n.endTime          = (float)endTime;
            n.scanX            = m_scanHoldStartX;
            n.scanY            = m_scanHoldStartY;
            n.scanEndY         = endY;
            n.scanHoldSweeps   = std::max(0, m_scanCurrentPage - m_scanHoldStartPage);
            notes().push_back(n);
            m_scanHoldAwaitEnd = false;
        }
        return;
    }

    // ── Slide authoring state ──────────────────────────────────────────────
    if (m_scanSlideDragging) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_scanSlideDragging = false;
            m_scanSlideDraft    = {};
            m_scanSlidePathPages.clear();
            return;
        }
        if (!lmbDown) {
            // Commit on LMB release with >=2 points.
            if (m_scanSlideDraft.scanPath.size() >= 2 &&
                m_scanSlidePathPages.size() == m_scanSlideDraft.scanPath.size())
            {
                auto& draft = m_scanSlideDraft;
                // Compute absolute time for each node.
                std::vector<double> absT(draft.scanPath.size());
                for (size_t i = 0; i < draft.scanPath.size(); ++i) {
                    int pi = m_scanSlidePathPages[i];
                    absT[i] = scanPageYToTime(pi, draft.scanPath[i].second);
                }
                double t0 = absT.front();
                double t1 = absT.back();
                if (t1 <= t0 + 0.02) t1 = t0 + 0.02;
                draft.time     = (float)t0;
                draft.endTime  = (float)t1;
                draft.samplePoints.clear();
                for (size_t i = 1; i < absT.size(); ++i)
                    draft.samplePoints.push_back((float)(absT[i] - t0));
                notes().push_back(draft);
            }
            m_scanSlideDragging = false;
            m_scanSlideDraft    = {};
            m_scanSlidePathPages.clear();
            return;
        }
        // RMB: append a control point on the current page.
        if (hovered && inRect && rmbClick) {
            ImVec2 mpC(std::clamp(mp.x, origin.x, pMax.x),
                       std::clamp(mp.y, origin.y, pMax.y));
            ImVec2 nrm = localToNorm(mpC);
            float nx = std::clamp(nrm.x, 0.f, 1.f);
            float ny = std::clamp(nrm.y, 0.f, 1.f);

            // Per-page monotonicity: within the same page, enforce that the
            // new node lies in the time-forward direction from the previous
            // one. Across pages, no constraint (time advances naturally).
            if (!m_scanSlidePathPages.empty() &&
                m_scanSlidePathPages.back() == m_scanCurrentPage)
            {
                float prevY = m_scanSlideDraft.scanPath.back().second;
                bool forward = page.goingUp ? (ny <= prevY + 1e-3f)
                                            : (ny >= prevY - 1e-3f);
                if (!forward) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
                    return;
                }
            }

            m_scanSlideDraft.scanPath.emplace_back(nx, ny);
            m_scanSlidePathPages.push_back(m_scanCurrentPage);
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        return;
    }

    // ── Idle: handle fresh clicks in the body rect ─────────────────────────
    if (!hovered || !inRect) return;

    if (m_noteTool == NoteTool::None) {
        // No tool → click to select nearest note.
        if (!lmbClick) return;
        auto scanToLocalB = [&](float nx, float ny) -> ImVec2 {
            return ImVec2(origin.x + nx * size.x, origin.y + ny * size.y);
        };
        for (size_t ni = 0; ni < notes().size(); ++ni) {
            const auto& n = notes()[ni];
            if (n.type != EditorNoteType::Tap &&
                n.type != EditorNoteType::Flick &&
                n.type != EditorNoteType::Hold &&
                n.type != EditorNoteType::Slide) continue;
            double pStart = page.startTime;
            double pEnd   = page.startTime + page.duration;
            if (n.time < pStart || n.time > pEnd) continue;
            float y01 = scanPageTimeToY(m_scanCurrentPage, n.time);
            ImVec2 h = scanToLocalB(n.scanX, y01);
            float dx = mp.x - h.x, dy = mp.y - h.y;
            if (dx * dx + dy * dy < 14.f * 14.f) {
                m_selectedNoteIdx = (int)ni;
                break;
            }
        }
        return;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (!lmbClick) return;

    ImVec2 nrm = localToNorm(mp);
    float nx = std::clamp(nrm.x, 0.f, 1.f);
    float ny = std::clamp(nrm.y, 0.f, 1.f);
    double clickTime = scanPageYToTime(m_scanCurrentPage, ny);
    if (!altHeld) {
        float snapTol = std::min(0.06f, 0.15f * (float)page.duration);
        float snapped = snapToScanMarker((float)clickTime, snapTol);
        if (std::abs(snapped - (float)clickTime) > 1e-6f) {
            clickTime = (double)snapped;
            ny = scanPageTimeToY(m_scanCurrentPage, clickTime);
        }
    }

    switch (m_noteTool) {
        case NoteTool::Tap: {
            EditorNote n{};
            n.type  = EditorNoteType::Tap;
            n.time  = (float)clickTime;
            n.scanX = nx;
            n.scanY = ny;
            notes().push_back(n);
            break;
        }
        case NoteTool::Flick: {
            EditorNote n{};
            n.type  = EditorNoteType::Flick;
            n.time  = (float)clickTime;
            n.scanX = nx;
            n.scanY = ny;
            notes().push_back(n);
            break;
        }
        case NoteTool::Hold: {
            m_scanHoldAwaitEnd    = true;
            m_scanHoldStartPage   = m_scanCurrentPage;
            m_scanHoldStartX      = nx;
            m_scanHoldStartY      = ny;
            m_scanHoldStartT      = (float)clickTime;
            m_scanHoldGoingUp     = page.goingUp;
            m_scanHoldExtraSweeps = 0;
            break;
        }
        case NoteTool::Slide: {
            m_scanSlideDragging         = true;
            m_scanSlideGoingUp          = page.goingUp;
            m_scanSlideDraft            = {};
            m_scanSlideDraft.type       = EditorNoteType::Slide;
            m_scanSlideDraft.time       = (float)clickTime;
            m_scanSlideDraft.scanX      = nx;
            m_scanSlideDraft.scanY      = ny;
            m_scanSlideDraft.scanPath.clear();
            m_scanSlideDraft.scanPath.emplace_back(nx, ny);
            m_scanSlidePathPages.clear();
            m_scanSlidePathPages.push_back(m_scanCurrentPage);
            break;
        }
        default: break;
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
            case EditorNoteType::Arc: {
                if (en.arcWaypoints.size() >= 2) {
                    // Decompose multi-waypoint arc into N-1 segments
                    const auto& wps = en.arcWaypoints;
                    for (size_t s = 0; s + 1 < wps.size(); ++s) {
                        NoteEvent segEv;
                        segEv.time = wps[s].time;
                        segEv.type = NoteType::Arc;
                        ArcData ad{};
                        ad.startPos   = glm::vec2(wps[s].x, wps[s].y);
                        ad.endPos     = glm::vec2(wps[s+1].x, wps[s+1].y);
                        ad.duration   = wps[s+1].time - wps[s].time;
                        ad.curveXEase = wps[s+1].easeX;
                        ad.curveYEase = wps[s+1].easeY;
                        ad.color      = en.arcColor;
                        ad.isVoid     = en.arcIsVoid;
                        segEv.data = std::move(ad);
                        chart.notes.push_back(segEv);
                    }
                    continue;  // skip the default push_back below
                }
                // Legacy 2-endpoint arc
                ev.type = NoteType::Arc;
                ArcData ad{};
                ad.startPos   = glm::vec2(en.arcStartX, en.arcStartY);
                ad.endPos     = glm::vec2(en.arcEndX, en.arcEndY);
                ad.duration   = en.endTime - en.time;
                ad.curveXEase = en.arcEaseX;
                ad.curveYEase = en.arcEaseY;
                ad.color      = en.arcColor;
                ad.isVoid     = en.arcIsVoid;
                ev.data = std::move(ad);
                break;
            }
            case EditorNoteType::ArcTap: {
                ev.type = NoteType::ArcTap;
                TapData td{};
                if (en.arcTapParent >= 0 && en.arcTapParent < (int)edNotes.size()) {
                    const auto& parent = edNotes[en.arcTapParent];
                    float dur = parent.endTime - parent.time;
                    float tP = std::clamp((en.time - parent.time) / std::max(0.001f, dur), 0.f, 1.f);
                    glm::vec2 pos = evalArcEditor(parent, tP);
                    td.laneX = pos.x;
                    td.scanY = pos.y;
                }
                ev.data = std::move(td);
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

    chart.scanSpeedEvents   = scanSpeed();
    chart.scanPageOverrides = scanPages();

    // Persist beat markers for this (mode, difficulty) so reopening the
    // project restores them. Runtime consumers ignore the field.
    chart.markers = markers();

    return chart;
}

// ── exportAllCharts ─────────────────────────────────────────────────────────

void SongEditor::exportAllCharts() {
    if (!m_song || m_projectPath.empty()) return;

    const char* diffSuffix[] = {"easy", "medium", "hard"};
    Difficulty  diffs[]      = {Difficulty::Easy, Difficulty::Medium, Difficulty::Hard};

    for (int d = 0; d < 3; d++) {
        auto& edNotes   = m_diffNotes[d];
        auto& edMarkers = m_diffMarkers[d];
        // Persist even note-less difficulties so their authored markers round-trip.
        if (edNotes.empty() && edMarkers.empty()) continue;

        // Save current difficulty, temporarily switch, build chart, restore
        Difficulty saved = m_currentDifficulty;
        m_currentDifficulty = diffs[d];
        ChartData chart = buildChartFromNotes();
        m_currentDifficulty = saved;

        // Write unified chart JSON. Filename is keyed on both the game mode
        // and difficulty so each (mode, difficulty) pair owns its own file.
        std::string relPath  = chartRelPathFor(m_song->name, m_song->gameMode, diffSuffix[d]);
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
                case NoteType::Tap:    f << "\"tap\"";    break;
                case NoteType::Hold:   f << "\"hold\"";   break;
                case NoteType::Slide:  f << "\"slide\"";  break;
                case NoteType::Flick:  f << "\"flick\"";  break;
                case NoteType::Arc:    f << "\"arc\"";    break;
                case NoteType::ArcTap: f << "\"arctap\""; break;
                default:               f << "\"tap\"";    break;
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

            // ── Arc-specific fields ──────────────────────────────────────
            if (n.type == NoteType::Arc) {
                if (auto* arc = std::get_if<ArcData>(&n.data)) {
                    f << ", \"startX\": " << arc->startPos.x
                      << ", \"startY\": " << arc->startPos.y
                      << ", \"endX\": "   << arc->endPos.x
                      << ", \"endY\": "   << arc->endPos.y
                      << ", \"duration\": " << arc->duration
                      << ", \"easeX\": "  << arc->curveXEase
                      << ", \"easeY\": "  << arc->curveYEase
                      << ", \"color\": "  << arc->color
                      << ", \"void\": "   << (arc->isVoid ? "true" : "false");
                }
            }
            // ── ArcTap position ──────────────────────────────────────────
            if (n.type == NoteType::ArcTap) {
                if (auto* tap = std::get_if<TapData>(&n.data)) {
                    f << ", \"arcX\": " << tap->laneX
                      << ", \"arcY\": " << tap->scanY;
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

        // ── Scan-line per-page speed overrides (authoritative when present) ─
        // scanPageOverrides is the edit-time source of truth. scanSpeedEvents
        // is regenerated from it at load; skip writing scanSpeedEvents when
        // overrides are present to avoid round-trip drift.
        if (!chart.scanPageOverrides.empty()) {
            f << ",\n \"scanPages\": [";
            for (size_t pi = 0; pi < chart.scanPageOverrides.size(); ++pi) {
                const auto& p = chart.scanPageOverrides[pi];
                if (pi) f << ", ";
                f << "{\"index\": " << p.pageIndex
                  << ", \"speed\": " << p.speed << "}";
            }
            f << "]";
        } else if (!chart.scanSpeedEvents.empty()) {
            // Legacy path: only write scanSpeedEvents if no page overrides.
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

        // ── Beat markers for this difficulty ─────────────────────────
        // Persisted so AI-detected or hand-placed markers reload with
        // the chart. Applies to every game mode.
        if (!chart.markers.empty()) {
            f << ",\n \"markers\": [";
            for (size_t mi = 0; mi < chart.markers.size(); ++mi) {
                if (mi) f << ", ";
                f << chart.markers[mi];
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
                m_arcPlacing    = false;
                m_arcDraft      = EditorNote{};
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

    // Arc tools: only in 3D DropNotes mode
    if (is3D) {
        toolBtn("Arc",    NoteTool::Arc,    ImVec4(0.3f, 0.7f, 0.9f, 1.f));
        // Arc color picker (inline, no style push/pop)
        if (m_noteTool == NoteTool::Arc) {
            ImGui::SameLine();
            const char* colorLabel = (m_arcDraftColor == 0) ? "[Cyan]" : "[Pink]";
            ImGui::TextColored(
                m_arcDraftColor == 0 ? ImVec4(0.3f, 0.8f, 1.f, 1.f) : ImVec4(1.f, 0.4f, 0.7f, 1.f),
                "%s", colorLabel);
            ImGui::SameLine();
            if (ImGui::SmallButton("C##cyan")) m_arcDraftColor = 0;
            ImGui::SameLine();
            if (ImGui::SmallButton("P##pink")) m_arcDraftColor = 1;
        }
        toolBtn("ArcTap", NoteTool::ArcTap, ImVec4(0.8f, 0.5f, 0.2f, 1.f));
    }

    // Show drag-recording indicator
    if (m_holdDragging && m_noteTool == NoteTool::Hold) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f),
                           "Recording... (drag, release to commit)");
    }

    // Show arc placement hint
    if (m_arcPlacing && m_noteTool == NoteTool::Arc) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.f, 1.f),
                           "Click to add waypoints, R-click/Enter to finish, Esc to cancel");
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
                        m_scanPageTableDirty = true;  // re-derive page durations from detected BPM

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

// ── Arc helper functions ────────────────────────────────────────────────────

float SongEditor::trackToArcX(int track, int trackCount) {
    if (trackCount <= 1) return 0.5f;
    return static_cast<float>(track) / static_cast<float>(trackCount - 1);
}

int SongEditor::arcXToTrack(float arcX, int trackCount) {
    int t = static_cast<int>(arcX * (trackCount - 1) + 0.5f);
    return std::clamp(t, 0, trackCount - 1);
}

glm::vec2 SongEditor::evalArcEditor(const EditorNote& arc, float t) {
    auto ease = [](float t, float e) -> float {
        if (e == 0.f) return t;
        return e > 0.f ? 1.f - powf(1.f - t, e + 1.f) : powf(t, -e + 1.f);
    };

    // ── Multi-waypoint path ────────────────────────────────────────────────
    if (arc.arcWaypoints.size() >= 2) {
        // t is normalized [0..1] over the full arc duration
        float totalDur = arc.endTime - arc.time;
        if (totalDur < 0.0001f) return {arc.arcWaypoints[0].x, arc.arcWaypoints[0].y};
        float absTime = arc.time + t * totalDur;

        // Find the segment containing absTime
        const auto& wps = arc.arcWaypoints;
        size_t seg = 0;
        for (size_t i = 0; i + 1 < wps.size(); ++i) {
            if (absTime <= wps[i + 1].time || i + 2 == wps.size()) { seg = i; break; }
        }
        const auto& a = wps[seg];
        const auto& b = wps[seg + 1];
        float segDur = b.time - a.time;
        float u = (segDur > 0.0001f) ? std::clamp((absTime - a.time) / segDur, 0.f, 1.f) : 1.f;
        float x = a.x + (b.x - a.x) * ease(u, b.easeX);
        float y = a.y + (b.y - a.y) * ease(u, b.easeY);
        return {x, y};
    }

    // ── Legacy 2-endpoint path ─────────────────────────────────────────────
    float x = arc.arcStartX + (arc.arcEndX - arc.arcStartX) * ease(t, arc.arcEaseX);
    float y = arc.arcStartY + (arc.arcEndY - arc.arcStartY) * ease(t, arc.arcEaseY);
    return {x, y};
}

void SongEditor::ensureArcWaypoints(EditorNote& arc) {
    if (!arc.arcWaypoints.empty()) return;
    // Migrate legacy 2-endpoint arc into 2 waypoints
    ArcWaypoint a, b;
    a.time = arc.time;     a.x = arc.arcStartX; a.y = arc.arcStartY;
    a.easeX = 0.f;         a.easeY = 0.f;
    b.time = arc.endTime;  b.x = arc.arcEndX;   b.y = arc.arcEndY;
    b.easeX = arc.arcEaseX; b.easeY = arc.arcEaseY;
    arc.arcWaypoints = {a, b};
}

void SongEditor::fixupArcTapParents(int deletedIdx) {
    for (auto& n : notes()) {
        if (n.type != EditorNoteType::ArcTap) continue;
        if (n.arcTapParent == deletedIdx)
            n.arcTapParent = -1;        // orphaned
        else if (n.arcTapParent > deletedIdx)
            n.arcTapParent--;           // shift down
    }
}

// ── Arc placement (click-to-place waypoints) ──────────────────────────────

void SongEditor::handleArcPlacement(ImVec2 origin, ImVec2 size, float startTime,
                                     int trackCount, float trackH, float regionTop) {
    ImVec2 mpos = ImGui::GetMousePos();
    float rawTime = startTime + (mpos.x - origin.x) / m_timelineZoom;
    float snappedTime = snapToMarker(rawTime);
    int track = trackFromY(mpos.y, regionTop, trackH, trackCount);
    float arcX = trackToArcX(track, trackCount);

    // Cancel with Escape
    if (m_arcPlacing && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_arcPlacing = false;
        m_arcDraft = EditorNote{};
        m_statusMsg   = "Arc cancelled";
        m_statusTimer = 1.5f;
        return;
    }

    // Finish with right-click or Enter (need >=2 waypoints)
    if (m_arcPlacing && m_arcDraft.arcWaypoints.size() >= 2) {
        bool finish = ImGui::IsMouseClicked(ImGuiMouseButton_Right)
                   || ImGui::IsKeyPressed(ImGuiKey_Enter);
        if (finish) {
            // Commit the arc
            auto& wps = m_arcDraft.arcWaypoints;
            m_arcDraft.time    = wps.front().time;
            m_arcDraft.endTime = wps.back().time;
            m_arcDraft.track   = arcXToTrack(wps.front().x, trackCount);
            // Sync legacy fields from first/last waypoint
            m_arcDraft.arcStartX = wps.front().x;
            m_arcDraft.arcStartY = wps.front().y;
            m_arcDraft.arcEndX   = wps.back().x;
            m_arcDraft.arcEndY   = wps.back().y;
            notes().push_back(std::move(m_arcDraft));
            m_arcPlacing = false;
            m_arcDraft = EditorNote{};
            m_statusMsg   = "Arc placed";
            m_statusTimer = 1.5f;
            return;
        }
    }

    // Left-click adds a waypoint
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!m_arcPlacing) {
            // First waypoint — start new arc
            m_arcPlacing = true;
            m_arcDraft = EditorNote{};
            m_arcDraft.type     = EditorNoteType::Arc;
            m_arcDraft.arcColor = m_arcDraftColor;
            m_arcDraft.arcIsVoid = false;

            ArcWaypoint wp;
            wp.time = snappedTime;
            wp.x    = arcX;
            wp.y    = 0.5f;  // default mid-sky height
            wp.easeX = 0.f;
            wp.easeY = 0.f;
            m_arcDraft.arcWaypoints.push_back(wp);
        } else {
            // Subsequent waypoints — must be strictly after previous
            float prevTime = m_arcDraft.arcWaypoints.back().time;
            if (snappedTime > prevTime) {
                ArcWaypoint wp;
                wp.time = snappedTime;
                wp.x    = arcX;
                wp.y    = 0.5f;
                wp.easeX = 0.f;  // linear by default
                wp.easeY = 0.f;
                m_arcDraft.arcWaypoints.push_back(wp);
            }
        }
    }

    // Draw preview of in-progress arc
    if (m_arcPlacing && !m_arcDraft.arcWaypoints.empty()) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const auto& wps = m_arcDraft.arcWaypoints;
        float pxPerSec = m_timelineZoom;
        float regionH  = trackH * trackCount;
        auto arcXToPixelY = [&](float ax) -> float {
            return regionTop + ax * regionH;
        };
        ImU32 col = (m_arcDraftColor == 0)
            ? IM_COL32(80, 200, 255, 180) : IM_COL32(255, 100, 180, 180);

        // Draw placed segments
        for (size_t i = 0; i + 1 < wps.size(); ++i) {
            float x0 = origin.x + (wps[i].time - startTime) * pxPerSec;
            float y0 = arcXToPixelY(wps[i].x);
            float x1 = origin.x + (wps[i+1].time - startTime) * pxPerSec;
            float y1 = arcXToPixelY(wps[i+1].x);
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col, 2.5f);
        }

        // Draw waypoint dots
        for (auto& wp : wps) {
            float px = origin.x + (wp.time - startTime) * pxPerSec;
            float py = arcXToPixelY(wp.x);
            dl->AddCircleFilled(ImVec2(px, py), 5.f, col);
            dl->AddCircle(ImVec2(px, py), 6.f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
        }

        // Draw line from last waypoint to mouse cursor
        float lastPx = origin.x + (wps.back().time - startTime) * pxPerSec;
        float lastPy = arcXToPixelY(wps.back().x);
        float curPx  = origin.x + (snappedTime - startTime) * pxPerSec;
        float curPy  = arcXToPixelY(arcX);
        dl->AddLine(ImVec2(lastPx, lastPy), ImVec2(curPx, curPy),
                    (col & 0x00FFFFFF) | 0x80000000, 1.5f);

        // Status hint
        int wpCount = (int)wps.size();
        char buf[64];
        snprintf(buf, sizeof(buf), "Arc: %d waypoint%s (R-click to finish)",
                 wpCount, wpCount == 1 ? "" : "s");
        dl->AddText(ImVec2(origin.x + 4, origin.y + 2), IM_COL32(220, 220, 240, 220), buf);
    }
}

// ── ArcTap placement (snap to parent arc) ──────────────────────────────────

void SongEditor::handleArcTapPlacement(ImVec2 origin, ImVec2 size, float startTime,
                                        int trackCount, float trackH, float regionTop) {
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;

    ImVec2 mpos = ImGui::GetMousePos();
    float rawTime = startTime + (mpos.x - origin.x) / m_timelineZoom;
    float snappedTime = snapToMarker(rawTime);
    int track = trackFromY(mpos.y, regionTop, trackH, trackCount);
    float clickArcX = trackToArcX(track, trackCount);

    // Find the nearest arc that contains this time
    int bestArc = -1;
    float bestDist = 1e9f;
    for (int i = 0; i < (int)notes().size(); ++i) {
        const auto& n = notes()[i];
        if (n.type != EditorNoteType::Arc) continue;
        if (snappedTime < n.time || snappedTime > n.endTime) continue;

        float tParam = (snappedTime - n.time) / std::max(0.001f, n.endTime - n.time);
        glm::vec2 pos = evalArcEditor(n, tParam);
        float dist = std::abs(pos.x - clickArcX);
        if (dist < bestDist) {
            bestDist = dist;
            bestArc  = i;
        }
    }

    // Tolerance: within ~2 tracks
    float tolerance = (trackCount > 1) ? 2.f / (trackCount - 1) : 0.5f;
    if (bestArc < 0 || bestDist > tolerance) {
        // No parent arc — auto-spawn a short void arc at the click so that
        // the ArcTap always has somewhere to live. The author can delete or
        // convert the hidden arc later via the Arc tool.
        EditorNote parent;
        parent.type      = EditorNoteType::Arc;
        parent.time      = snappedTime;
        parent.endTime   = snappedTime + 0.5f;
        parent.track     = arcXToTrack(clickArcX, trackCount);
        parent.arcStartX = clickArcX;
        parent.arcEndX   = clickArcX;
        parent.arcStartY = 0.6f;
        parent.arcEndY   = 0.6f;
        parent.arcColor  = 0;
        parent.arcIsVoid = true;  // hidden in gameplay, visible in editor
        notes().push_back(parent);
        bestArc = (int)notes().size() - 1;
    }

    EditorNote note;
    note.type         = EditorNoteType::ArcTap;
    note.time         = snappedTime;
    note.arcTapParent = bestArc;
    notes().push_back(note);
    m_statusMsg   = "ArcTap placed";
    m_statusTimer = 1.5f;
}

// ── renderArcHeightEditor ───────────────────────────────────────────────────

void SongEditor::renderArcHeightEditor(ImDrawList* dl, ImVec2 origin, ImVec2 size) {
    if (!m_song) return;

    float pxPerSec = m_timelineZoom;
    float startTime = m_timelineScrollX;

    // Background
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                      IM_COL32(20, 20, 30, 255));

    // Label
    dl->AddText(ImVec2(origin.x + 4, origin.y + 2),
                IM_COL32(180, 180, 200, 200), "Height [0..1]");

    // Horizontal grid lines at 0, 0.25, 0.5, 0.75, 1.0
    const float pad = 14.f;
    float plotH = size.y - pad * 2;
    float plotTop = origin.y + pad;

    auto heightToY = [&](float h) -> float {
        return plotTop + (1.f - h) * plotH;
    };
    auto yToHeight = [&](float y) -> float {
        return std::clamp(1.f - (y - plotTop) / plotH, 0.f, 1.f);
    };

    for (float h : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
        float y = heightToY(h);
        ImU32 col = (h == 0.f || h == 1.f) ? IM_COL32(80, 80, 100, 180)
                                             : IM_COL32(50, 50, 70, 120);
        dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y), col, 1.f);

        char buf[8];
        snprintf(buf, sizeof(buf), "%.2f", h);
        dl->AddText(ImVec2(origin.x + size.x - 30, y - 6), IM_COL32(120, 120, 150, 180), buf);
    }

    // Draw each arc's height curve with per-waypoint handles
    constexpr int SEG_SAMPLES = 8;  // samples per segment
    constexpr float HANDLE_R = 6.f;

    for (int ni = 0; ni < (int)notes().size(); ++ni) {
        auto& note = notes()[ni];
        if (note.type != EditorNoteType::Arc) continue;

        float dur = note.endTime - note.time;
        if (dur < 0.0001f) continue;

        float x0 = origin.x + (note.time - startTime) * pxPerSec;
        float x1 = origin.x + (note.endTime - startTime) * pxPerSec;
        if (x1 < origin.x || x0 > origin.x + size.x) continue;

        ImU32 col = (note.arcColor == 0)
            ? IM_COL32(80, 200, 255, 200)
            : IM_COL32(255, 100, 180, 200);

        // Multi-waypoint: draw height curve with per-segment sampling
        if (note.arcWaypoints.size() >= 2) {
            const auto& wps = note.arcWaypoints;
            // Draw polyline through all segments
            std::vector<ImVec2> pts;
            for (size_t s = 0; s + 1 < wps.size(); ++s) {
                for (int k = 0; k <= SEG_SAMPLES; ++k) {
                    if (s > 0 && k == 0) continue; // avoid duplicating junction
                    float localT = static_cast<float>(k) / SEG_SAMPLES;
                    float absT = wps[s].time + localT * (wps[s+1].time - wps[s].time);
                    float normT = (absT - note.time) / dur;
                    glm::vec2 pos = evalArcEditor(note, normT);
                    float px = origin.x + (absT - startTime) * pxPerSec;
                    pts.push_back(ImVec2(px, heightToY(pos.y)));
                }
            }
            if (pts.size() >= 2)
                dl->AddPolyline(pts.data(), (int)pts.size(), col, ImDrawFlags_None, 2.f);

            // Draw per-waypoint handles
            ImVec2 mpos = ImGui::GetMousePos();
            for (int wi = 0; wi < (int)wps.size(); ++wi) {
                float hx = origin.x + (wps[wi].time - startTime) * pxPerSec;
                float hy = heightToY(wps[wi].y);
                ImVec2 handle(hx, hy);

                ImU32 hcol = (ni == m_selectedNoteIdx) ? IM_COL32(255, 255, 255, 255) : col;
                dl->AddCircleFilled(handle, HANDLE_R, hcol);
                dl->AddCircle(handle, HANDLE_R, IM_COL32(255, 255, 255, 150), 0, 1.5f);

                // Click to start dragging this waypoint's height
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                    && std::abs(mpos.x - hx) < HANDLE_R + 3.f
                    && std::abs(mpos.y - hy) < HANDLE_R + 3.f) {
                    m_heightDragArc = ni;
                    m_heightDragWp  = wi;
                    m_selectedNoteIdx = ni;
                }
            }
        } else {
            // Legacy 2-endpoint: draw simple curve
            constexpr int LEGACY_SAMPLES = 20;
            ImVec2 pts[LEGACY_SAMPLES + 1];
            for (int s = 0; s <= LEGACY_SAMPLES; ++s) {
                float t = static_cast<float>(s) / LEGACY_SAMPLES;
                glm::vec2 pos = evalArcEditor(note, t);
                float px = x0 + t * (x1 - x0);
                pts[s] = ImVec2(px, heightToY(pos.y));
            }
            dl->AddPolyline(pts, LEGACY_SAMPLES + 1, col, ImDrawFlags_None, 2.f);

            // Start/end handles (legacy)
            ImVec2 startHandle(x0, heightToY(note.arcStartY));
            ImVec2 endHandle(x1, heightToY(note.arcEndY));
            ImU32 hcol = (ni == m_selectedNoteIdx) ? IM_COL32(255, 255, 255, 255) : col;
            dl->AddCircleFilled(startHandle, HANDLE_R, hcol);
            dl->AddCircleFilled(endHandle,   HANDLE_R, hcol);
            dl->AddCircle(startHandle, HANDLE_R, IM_COL32(255, 255, 255, 150), 0, 1.5f);
            dl->AddCircle(endHandle,   HANDLE_R, IM_COL32(255, 255, 255, 150), 0, 1.5f);

            ImVec2 mpos = ImGui::GetMousePos();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (std::abs(mpos.x - startHandle.x) < HANDLE_R + 2.f
                    && std::abs(mpos.y - startHandle.y) < HANDLE_R + 2.f) {
                    m_heightDragArc = ni;
                    m_heightDragWp  = 0;  // start
                    m_selectedNoteIdx = ni;
                } else if (std::abs(mpos.x - endHandle.x) < HANDLE_R + 2.f
                           && std::abs(mpos.y - endHandle.y) < HANDLE_R + 2.f) {
                    m_heightDragArc = ni;
                    m_heightDragWp  = 1;  // end
                    m_selectedNoteIdx = ni;
                }
            }
        }
    }

    // Process active drag
    if (m_heightDragArc >= 0 && m_heightDragArc < (int)notes().size()) {
        auto& note = notes()[m_heightDragArc];
        if (note.type == EditorNoteType::Arc && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float h = yToHeight(ImGui::GetMousePos().y);
            if (!note.arcWaypoints.empty() && m_heightDragWp >= 0
                && m_heightDragWp < (int)note.arcWaypoints.size()) {
                note.arcWaypoints[m_heightDragWp].y = h;
                // Sync legacy fields
                note.arcStartY = note.arcWaypoints.front().y;
                note.arcEndY   = note.arcWaypoints.back().y;
            } else {
                // Legacy 2-endpoint
                if (m_heightDragWp == 0)
                    note.arcStartY = h;
                else
                    note.arcEndY = h;
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_heightDragArc = -1;
            m_heightDragWp  = -1;
        }
    }
}

// ── renderArcNotes (timeline arc ribbons + arctap diamonds) ────────────────

void SongEditor::renderArcNotes(ImDrawList* dl, ImVec2 origin, ImVec2 size,
                                 float startTime, int trackCount,
                                 float trackH, float regionTop) {
    if (!m_song) return;

    float pxPerSec = m_timelineZoom;
    float regionH  = trackH * trackCount;

    auto arcXToPixelY = [&](float ax) -> float {
        return regionTop + ax * regionH;
    };

    // ── Draw arc ribbons ───────────────────────────────────────────────────
    for (int ni = 0; ni < (int)notes().size(); ++ni) {
        const auto& note = notes()[ni];
        if (note.type != EditorNoteType::Arc) continue;
        if (note.arcIsVoid) continue;

        float dur = note.endTime - note.time;
        if (dur < 0.0001f) continue;

        float x0 = origin.x + (note.time - startTime) * pxPerSec;
        float x1 = origin.x + (note.endTime - startTime) * pxPerSec;
        if (x1 < origin.x || x0 > origin.x + size.x) continue;

        ImU32 col = (note.arcColor == 0)
            ? IM_COL32(80, 200, 255, 160) : IM_COL32(255, 100, 180, 160);

        // Sample the arc curve and draw as a polyline ribbon
        constexpr int SAMPLES = 32;
        constexpr float HALF_W = 3.f;
        ImVec2 top[SAMPLES + 1], bot[SAMPLES + 1];
        for (int s = 0; s <= SAMPLES; ++s) {
            float t = static_cast<float>(s) / SAMPLES;
            glm::vec2 pos = evalArcEditor(note, t);
            float px = x0 + t * (x1 - x0);
            float py = arcXToPixelY(pos.x);
            top[s] = ImVec2(px, py - HALF_W);
            bot[s] = ImVec2(px, py + HALF_W);
        }

        for (int s = 0; s < SAMPLES; ++s) {
            dl->AddQuadFilled(top[s], top[s + 1], bot[s + 1], bot[s], col);
        }

        // Waypoint handles (multi-waypoint arcs)
        ImU32 capCol = (note.arcColor == 0)
            ? IM_COL32(80, 200, 255, 220) : IM_COL32(255, 100, 180, 220);

        if (note.arcWaypoints.size() >= 2) {
            for (size_t wi = 0; wi < note.arcWaypoints.size(); ++wi) {
                const auto& wp = note.arcWaypoints[wi];
                float wpPx = origin.x + (wp.time - startTime) * pxPerSec;
                float wpPy = arcXToPixelY(wp.x);
                float r = (wi == 0 || wi == note.arcWaypoints.size() - 1) ? 5.f : 4.f;
                dl->AddCircleFilled(ImVec2(wpPx, wpPy), r, capCol);
                dl->AddCircle(ImVec2(wpPx, wpPy), r + 1.f,
                              IM_COL32(255, 255, 255, 150), 0, 1.f);
            }
        } else {
            // Legacy: start/end caps only
            dl->AddCircleFilled(ImVec2(x0, arcXToPixelY(note.arcStartX)), 5.f, capCol);
            dl->AddCircleFilled(ImVec2(x1, arcXToPixelY(note.arcEndX)),   5.f, capCol);
        }

        // Selection highlight
        if (ni == m_selectedNoteIdx) {
            for (int s = 0; s < SAMPLES; ++s) {
                dl->AddQuad(top[s], top[s + 1], bot[s + 1], bot[s],
                            IM_COL32(255, 255, 255, 200), 1.5f);
            }
        }
    }

    // ── Draw ArcTap diamonds ───────────────────────────────────────────────
    for (int ni = 0; ni < (int)notes().size(); ++ni) {
        const auto& note = notes()[ni];
        if (note.type != EditorNoteType::ArcTap) continue;
        if (note.arcTapParent < 0 || note.arcTapParent >= (int)notes().size()) continue;

        const auto& parent = notes()[note.arcTapParent];
        if (parent.type != EditorNoteType::Arc) continue;

        float dur = parent.endTime - parent.time;
        float tParam = (dur < 0.0001f) ? 0.f
                        : std::clamp((note.time - parent.time) / dur, 0.f, 1.f);
        glm::vec2 pos = evalArcEditor(parent, tParam);

        float px = origin.x + (note.time - startTime) * pxPerSec;
        float py = arcXToPixelY(pos.x);
        if (px < origin.x || px > origin.x + size.x) continue;

        constexpr float R = 6.f;
        ImVec2 pts[4] = {
            ImVec2(px, py - R), ImVec2(px + R, py),
            ImVec2(px, py + R), ImVec2(px - R, py)
        };
        dl->AddConvexPolyFilled(pts, 4, IM_COL32(255, 180, 60, 220));
        if (ni == m_selectedNoteIdx) {
            dl->AddPolyline(pts, 4, IM_COL32(255, 255, 255, 220), ImDrawFlags_Closed, 2.f);
        }
    }

    // ── Hit-test for selection ─────────────────────────────────────────────
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && m_noteTool != NoteTool::Arc && m_noteTool != NoteTool::ArcTap) {
        ImVec2 mpos = ImGui::GetMousePos();
        // Check ArcTaps first
        for (int ni = 0; ni < (int)notes().size(); ++ni) {
            const auto& n = notes()[ni];
            if (n.type != EditorNoteType::ArcTap) continue;
            if (n.arcTapParent < 0 || n.arcTapParent >= (int)notes().size()) continue;
            const auto& parent = notes()[n.arcTapParent];
            float dur = parent.endTime - parent.time;
            float tP = (dur < 0.0001f) ? 0.f
                         : std::clamp((n.time - parent.time) / dur, 0.f, 1.f);
            glm::vec2 pos = evalArcEditor(parent, tP);
            float px = origin.x + (n.time - startTime) * pxPerSec;
            float py = arcXToPixelY(pos.x);
            if (std::abs(mpos.x - px) < 8.f && std::abs(mpos.y - py) < 8.f) {
                m_selectedNoteIdx = ni;
                return;
            }
        }
        // Check Arcs
        for (int ni = 0; ni < (int)notes().size(); ++ni) {
            const auto& n = notes()[ni];
            if (n.type != EditorNoteType::Arc) continue;
            float dur = n.endTime - n.time;
            if (dur < 0.0001f) continue;
            float x0 = origin.x + (n.time - startTime) * pxPerSec;
            float x1 = origin.x + (n.endTime - startTime) * pxPerSec;
            if (mpos.x < x0 - 5.f || mpos.x > x1 + 5.f) continue;
            float tP = std::clamp((mpos.x - x0) / (x1 - x0), 0.f, 1.f);
            glm::vec2 pos = evalArcEditor(n, tP);
            float py = arcXToPixelY(pos.x);
            if (std::abs(mpos.y - py) < 10.f) {
                m_selectedNoteIdx = ni;
                return;
            }
        }
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
            case EditorNoteType::Tap:    return IM_COL32(40, 160, 220, 80);
            case EditorNoteType::Hold:   return IM_COL32(40, 200, 80, 80);
            case EditorNoteType::Slide:  return IM_COL32(180, 60, 200, 80);
            case EditorNoteType::Flick:  return IM_COL32(220, 150, 40, 80);
            case EditorNoteType::Arc:    return IM_COL32(60, 180, 230, 80);
            case EditorNoteType::ArcTap: return IM_COL32(230, 160, 50, 80);
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
        // Arc/ArcTap are rendered by renderArcNotes — skip here
        if (note.type == EditorNoteType::Arc || note.type == EditorNoteType::ArcTap)
            continue;
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

    int   tc     = gm.trackCount;
    float baseR  = gm.diskBaseRadius   > 0.f ? gm.diskBaseRadius   : 2.4f;
    float initS  = gm.diskInitialScale > 0.f ? gm.diskInitialScale : 1.f;
    for (double t : samples) {
        glm::vec2 c    = sampleDiskCenter  (t, diskMove());
        float     s    = sampleDiskScale   (t, diskScale()) * initS;
        float     r    = sampleDiskRotation(t, diskRot());
        uint32_t  mask = laneMaskForTransform(tc, baseR, c, s, r);
        if (m_laneMaskTimeline.empty() || m_laneMaskTimeline.back().mask != mask)
            m_laneMaskTimeline.push_back({t, mask});
    }
    if (m_laneMaskTimeline.empty())
        m_laneMaskTimeline.push_back({0.0, laneMaskForTransform(tc, baseR, {0.f,0.f}, initS, 0.f)});
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
