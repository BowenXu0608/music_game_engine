#include "MusicSelectionEditor.h"
#include "SettingsPageUI.h"
#include "StartScreenEditor.h"
#include "engine/Engine.h"
#include "renderer/MaterialAssetLibrary.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/BufferManager.h"
#include "ui/PreviewAspect.h"
#include <imgui_internal.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#pragma comment(lib, "comdlg32.lib")
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// ── Vulkan lifecycle ─────────────────────────────────────────────────────────

void MusicSelectionEditor::initVulkan(VulkanContext& ctx, BufferManager& bufMgr,
                                      ImGuiLayer& imgui, GLFWwindow* window) {
    m_ctx    = &ctx;
    m_bufMgr = &bufMgr;
    m_imgui  = &imgui;
    m_window = window;
}

void MusicSelectionEditor::shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr) {
    clearCovers();
    clearThumbnails();
}

// ── Cover texture cache ──────────────────────────────────────────────────────

void MusicSelectionEditor::clearCovers() {
    if (!m_ctx || !m_bufMgr) return;
    for (auto& [path, entry] : m_coverCache) {
        if (entry.tex.image != VK_NULL_HANDLE) {
            vkDestroySampler(m_ctx->device(), entry.tex.sampler, nullptr);
            vkDestroyImageView(m_ctx->device(), entry.tex.view, nullptr);
            vmaDestroyImage(m_bufMgr->allocator(), entry.tex.image, entry.tex.allocation);
        }
    }
    m_coverCache.clear();
}

VkDescriptorSet MusicSelectionEditor::getCoverDesc(const std::string& relPath) {
    if (relPath.empty()) return VK_NULL_HANDLE;
    auto it = m_coverCache.find(relPath);
    if (it != m_coverCache.end()) return it->second.desc;
    if (!m_ctx || !m_bufMgr || !m_imgui) return VK_NULL_HANDLE;

    std::string fullPath = m_projectPath + "/" + relPath;
    try {
        CoverEntry entry;
        TextureManager texMgr;
        texMgr.init(*m_ctx, *m_bufMgr);
        entry.tex  = texMgr.loadFromFile(*m_ctx, *m_bufMgr, fullPath);
        entry.desc = m_imgui->addTexture(entry.tex.view, entry.tex.sampler);
        auto& stored = m_coverCache[relPath] = std::move(entry);
        return stored.desc;
    } catch (...) {
        m_coverCache[relPath] = {};
        return VK_NULL_HANDLE;
    }
}

// ── Thumbnail cache (for asset panel) ────────────────────────────────────────

void MusicSelectionEditor::clearThumbnails() {
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

VkDescriptorSet MusicSelectionEditor::getThumb(const std::string& relPath) {
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

// ── importFiles ──────────────────────────────────────────────────────────────

void MusicSelectionEditor::importFiles(const std::vector<std::string>& srcPaths) {
    int copied = importAssetsToProject(m_projectPath, srcPaths);
    if (copied > 0) {
        m_statusMsg   = "Imported " + std::to_string(copied) + " file(s)";
        m_statusTimer = 3.f;
    }
    m_assetsScanned = false;
}

// ── JSON load / save ─────────────────────────────────────────────────────────

void MusicSelectionEditor::load(const std::string& projectPath) {
    m_projectPath = projectPath;
    clearCovers();
    m_sets.clear();
    m_selectedSet  = -1;
    m_selectedSong = -1;

    std::string configPath = projectPath + "/music_selection.json";
    std::ifstream f(configPath);
    if (!f.is_open()) {
        m_loaded = true;
        return;
    }

    json j;
    try { j = json::parse(f); } catch (...) { m_loaded = true; return; }

    m_pageBackground = j.value("background", "");
    m_fcImage        = j.value("fcImage", "");
    m_apImage        = j.value("apImage", "");

    if (j.contains("sets") && j["sets"].is_array()) {
        for (auto& sj : j["sets"]) {
            MusicSetInfo set;
            set.name       = sj.value("name", "Untitled Set");
            set.coverImage = sj.value("coverImage", "");
            if (sj.contains("songs") && sj["songs"].is_array()) {
                for (auto& songJ : sj["songs"]) {
                    SongInfo song;
                    song.name        = songJ.value("name", "Untitled");
                    song.artist      = songJ.value("artist", "");
                    song.coverImage  = songJ.value("coverImage", "");
                    song.audioFile   = songJ.value("audioFile", "");
                    song.chartEasy   = songJ.value("chartEasy", "");
                    song.chartMedium = songJ.value("chartMedium", "");
                    song.chartHard   = songJ.value("chartHard", "");
                    song.score       = songJ.value("score", 0);
                    song.achievement = songJ.value("achievement", "");
                    song.scoreEasy         = songJ.value("scoreEasy",   0);
                    song.scoreMedium       = songJ.value("scoreMedium", 0);
                    song.scoreHard         = songJ.value("scoreHard",   0);
                    song.achievementEasy   = songJ.value("achievementEasy",   "");
                    song.achievementMedium = songJ.value("achievementMedium", "");
                    song.achievementHard   = songJ.value("achievementHard",   "");
                    song.previewStart      = songJ.value("previewStart",   -1.f);
                    song.previewDuration   = songJ.value("previewDuration", 30.f);
                    // per-song game mode config
                    if (songJ.contains("gameMode") && songJ["gameMode"].is_object()) {
                        auto& gm = songJ["gameMode"];
                        std::string t = gm.value("type", "dropNotes");
                        if (t == "circle")        song.gameMode.type = GameModeType::Circle;
                        else if (t == "scanLine") song.gameMode.type = GameModeType::ScanLine;
                        else                      song.gameMode.type = GameModeType::DropNotes;
                        song.gameMode.trackCount = gm.value("trackCount", 7);
                        std::string dim = gm.value("dimension", "2D");
                        song.gameMode.dimension = (dim == "3D") ? DropDimension::ThreeD
                                                                : DropDimension::TwoD;
                        song.gameMode.audioOffset  = gm.value("audioOffset", 0.f);
                        song.gameMode.perfectMs    = gm.value("perfectMs", 50.f);
                        song.gameMode.goodMs       = gm.value("goodMs", 100.f);
                        song.gameMode.badMs        = gm.value("badMs", 150.f);
                        song.gameMode.perfectScore = gm.value("perfectScore", 1000);
                        song.gameMode.goodScore    = gm.value("goodScore", 600);
                        song.gameMode.badScore     = gm.value("badScore", 200);
                        song.gameMode.fcImage      = gm.value("fcImage", "");
                        song.gameMode.apImage      = gm.value("apImage", "");

                        // HUD text configs
                        auto loadHud = [](const json& j, const char* key, HudTextConfig& h) {
                            if (!j.contains(key) || !j[key].is_object()) return;
                            auto& hj = j[key];
                            if (hj.contains("pos") && hj["pos"].is_array() && hj["pos"].size() >= 2) {
                                h.pos[0] = hj["pos"][0].get<float>();
                                h.pos[1] = hj["pos"][1].get<float>();
                            }
                            h.fontSize = hj.value("fontSize", h.fontSize);
                            h.scale    = hj.value("scale", h.scale);
                            h.bold     = hj.value("bold", h.bold);
                            h.glow     = hj.value("glow", h.glow);
                            h.glowRadius = hj.value("glowRadius", h.glowRadius);
                            if (hj.contains("color") && hj["color"].is_array() && hj["color"].size() >= 4)
                                for (int i = 0; i < 4; ++i) h.color[i] = hj["color"][i].get<float>();
                            if (hj.contains("glowColor") && hj["glowColor"].is_array() && hj["glowColor"].size() >= 4)
                                for (int i = 0; i < 4; ++i) h.glowColor[i] = hj["glowColor"][i].get<float>();
                        };
                        loadHud(gm, "scoreHud", song.gameMode.scoreHud);
                        loadHud(gm, "comboHud", song.gameMode.comboHud);

                        // Camera
                        if (gm.contains("cameraEye") && gm["cameraEye"].is_array() && gm["cameraEye"].size() >= 3)
                            for (int i = 0; i < 3; ++i) song.gameMode.cameraEye[i] = gm["cameraEye"][i].get<float>();
                        if (gm.contains("cameraTarget") && gm["cameraTarget"].is_array() && gm["cameraTarget"].size() >= 3)
                            for (int i = 0; i < 3; ++i) song.gameMode.cameraTarget[i] = gm["cameraTarget"][i].get<float>();
                        song.gameMode.cameraFov = gm.value("cameraFov", 55.f);

                        // Background
                        song.gameMode.backgroundImage = gm.value("backgroundImage", "");

                        // 3D sky height
                        song.gameMode.skyHeight = gm.value("skyHeight", 1.f);

                        // Circle-mode disk defaults
                        song.gameMode.diskInnerRadius  = gm.value("diskInnerRadius",  0.9f);
                        song.gameMode.diskBaseRadius   = gm.value("diskBaseRadius",   2.4f);
                        song.gameMode.diskRingSpacing  = gm.value("diskRingSpacing",  0.6f);
                        song.gameMode.diskInitialScale = gm.value("diskInitialScale", 1.0f);
                    }
                    set.songs.push_back(std::move(song));
                }
            }
            m_sets.push_back(std::move(set));
        }
    }

    if (!m_sets.empty()) {
        m_selectedSet = 0;
        m_setScrollTarget = 0.f;
    }

    m_loaded = true;
}

void MusicSelectionEditor::save() {
    if (m_projectPath.empty()) return;

    json j;
    json setsArr = json::array();
    for (auto& set : m_sets) {
        json sj;
        sj["name"]       = set.name;
        sj["coverImage"] = set.coverImage;
        json songsArr = json::array();
        for (auto& song : set.songs) {
            json songJ;
            songJ["name"]        = song.name;
            songJ["artist"]      = song.artist;
            songJ["coverImage"]  = song.coverImage;
            songJ["audioFile"]   = song.audioFile;
            songJ["chartEasy"]   = song.chartEasy;
            songJ["chartMedium"] = song.chartMedium;
            songJ["chartHard"]   = song.chartHard;
            songJ["score"]       = song.score;
            songJ["achievement"] = song.achievement;
            songJ["scoreEasy"]         = song.scoreEasy;
            songJ["scoreMedium"]       = song.scoreMedium;
            songJ["scoreHard"]         = song.scoreHard;
            songJ["achievementEasy"]   = song.achievementEasy;
            songJ["achievementMedium"] = song.achievementMedium;
            songJ["achievementHard"]   = song.achievementHard;
            songJ["previewStart"]      = song.previewStart;
            songJ["previewDuration"]   = song.previewDuration;
            // per-song game mode config
            json gmJ;
            switch (song.gameMode.type) {
                case GameModeType::DropNotes: gmJ["type"] = "dropNotes"; break;
                case GameModeType::Circle:    gmJ["type"] = "circle";    break;
                case GameModeType::ScanLine:  gmJ["type"] = "scanLine";  break;
            }
            gmJ["trackCount"] = song.gameMode.trackCount;
            if (song.gameMode.type == GameModeType::DropNotes)
                gmJ["dimension"] = (song.gameMode.dimension == DropDimension::TwoD) ? "2D" : "3D";
            gmJ["audioOffset"]  = song.gameMode.audioOffset;
            gmJ["perfectMs"]    = song.gameMode.perfectMs;
            gmJ["goodMs"]       = song.gameMode.goodMs;
            gmJ["badMs"]        = song.gameMode.badMs;
            gmJ["perfectScore"] = song.gameMode.perfectScore;
            gmJ["goodScore"]    = song.gameMode.goodScore;
            gmJ["badScore"]     = song.gameMode.badScore;
            gmJ["fcImage"]      = song.gameMode.fcImage;
            gmJ["apImage"]      = song.gameMode.apImage;

            // HUD text configs
            auto saveHud = [](json& parent, const char* key, const HudTextConfig& h) {
                json hj;
                hj["pos"]       = {h.pos[0], h.pos[1]};
                hj["fontSize"]  = h.fontSize;
                hj["scale"]     = h.scale;
                hj["bold"]      = h.bold;
                hj["color"]     = {h.color[0], h.color[1], h.color[2], h.color[3]};
                hj["glow"]      = h.glow;
                hj["glowColor"] = {h.glowColor[0], h.glowColor[1], h.glowColor[2], h.glowColor[3]};
                hj["glowRadius"]= h.glowRadius;
                parent[key] = hj;
            };
            saveHud(gmJ, "scoreHud", song.gameMode.scoreHud);
            saveHud(gmJ, "comboHud", song.gameMode.comboHud);

            // Camera
            gmJ["cameraEye"]    = {song.gameMode.cameraEye[0], song.gameMode.cameraEye[1], song.gameMode.cameraEye[2]};
            gmJ["cameraTarget"] = {song.gameMode.cameraTarget[0], song.gameMode.cameraTarget[1], song.gameMode.cameraTarget[2]};
            gmJ["cameraFov"]    = song.gameMode.cameraFov;

            // Background
            gmJ["backgroundImage"] = song.gameMode.backgroundImage;

            // 3D sky height
            gmJ["skyHeight"] = song.gameMode.skyHeight;

            // Circle-mode disk defaults
            gmJ["diskInnerRadius"]  = song.gameMode.diskInnerRadius;
            gmJ["diskBaseRadius"]   = song.gameMode.diskBaseRadius;
            gmJ["diskRingSpacing"]  = song.gameMode.diskRingSpacing;
            gmJ["diskInitialScale"] = song.gameMode.diskInitialScale;

            songJ["gameMode"] = gmJ;
            songsArr.push_back(songJ);
        }
        sj["songs"] = songsArr;
        setsArr.push_back(sj);
    }
    j["sets"]       = setsArr;
    j["background"] = m_pageBackground;
    j["fcImage"]    = m_fcImage;
    j["apImage"]    = m_apImage;

    std::ofstream out(m_projectPath + "/music_selection.json");
    if (out.is_open()) out << j.dump(2);
}

// ── Audio preview ────────────────────────────────────────────────────────────
//
// Plays a ~30 s clip from `previewStart` of the currently-selected song
// after the selection "dwells" for a short moment (~500 ms). Changing the
// selection restarts the dwell; after `previewDuration` seconds we stop.

void MusicSelectionEditor::updateAudioPreview(float dt) {
    if (!m_engine) return;
    // Preview audio is only meant for the real game and the editor's
    // full-screen test-game mode — not the small editor preview box.
    if (!m_engine->isTestMode()) {
        if (m_previewPlaying) {
            m_engine->audio().stop();
            m_previewPlaying = false;
        }
        m_previewDwellT = 0.f;
        m_previewStopT  = 0.f;
        return;
    }
    AudioEngine& ae = m_engine->audio();

    // Did the selection change since the last frame? If yes, reset dwell.
    if (m_selectedSet != m_previewSetIdx ||
        m_selectedSong != m_previewSongIdx) {
        if (m_previewPlaying) {
            ae.stop();
            m_previewPlaying = false;
        }
        m_previewSetIdx  = m_selectedSet;
        m_previewSongIdx = m_selectedSong;
        m_previewDwellT  = 0.f;
        m_previewStopT   = 0.f;
        return;
    }

    if (m_selectedSet < 0 || m_selectedSet >= (int)m_sets.size()) return;
    auto& set = m_sets[m_selectedSet];
    if (m_selectedSong < 0 || m_selectedSong >= (int)set.songs.size()) return;
    const auto& song = set.songs[m_selectedSong];
    if (song.audioFile.empty()) return;

    if (!m_previewPlaying) {
        // Dwell before loading audio so fast scrolling doesn't churn the
        // decoder on every song the user flies past.
        m_previewDwellT += dt;
        if (m_previewDwellT < 0.5f) return;

        std::string full = m_projectPath + "/" + song.audioFile;
        if (m_previewPath != full) {
            if (!ae.load(full)) return;
            m_previewPath = full;
        }
        float start = song.previewStart;
        if (start < 0.f) {
            // Fallback: 25% of duration so we land somewhere past the intro.
            double dur = ae.durationSeconds();
            start = (float)(dur * 0.25);
        }
        ae.playFrom((double)start);
        m_previewPlaying = true;
        m_previewStopT   = song.previewDuration;
    } else {
        m_previewStopT -= dt;
        if (m_previewStopT <= 0.f) {
            ae.stop();
            m_previewPlaying = false;
        }
    }
}

// ── Accessor ─────────────────────────────────────────────────────────────────

SongInfo* MusicSelectionEditor::getSelectedSong() {
    if (m_selectedSet < 0 || m_selectedSet >= (int)m_sets.size()) return nullptr;
    auto& set = m_sets[m_selectedSet];
    if (m_selectedSong < 0 || m_selectedSong >= (int)set.songs.size()) return nullptr;
    return &set.songs[m_selectedSong];
}

// ── Main render ──────────────────────────────────────────────────────────────

void MusicSelectionEditor::render(Engine* engine) {
    m_engine = engine;
    if (m_statusTimer > 0.f) m_statusTimer -= ImGui::GetIO().DeltaTime;

    // Smooth scroll animation
    float dt = ImGui::GetIO().DeltaTime;
    float lerpSpeed = 8.f;
    m_setScrollCurrent  += (m_setScrollTarget  - m_setScrollCurrent)  * std::min(1.f, lerpSpeed * dt);
    m_songScrollCurrent += (m_songScrollTarget - m_songScrollCurrent) * std::min(1.f, lerpSpeed * dt);

    // Default selection: on first entry after load, pick the first song of
    // the first set so the scene shows something by default.
    if (m_selectedSet < 0 && !m_sets.empty()) {
        m_selectedSet     = 0;
        m_setScrollTarget = 0.f;
        if (!m_sets[0].songs.empty()) {
            m_selectedSong     = 0;
            m_songScrollTarget = 0.f;
        }
    }

    // Audio preview for the dwelling selection. After a short dwell we load
    // the song's audio and start playing from its previewStart. Changing
    // selection resets the timer; after previewDuration we stop playback.
    updateAudioPreview(dt);

    // ── Test mode: full-screen music selection ─────────────────────────────────
    if (engine && engine->isTestMode()) {
        ImVec2 displaySz = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(displaySz);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##test_musicsel", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        renderGamePreview(origin, displaySz);

        // Fade-in from black (reverse of transition progress, briefly at start)
        // Reset fade on re-entry (detect layer switch)
        static EditorLayer lastLayer = EditorLayer::ProjectHub;
        static float fadeIn = 1.f;
        if (lastLayer != EditorLayer::MusicSelection) fadeIn = 1.f;
        lastLayer = EditorLayer::MusicSelection;
        fadeIn -= ImGui::GetIO().DeltaTime * 2.f;
        if (fadeIn > 0.f) {
            int alpha = (int)(255 * fadeIn);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(0, 0), displaySz, IM_COL32(0, 0, 0, alpha));
        }

        ImGui::End();
        ImGui::PopStyleVar();

        // ── Settings page overlay — drawn FIRST so the gear button below is
        // always on top of it, giving the player a single consistent point
        // of interaction.
        if (m_showSettings) {
            PlayerSettings& settings =
                m_engine ? m_engine->playerSettings() : m_previewSettings;
            SettingsPageUI::Host host;
            host.audio  = m_engine ? &m_engine->audio() : nullptr;
            host.onSave = [this]() { if (m_engine) m_engine->applyPlayerSettings(); };
            host.onBack = [this]() {
                if (m_engine) m_engine->applyPlayerSettings();
                m_showSettings = false;
            };
            SettingsPageUI::render(ImVec2(0, 0), displaySz,
                                   settings, host, /*readOnly=*/false);
            // Live-apply every frame while the modal is open so slider drags
            // update the active renderer / audio engine immediately.
            if (m_engine) m_engine->applyPlayerSettings();
        }

        // ── Gear/Settings button — independent top-level window submitted
        // LAST so its default z-order is above both the music-select test
        // window and (when present) the settings modal scrim. ─────────────
        {
            const float gearW = 160.f, gearH = 44.f, gearPad = 16.f;
            ImGui::SetNextWindowPos(ImVec2(displaySz.x - gearW - gearPad, gearPad),
                                    ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(gearW, gearH), ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::Begin("##settings_gear_btn", nullptr,
                ImGuiWindowFlags_NoDecoration  | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
            if (ImGui::Button("\xE2\x9A\x99  Settings",
                              ImVec2(gearW - 4.f, gearH - 4.f))) {
                m_showSettings = true;
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        return;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Music Selection", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    // Scan assets once per project
    if (!m_assetsScanned && !m_projectPath.empty()) {
        m_assets        = scanAssets(m_projectPath);
        m_assetsScanned = true;
    }

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float splitterThick = 4.f;
    const float navH = 36.f;
    float totalH   = contentSize.y - navH - 8.f;
    float topH     = totalH * m_vSplit - splitterThick * 0.5f;
    float assetsH  = totalH * (1.f - m_vSplit) - splitterThick * 0.5f;
    float previewW = contentSize.x * m_hSplit - splitterThick * 0.5f;
    float hierW    = contentSize.x * (1.f - m_hSplit) - splitterThick * 0.5f;

    // ── Top row: Preview | vsplitter | Hierarchy ─────────────────────────────
    ImGui::BeginChild("MSPreview", ImVec2(previewW, topH), true);
    renderPreview(previewW, topH);
    ImGui::EndChild();

    ImGui::SameLine();

    // Vertical splitter
    ImGui::InvisibleButton("ms_vsplit", ImVec2(splitterThick, topH));
    if (ImGui::IsItemActive()) {
        m_hSplit += ImGui::GetIO().MouseDelta.x / contentSize.x;
        m_hSplit = std::clamp(m_hSplit, 0.4f, 0.85f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImGui::SameLine();

    ImGui::BeginChild("MSHierarchy", ImVec2(hierW, topH), true);
    renderHierarchy(hierW, topH);
    ImGui::EndChild();

    // Horizontal splitter
    ImGui::InvisibleButton("ms_hsplit", ImVec2(contentSize.x, splitterThick));
    if (ImGui::IsItemActive()) {
        m_vSplit += ImGui::GetIO().MouseDelta.y / totalH;
        m_vSplit = std::clamp(m_vSplit, 0.3f, 0.9f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    // ── Bottom: Assets panel ─────────────────────────────────────────────────
    ImGui::BeginChild("MSAssets", ImVec2(contentSize.x, assetsH), true);
    renderAssets();
    ImGui::EndChild();

    // ── Nav bar ──────────────────────────────────────────────────────────────
    if (ImGui::Button("< Back")) {
        if (engine) engine->switchLayer(EditorLayer::StartScreen);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        save();
        m_statusMsg   = "Saved!";
        m_statusTimer = 2.f;
    }
    if (m_statusTimer > 0.f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "%s", m_statusMsg.c_str());
    }

    // Bottom-right: switch to the dedicated Settings editor layer.
    ImGui::SameLine();
    ImGui::SetCursorPosX(contentSize.x - 200.f);
    if (ImGui::Button("Next: Settings >")) {
        if (engine) engine->switchLayer(EditorLayer::Settings);
    }

    ImGui::End();

}

// ── renderPreview ────────────────────────────────────────────────────────────

void MusicSelectionEditor::renderPreview(float width, float height) {

    // Aspect-ratio controls so authors preview the song wheel at the final
    // device's shape. Shares state with the Start Screen editor.
    if (m_engine)
        previewAspect::renderControls(m_engine->previewAspect());
    ImGui::Spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    previewAspect::FitResult fit = m_engine
        ? previewAspect::fitAndLetterbox(m_engine->previewAspect(), avail)
        : previewAspect::FitResult{ImGui::GetCursorScreenPos(), avail};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = fit.origin;
    float pw = fit.size.x, ph = fit.size.y;

    // Clip wheel/cover/button draws to the letterbox rect so nothing leaks
    // into the dark bars when the author picks a narrow aspect.
    dl->PushClipRect(p, ImVec2(p.x + pw, p.y + ph), true);

    // ── Background ──────────────────────────────────────────────────────────
    VkDescriptorSet pageBg = m_pageBackground.empty()
        ? VK_NULL_HANDLE : getThumb(m_pageBackground);
    if (pageBg) {
        // Draw the user's image filling the scene.
        dl->AddImage((ImTextureID)(uint64_t)pageBg, p,
                     ImVec2(p.x + pw, p.y + ph));

        // Frosted-glass overlay — approximated by layering semi-transparent
        // dark panels whose opacity varies horizontally. We split the width
        // into three horizontal bands (left wheel, center stack, right wheel)
        // and draw each with its own alpha so the middle feels less frosted
        // than the sides. Gradient edges smooth the transitions.
        const float wheelW = pw * 0.18f;
        const ImU32 frostHeavy  = IM_COL32(20, 22, 30, 190); // sides
        const ImU32 frostLight  = IM_COL32(20, 22, 30,  55); // middle
        const float fadeW       = 18.f;                      // narrow transition

        // Left: solid heavy band
        dl->AddRectFilled(p, ImVec2(p.x + wheelW, p.y + ph), frostHeavy);
        // Transition left → middle
        dl->AddRectFilledMultiColor(
            ImVec2(p.x + wheelW,         p.y),
            ImVec2(p.x + wheelW + fadeW, p.y + ph),
            frostHeavy, frostLight, frostLight, frostHeavy);
        // Middle: light
        dl->AddRectFilled(ImVec2(p.x + wheelW + fadeW,      p.y),
                          ImVec2(p.x + pw - wheelW - fadeW, p.y + ph),
                          frostLight);
        // Transition middle → right
        dl->AddRectFilledMultiColor(
            ImVec2(p.x + pw - wheelW - fadeW, p.y),
            ImVec2(p.x + pw - wheelW,         p.y + ph),
            frostLight, frostHeavy, frostHeavy, frostLight);
        // Right: solid heavy band
        dl->AddRectFilled(ImVec2(p.x + pw - wheelW, p.y),
                          ImVec2(p.x + pw,          p.y + ph),
                          frostHeavy);

        // Soft dark shadow on the middle-facing side of each panel gives
        // the boundary depth without drawing a hard bright line.
        const ImU32 edgeLo = IM_COL32(0, 0, 0, 140);
        float lx = p.x + wheelW;
        float rx = p.x + pw - wheelW;
        dl->AddLine(ImVec2(lx, p.y), ImVec2(lx, p.y + ph), edgeLo, 2.f);
        dl->AddLine(ImVec2(rx, p.y), ImVec2(rx, p.y + ph), edgeLo, 2.f);

        // Subtle top/bottom vignette so text pops against busy backgrounds.
        const ImU32 tVignette = IM_COL32(0, 0, 0,  90);
        const ImU32 bVignette = IM_COL32(0, 0, 0,  50);
        dl->AddRectFilledMultiColor(p, ImVec2(p.x + pw, p.y + ph * 0.18f),
                                    tVignette, tVignette, 0, 0);
        dl->AddRectFilledMultiColor(
            ImVec2(p.x,      p.y + ph * 0.82f),
            ImVec2(p.x + pw, p.y + ph),
            0, 0, bVignette, bVignette);
    } else {
        // No background set — fall back to the flat dark fill.
        dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(20, 22, 30, 255));
    }

    // Layout proportions
    float wheelW     = pw * 0.18f;   // each wheel column
    float centerW    = pw - wheelW * 2.f;
    float coverSize  = std::min(centerW * 0.7f, ph * 0.55f);

    // ── Left wheel: Music Sets ───────────────────────────────────────────────
    renderSetWheel(ImVec2(p.x, p.y), wheelW, ph);

    // ── Right wheel: Songs ───────────────────────────────────────────────────
    renderSongWheel(ImVec2(p.x + pw - wheelW, p.y), wheelW, ph);

    // ── Center: Cover + difficulty + play, centered vertically ──────────────
    const float diffGap   = std::max(ph * 0.04f, 28.f);
    const float playGap   = 50.f;
    const float diffRowH  = 48.f;   // matches renderDifficultyButtons
    const float playRowH  = 60.f;   // matches renderPlayButton
    float stackH  = coverSize + diffGap + diffRowH + playGap + playRowH;
    float centerX = p.x + wheelW + centerW * 0.5f;
    float stackTop = p.y + (ph - stackH) * 0.5f;
    float coverY   = stackTop;
    renderCoverPhoto(ImVec2(centerX, coverY), coverSize);

    float diffY = coverY + coverSize + diffGap;
    renderDifficultyButtons(ImVec2(centerX, diffY), centerW);

    float playY = diffY + playGap;
    renderPlayButton(ImVec2(centerX, playY), centerW);

    // Preview toggle is handled inside renderSongWheel — when on, every
    // song card renders both rhombus slots as "unlocked" so the author can
    // judge how the badge images fill the real slots.

    dl->PopClipRect();
}

// ── renderGamePreview ────────────────────────────────────────────────────────

void MusicSelectionEditor::renderGamePreview(ImVec2 p, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pw = size.x, ph = size.y;

    // Page background + frosted overlay (see renderPreview for the layering
    // rationale — middle stays readable, sides feel heavier).
    VkDescriptorSet pageBg = m_pageBackground.empty()
        ? VK_NULL_HANDLE : getThumb(m_pageBackground);
    if (pageBg) {
        dl->AddImage((ImTextureID)(uint64_t)pageBg, p,
                     ImVec2(p.x + pw, p.y + ph));
        const float wheelWBand = pw * 0.18f;
        const ImU32 frostHeavy = IM_COL32(20, 22, 30, 190);
        const ImU32 frostLight = IM_COL32(20, 22, 30,  55);
        const float fadeW      = 18.f;
        dl->AddRectFilled(p, ImVec2(p.x + wheelWBand, p.y + ph), frostHeavy);
        dl->AddRectFilledMultiColor(
            ImVec2(p.x + wheelWBand,         p.y),
            ImVec2(p.x + wheelWBand + fadeW, p.y + ph),
            frostHeavy, frostLight, frostLight, frostHeavy);
        dl->AddRectFilled(ImVec2(p.x + wheelWBand + fadeW,      p.y),
                          ImVec2(p.x + pw - wheelWBand - fadeW, p.y + ph),
                          frostLight);
        dl->AddRectFilledMultiColor(
            ImVec2(p.x + pw - wheelWBand - fadeW, p.y),
            ImVec2(p.x + pw - wheelWBand,         p.y + ph),
            frostLight, frostHeavy, frostHeavy, frostLight);
        dl->AddRectFilled(ImVec2(p.x + pw - wheelWBand, p.y),
                          ImVec2(p.x + pw,               p.y + ph),
                          frostHeavy);

        const ImU32 edgeLo = IM_COL32(0, 0, 0, 140);
        float lx = p.x + wheelWBand;
        float rx = p.x + pw - wheelWBand;
        dl->AddLine(ImVec2(lx, p.y), ImVec2(lx, p.y + ph), edgeLo, 2.f);
        dl->AddLine(ImVec2(rx, p.y), ImVec2(rx, p.y + ph), edgeLo, 2.f);
    } else {
        dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(20, 22, 30, 255));
    }

    float wheelW    = pw * 0.18f;
    float centerW   = pw - wheelW * 2.f;
    float coverSize = std::min(centerW * 0.7f, ph * 0.50f);

    renderSetWheel(ImVec2(p.x, p.y), wheelW, ph);
    renderSongWheel(ImVec2(p.x + pw - wheelW, p.y), wheelW, ph);

    float centerX = p.x + wheelW + centerW * 0.5f;
    float coverY  = p.y + ph * 0.08f;
    renderCoverPhoto(ImVec2(centerX, coverY), coverSize);

    float diffY = coverY + coverSize + ph * 0.04f;
    renderDifficultyButtons(ImVec2(centerX, diffY), centerW);

    float playY = diffY + 50.f;
    renderPlayButton(ImVec2(centerX, playY), centerW);

    // Gear/Settings button + modal overlay are rendered by the caller (see
    // MusicSelectionEditor::render and GameFlowPreview) AFTER this window
    // closes so ImGui can treat them as independent top-level windows. That
    // guarantees the button hit-test isn't blocked by the wheel cards'
    // InvisibleButtons inside this window.
}

// ── Set wheel (left) ─────────────────────────────────────────────────────────

void MusicSelectionEditor::renderSetWheel(ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Panel background
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      IM_COL32(15, 15, 25, 200));

    if (m_sets.empty()) {
        ImVec2 textSz = ImGui::CalcTextSize("No Sets");
        dl->AddText(ImVec2(origin.x + width * 0.5f - textSz.x * 0.5f,
                           origin.y + height * 0.5f - textSz.y * 0.5f),
                    IM_COL32(120, 120, 140, 200), "No Sets");
        return;
    }

    int count = (int)m_sets.size();
    float centerX = origin.x + width * 0.5f;
    float centerY = origin.y + height * 0.5f;
    float cardW = width * 0.82f;
    float cardH = 80.f;
    float cardHalfW = cardW * 0.5f;
    float cardHalfH = cardH * 0.5f;

    // Mouse wheel scroll
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    if (mousePos.x >= origin.x && mousePos.x <= origin.x + width &&
        mousePos.y >= origin.y && mousePos.y <= origin.y + height) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) {
            m_setScrollTarget -= wheel;
            m_setScrollTarget = std::clamp(m_setScrollTarget, 0.f, (float)(count - 1));
            m_selectedSet = (int)std::round(m_setScrollTarget);
            m_selectedSong = -1;
            m_songScrollTarget = 0.f;
        }
    }

    // Collect visible cards, sort by distance (farthest first = painter's order)
    int maxVisible = 5;
    struct CardToDraw { int index; float offset; };
    std::vector<CardToDraw> cards;
    for (int i = 0; i < count; ++i) {
        float offset = (float)i - m_setScrollCurrent;
        if (std::abs(offset) <= (float)maxVisible * 0.5f + 0.5f)
            cards.push_back({i, offset});
    }
    std::sort(cards.begin(), cards.end(), [](const CardToDraw& a, const CardToDraw& b) {
        return std::abs(a.offset) > std::abs(b.offset);
    });

    for (auto& card : cards) {
        float t = card.offset;
        float absT = std::abs(t);

        // Progressive transform
        float scaleFactor = std::max(0.55f, 1.f - absT * 0.12f);
        float alphaFactor = std::max(0.15f, 1.f - absT * 0.25f);
        float yShift = t * cardH * 0.55f;
        float skew = std::min(0.30f, absT * 0.08f);

        float sw = cardHalfW * scaleFactor;
        float sh = cardHalfH * scaleFactor;
        float cx = centerX;
        float cy = centerY + yShift;

        // Quad corners with perspective skew
        float nearHalfW = sw;
        float farHalfW  = sw * (1.f - skew);

        ImVec2 tl, tr, br, bl;
        if (t >= 0.f) {
            // Below center: top edge narrower
            tl = ImVec2(cx - farHalfW,  cy - sh);
            tr = ImVec2(cx + farHalfW,  cy - sh);
            br = ImVec2(cx + nearHalfW, cy + sh);
            bl = ImVec2(cx - nearHalfW, cy + sh);
        } else {
            // Above center: bottom edge narrower
            tl = ImVec2(cx - nearHalfW, cy - sh);
            tr = ImVec2(cx + nearHalfW, cy - sh);
            br = ImVec2(cx + farHalfW,  cy + sh);
            bl = ImVec2(cx - farHalfW,  cy + sh);
        }

        int alpha = (int)(220 * alphaFactor);
        bool isSelected = (card.index == m_selectedSet);

        // Card background
        ImU32 bgCol = isSelected
            ? IM_COL32(60, 80, 160, alpha)
            : IM_COL32(40, 42, 55, alpha);
        dl->AddQuadFilled(tl, tr, br, bl, bgCol);

        if (isSelected) {
            dl->AddQuad(tl, tr, br, bl, IM_COL32(120, 160, 255, (int)(255 * alphaFactor)), 2.f);
        }

        // Cover thumbnail (left 25% of card)
        float thumbFrac = 0.25f;
        VkDescriptorSet coverDesc = getCoverDesc(m_sets[card.index].coverImage);
        ImVec2 ttl = tl;
        ImVec2 ttr(tl.x + (tr.x - tl.x) * thumbFrac, tl.y + (tr.y - tl.y) * thumbFrac);
        ImVec2 tbr(bl.x + (br.x - bl.x) * thumbFrac, bl.y + (br.y - bl.y) * thumbFrac);
        ImVec2 tbl = bl;

        float inset = 4.f;
        ImVec2 itl(ttl.x + inset, ttl.y + inset);
        ImVec2 itr(ttr.x,         ttr.y + inset);
        ImVec2 ibr(tbr.x,         tbr.y - inset);
        ImVec2 ibl(tbl.x + inset, tbl.y - inset);

        if (coverDesc) {
            dl->AddImageQuad((ImTextureID)(uint64_t)coverDesc,
                             itl, itr, ibr, ibl,
                             ImVec2(0,0), ImVec2(1,0), ImVec2(1,1), ImVec2(0,1),
                             IM_COL32(255, 255, 255, (int)(255 * alphaFactor)));
        } else {
            dl->AddQuadFilled(itl, itr, ibr, ibl,
                              IM_COL32(60, 60, 80, (int)(200 * alphaFactor)));
        }

        // Set name text (centered in right portion of card)
        float quadCX = (tl.x + tr.x + br.x + bl.x) * 0.25f;
        float quadCY = (tl.y + tr.y + br.y + bl.y) * 0.25f;
        float textOffsetX = sw * thumbFrac * 0.5f;

        const char* setName = m_sets[card.index].name.c_str();
        ImVec2 textSz = ImGui::CalcTextSize(setName);
        dl->AddText(ImVec2(quadCX + textOffsetX - textSz.x * 0.5f,
                           quadCY - textSz.y * 0.5f),
                    IM_COL32(230, 230, 240, (int)(255 * alphaFactor)),
                    setName);

        // Click target (bounding box)
        float minX = std::min({tl.x, tr.x, br.x, bl.x});
        float minY = std::min({tl.y, tr.y, br.y, bl.y});
        float maxX = std::max({tl.x, tr.x, br.x, bl.x});
        float maxY = std::max({tl.y, tr.y, br.y, bl.y});

        ImGui::SetCursorScreenPos(ImVec2(minX, minY));
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##setcard_%d", card.index);
        if (ImGui::InvisibleButton(btnId, ImVec2(maxX - minX, maxY - minY))) {
            m_selectedSet = card.index;
            m_setScrollTarget = (float)card.index;
            m_selectedSong = -1;
            m_songScrollTarget = 0.f;
        }
    }
}

// ── Song wheel (right) ──────────────────────────────────────────────────────

void MusicSelectionEditor::renderSongWheel(ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      IM_COL32(15, 15, 25, 200));

    if (m_selectedSet < 0 || m_selectedSet >= (int)m_sets.size()) {
        ImVec2 textSz = ImGui::CalcTextSize("Select a Set");
        dl->AddText(ImVec2(origin.x + width * 0.5f - textSz.x * 0.5f,
                           origin.y + height * 0.5f - textSz.y * 0.5f),
                    IM_COL32(120, 120, 140, 200), "Select a Set");
        return;
    }

    auto& songs = m_sets[m_selectedSet].songs;
    if (songs.empty()) {
        ImVec2 textSz = ImGui::CalcTextSize("No Songs");
        dl->AddText(ImVec2(origin.x + width * 0.5f - textSz.x * 0.5f,
                           origin.y + height * 0.5f - textSz.y * 0.5f),
                    IM_COL32(120, 120, 140, 200), "No Songs");
        return;
    }

    int count = (int)songs.size();
    float centerX = origin.x + width * 0.5f;
    float centerY = origin.y + height * 0.5f;
    float cardW = width * 0.82f;
    float cardH = 80.f;
    float cardHalfW = cardW * 0.5f;
    float cardHalfH = cardH * 0.5f;

    // Mouse wheel scroll
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    if (mousePos.x >= origin.x && mousePos.x <= origin.x + width &&
        mousePos.y >= origin.y && mousePos.y <= origin.y + height) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f) {
            m_songScrollTarget -= wheel;
            m_songScrollTarget = std::clamp(m_songScrollTarget, 0.f, (float)(count - 1));
            m_selectedSong = (int)std::round(m_songScrollTarget);
        }
    }

    // Collect visible cards, sort farthest-first (painter's order)
    int maxVisible = 5;
    struct CardToDraw { int index; float offset; };
    std::vector<CardToDraw> cards;
    for (int i = 0; i < count; ++i) {
        float offset = (float)i - m_songScrollCurrent;
        if (std::abs(offset) <= (float)maxVisible * 0.5f + 0.5f)
            cards.push_back({i, offset});
    }
    std::sort(cards.begin(), cards.end(), [](const CardToDraw& a, const CardToDraw& b) {
        return std::abs(a.offset) > std::abs(b.offset);
    });

    for (auto& card : cards) {
        float t = card.offset;
        float absT = std::abs(t);

        float scaleFactor = std::max(0.55f, 1.f - absT * 0.12f);
        float alphaFactor = std::max(0.15f, 1.f - absT * 0.25f);
        float yShift = t * cardH * 0.55f;
        float skew = std::min(0.30f, absT * 0.08f);

        float sw = cardHalfW * scaleFactor;
        float sh = cardHalfH * scaleFactor;
        float cx = centerX;
        float cy = centerY + yShift;

        float nearHalfW = sw;
        float farHalfW  = sw * (1.f - skew);

        ImVec2 tl, tr, br, bl;
        if (t >= 0.f) {
            tl = ImVec2(cx - farHalfW,  cy - sh);
            tr = ImVec2(cx + farHalfW,  cy - sh);
            br = ImVec2(cx + nearHalfW, cy + sh);
            bl = ImVec2(cx - nearHalfW, cy + sh);
        } else {
            tl = ImVec2(cx - nearHalfW, cy - sh);
            tr = ImVec2(cx + nearHalfW, cy - sh);
            br = ImVec2(cx + farHalfW,  cy + sh);
            bl = ImVec2(cx - farHalfW,  cy + sh);
        }

        int alpha = (int)(220 * alphaFactor);
        bool isSelected = (card.index == m_selectedSong);
        auto& song = songs[card.index];

        // Card background
        ImU32 bgCol = isSelected
            ? IM_COL32(160, 60, 100, alpha)
            : IM_COL32(40, 42, 55, alpha);
        dl->AddQuadFilled(tl, tr, br, bl, bgCol);

        if (isSelected) {
            dl->AddQuad(tl, tr, br, bl, IM_COL32(255, 120, 180, (int)(255 * alphaFactor)), 2.f);
        }

        // Cover thumbnail (left 25%)
        float thumbFrac = 0.25f;
        VkDescriptorSet coverDesc = getCoverDesc(song.coverImage);
        ImVec2 ttl = tl;
        ImVec2 ttr(tl.x + (tr.x - tl.x) * thumbFrac, tl.y + (tr.y - tl.y) * thumbFrac);
        ImVec2 tbr(bl.x + (br.x - bl.x) * thumbFrac, bl.y + (br.y - bl.y) * thumbFrac);
        ImVec2 tbl = bl;

        float inset = 4.f;
        ImVec2 itl(ttl.x + inset, ttl.y + inset);
        ImVec2 itr(ttr.x,         ttr.y + inset);
        ImVec2 ibr(tbr.x,         tbr.y - inset);
        ImVec2 ibl(tbl.x + inset, tbl.y - inset);

        if (coverDesc) {
            dl->AddImageQuad((ImTextureID)(uint64_t)coverDesc,
                             itl, itr, ibr, ibl,
                             ImVec2(0,0), ImVec2(1,0), ImVec2(1,1), ImVec2(0,1),
                             IM_COL32(255, 255, 255, (int)(255 * alphaFactor)));
        } else {
            dl->AddQuadFilled(itl, itr, ibr, ibl,
                              IM_COL32(80, 40, 60, (int)(200 * alphaFactor)));
        }

        // Clip all card interior to the card's bounding rect so long song
        // names, oversized badges, etc. can never paint past the edges.
        ImVec2 cardMin(std::min({tl.x, tr.x, br.x, bl.x}),
                        std::min({tl.y, tr.y, br.y, bl.y}));
        ImVec2 cardMax(std::max({tl.x, tr.x, br.x, bl.x}),
                        std::max({tl.y, tr.y, br.y, bl.y}));
        dl->PushClipRect(cardMin, cardMax, true);

        // Card interior layout:
        //   [cover  0..thumbFrac]  [text column]  [two rhombus badges]
        // Rhombus area is sized first (so it always fits); whatever width
        // is left after the cover + rhombus pair becomes the text column.
        float quadCX = (tl.x + tr.x + br.x + bl.x) * 0.25f;
        float quadCY = (tl.y + tr.y + br.y + bl.y) * 0.25f;

        float cardW      = sw * 2.f;
        float coverW     = cardW * thumbFrac;
        float padding    = sw * 0.04f;
        // Rhombus sized to leave vertical padding inside the card; the pair
        // overlaps by 25%, so visual width ≈ rhombusH * 1.75.
        float rhombusH   = std::min(sh * 1.60f, cardW * 0.20f);
        float rhombusW   = rhombusH;
        float overlap    = rhombusW * 0.25f;
        float rhombusPairW = rhombusW + (rhombusW - overlap); // ~1.75 * W
        float rhombusAreaW = rhombusPairW + padding;

        // Text column spans what's left between cover and rhombus area.
        float textColLeft  = tl.x + coverW + padding;
        float textColRight = tr.x - rhombusAreaW;
        if (textColRight < textColLeft + 10.f)
            textColRight = textColLeft + 10.f;
        float textBaseX    = (textColLeft + textColRight) * 0.5f;
        float textColW     = textColRight - textColLeft;

        // Per-difficulty score + achievement.
        int diffScore = 0;
        const std::string* diffAch = nullptr;
        switch (m_selectedDifficulty) {
            case Difficulty::Easy:
                diffScore = song.scoreEasy;   diffAch = &song.achievementEasy;   break;
            case Difficulty::Medium:
                diffScore = song.scoreMedium; diffAch = &song.achievementMedium; break;
            case Difficulty::Hard:
                diffScore = song.scoreHard;   diffAch = &song.achievementHard;   break;
        }

        VkDescriptorSet fcTex = m_fcImage.empty()
            ? VK_NULL_HANDLE : getThumb(m_fcImage);
        VkDescriptorSet apTex = m_apImage.empty()
            ? VK_NULL_HANDLE : getThumb(m_apImage);

        bool fcUnlocked = false, apUnlocked = false;
        if (diffAch && !diffAch->empty()) {
            std::string low = *diffAch;
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            if      (low == "ap") { fcUnlocked = true; apUnlocked = true; }
            else if (low == "fc") { fcUnlocked = true; }
        }
        // Preview toggle: force both slots lit so the engine author can
        // see how their uploaded badge images read in the real rhombus.
        if (m_showAchievementPreview) { fcUnlocked = true; apUnlocked = true; }

        // Name + score stacked in the card's text column; two rhombus
        // badge slots sit to the RIGHT of that column.
        const char* songName = song.name.c_str();
        ImVec2 nameSz  = ImGui::CalcTextSize(songName);
        char scoreBuf[32];
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", diffScore);
        ImVec2 scoreSz = ImGui::CalcTextSize(scoreBuf);

        float nameY  = quadCY - sh * 0.32f;
        float scoreY = quadCY + sh * 0.12f;
        // Rhombus pair anchored at the right edge of the interior.
        float rhombusRight = tr.x - padding;
        float apCX = rhombusRight - rhombusW * 0.5f;
        float fcCX = apCX - (rhombusW - overlap);
        float rhombusCY = quadCY;

        auto drawRhombusSlot = [&](float cx, float cy,
                                    VkDescriptorSet tex, bool unlocked,
                                    ImU32 fillCol) {
            float hw = rhombusW * 0.5f;
            float hh = rhombusH * 0.5f;
            ImVec2 pN(cx,      cy - hh);
            ImVec2 pE(cx + hw, cy);
            ImVec2 pS(cx,      cy + hh);
            ImVec2 pW(cx - hw, cy);

            ImU32 back = unlocked
                ? fillCol
                : IM_COL32(45, 48, 60, (int)(180 * alphaFactor));
            dl->AddQuadFilled(pN, pE, pS, pW, back);

            if (tex) {
                int a = unlocked ? (int)(230 * alphaFactor)
                                  : (int)( 60 * alphaFactor);
                dl->AddImageQuad((ImTextureID)(uint64_t)tex, pN, pE, pS, pW,
                                  ImVec2(0.5f, 0),   ImVec2(1, 0.5f),
                                  ImVec2(0.5f, 1),   ImVec2(0, 0.5f),
                                  IM_COL32(255, 255, 255, a));
            }

            ImU32 outline = unlocked
                ? IM_COL32(255, 255, 255, (int)(200 * alphaFactor))
                : IM_COL32(150, 150, 170, (int)(140 * alphaFactor));
            dl->AddQuad(pN, pE, pS, pW, outline, 1.5f);
        };

        drawRhombusSlot(fcCX, rhombusCY, fcTex, fcUnlocked,
                         IM_COL32( 40, 110, 150, (int)(170 * alphaFactor)));
        drawRhombusSlot(apCX, rhombusCY, apTex, apUnlocked,
                         IM_COL32(150, 120,  40, (int)(170 * alphaFactor)));

        // Text drawn on top of the rhombus backdrop.
        dl->AddText(ImVec2(textBaseX - nameSz.x * 0.5f, nameY),
                    IM_COL32(240, 240, 250, (int)(255 * alphaFactor)),
                    songName);
        dl->AddText(ImVec2(textBaseX - scoreSz.x * 0.5f, scoreY),
                    IM_COL32(200, 200, 220, (int)(220 * alphaFactor)),
                    scoreBuf);

        dl->PopClipRect();

        // Click target
        float minX = std::min({tl.x, tr.x, br.x, bl.x});
        float minY = std::min({tl.y, tr.y, br.y, bl.y});
        float maxX = std::max({tl.x, tr.x, br.x, bl.x});
        float maxY = std::max({tl.y, tr.y, br.y, bl.y});

        ImGui::SetCursorScreenPos(ImVec2(minX, minY));
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##songcard_%d", card.index);
        if (ImGui::InvisibleButton(btnId, ImVec2(maxX - minX, maxY - minY))) {
            m_selectedSong = card.index;
            m_songScrollTarget = (float)card.index;
        }
        // Double-click to open SongEditor
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_selectedSong = card.index;
            m_songScrollTarget = (float)card.index;
            if (m_engine) {
                SongInfo* s = getSelectedSong();
                if (s) {
                    m_engine->songEditor().setSong(s, m_projectPath);
                    m_engine->switchLayer(EditorLayer::SongEditor);
                }
            }
        }
    }
}

// ── Center cover photo ───────────────────────────────────────────────────────

void MusicSelectionEditor::renderCoverPhoto(ImVec2 origin, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Determine which cover to show: song cover if selected, else set cover
    std::string coverPath;
    std::string label;

    if (m_selectedSong >= 0 && m_selectedSet >= 0 &&
        m_selectedSet < (int)m_sets.size() &&
        m_selectedSong < (int)m_sets[m_selectedSet].songs.size()) {
        auto& song = m_sets[m_selectedSet].songs[m_selectedSong];
        coverPath = song.coverImage;
        label = song.name;
    } else if (m_selectedSet >= 0 && m_selectedSet < (int)m_sets.size()) {
        coverPath = m_sets[m_selectedSet].coverImage;
        label = m_sets[m_selectedSet].name;
    }

    float halfSz = size * 0.5f;
    ImVec2 tl(origin.x - halfSz, origin.y);
    ImVec2 br(origin.x + halfSz, origin.y + size);

    // Shadow
    dl->AddRectFilled(ImVec2(tl.x + 4.f, tl.y + 4.f),
                      ImVec2(br.x + 4.f, br.y + 4.f),
                      IM_COL32(0, 0, 0, 120), 8.f);

    VkDescriptorSet desc = getCoverDesc(coverPath);
    if (desc) {
        dl->AddImageRounded((ImTextureID)(uint64_t)desc, tl, br,
                            ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32(255, 255, 255, 255), 8.f);
    } else {
        // Placeholder
        dl->AddRectFilled(tl, br, IM_COL32(45, 45, 65, 255), 8.f);
        const char* placeholder = coverPath.empty() ? "No Cover" : "Loading...";
        ImVec2 textSz = ImGui::CalcTextSize(placeholder);
        dl->AddText(ImVec2(origin.x - textSz.x * 0.5f, origin.y + size * 0.5f - textSz.y * 0.5f),
                    IM_COL32(150, 150, 170, 200), placeholder);
    }

    // Border
    dl->AddRect(tl, br, IM_COL32(100, 110, 140, 180), 8.f, 0, 1.5f);

    // Label below cover
    if (!label.empty()) {
        ImVec2 labelSz = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(origin.x - labelSz.x * 0.5f, br.y + 6.f),
                    IM_COL32(220, 220, 240, 255), label.c_str());
    }
    // Achievement badge lives on the song wheel card (next to the score),
    // see renderSongWheel.
}

// ── Difficulty buttons ───────────────────────────────────────────────────────

void MusicSelectionEditor::renderDifficultyButtons(ImVec2 origin, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    struct DiffInfo {
        const char* label;
        Difficulty  diff;
        ImU32       color;
        ImU32       activeColor;
    };
    DiffInfo diffs[] = {
        { "EASY",   Difficulty::Easy,   IM_COL32(60, 160, 80, 200),  IM_COL32(80, 220, 100, 255) },
        { "MEDIUM", Difficulty::Medium, IM_COL32(180, 160, 40, 200), IM_COL32(240, 210, 50, 255)  },
        { "HARD",   Difficulty::Hard,   IM_COL32(180, 50, 50, 200),  IM_COL32(240, 60, 60, 255)   },
    };

    float btnW = 90.f;
    float btnH = 32.f;
    float gap  = 16.f;
    float totalW = 3.f * btnW + 2.f * gap;
    float startX = origin.x - totalW * 0.5f;

    for (int i = 0; i < 3; ++i) {
        float bx = startX + (float)i * (btnW + gap);
        float by = origin.y;
        bool active = (m_selectedDifficulty == diffs[i].diff);

        ImU32 col = active ? diffs[i].activeColor : diffs[i].color;
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + btnW, by + btnH), col, 6.f);

        if (active) {
            dl->AddRect(ImVec2(bx, by), ImVec2(bx + btnW, by + btnH),
                        IM_COL32(255, 255, 255, 200), 6.f, 0, 2.f);
        }

        ImVec2 textSz = ImGui::CalcTextSize(diffs[i].label);
        dl->AddText(ImVec2(bx + (btnW - textSz.x) * 0.5f, by + (btnH - textSz.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), diffs[i].label);

        // Clickable
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        char id[32];
        snprintf(id, sizeof(id), "##diff_%d", i);
        if (ImGui::InvisibleButton(id, ImVec2(btnW, btnH))) {
            m_selectedDifficulty = diffs[i].diff;
        }
    }
}

// ── Play button ──────────────────────────────────────────────────────────────

void MusicSelectionEditor::renderPlayButton(ImVec2 origin, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float btnW = 160.f;
    float btnH = 44.f;
    float bx = origin.x - btnW * 0.5f;
    float by = origin.y;

    // Check if we have a valid selection (bounds-checked)
    bool canPlay = (m_selectedSet >= 0 && m_selectedSet < (int)m_sets.size() &&
                    m_selectedSong >= 0 && m_selectedSong < (int)m_sets[m_selectedSet].songs.size());

    ImU32 bgCol   = canPlay ? IM_COL32(50, 120, 220, 240) : IM_COL32(60, 60, 70, 180);
    ImU32 textCol = canPlay ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 120, 130, 200);

    // Hover glow
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool hovered = canPlay &&
        mousePos.x >= bx && mousePos.x <= bx + btnW &&
        mousePos.y >= by && mousePos.y <= by + btnH;

    if (hovered)
        bgCol = IM_COL32(70, 150, 255, 255);

    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + btnW, by + btnH), bgCol, 8.f);
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + btnW, by + btnH),
                IM_COL32(140, 180, 255, canPlay ? 200 : 80), 8.f, 0, 1.5f);

    // "START" text
    const char* playText = "START";
    ImVec2 textSz = ImGui::CalcTextSize(playText);
    dl->AddText(ImVec2(bx + (btnW - textSz.x) * 0.5f, by + (btnH - textSz.y) * 0.5f),
                textCol, playText);

    // Play triangle icon
    float triSize = 10.f;
    float triX = bx + btnW * 0.5f - textSz.x * 0.5f - 20.f;
    float triY = by + btnH * 0.5f;
    dl->AddTriangleFilled(
        ImVec2(triX, triY - triSize),
        ImVec2(triX, triY + triSize),
        ImVec2(triX + triSize * 1.2f, triY),
        textCol);

    // The START button serves two contexts:
    //   1. Editor layout preview — the user is designing how the selection
    //      page looks, so clicking should NOT drop them into a play session.
    //   2. Test-game player flow — the user is actually navigating the
    //      game, so clicking MUST launch the selected song.
    // Engine::isTestMode() distinguishes the two.
    ImGui::SetCursorScreenPos(ImVec2(bx, by));
    if (ImGui::InvisibleButton("##play_btn", ImVec2(btnW, btnH)) && canPlay) {
        if (m_engine && m_engine->isTestMode()) {
            auto& song = m_sets[m_selectedSet].songs[m_selectedSong];
            m_engine->launchGameplay(song, m_selectedDifficulty, m_projectPath, m_autoPlay);
        }
    }

    // ── Auto Play toggle (below the START button) ───────────────────────────
    float abtnW = btnW;
    float abtnH = 28.f;
    float abx = bx;
    float aby = by + btnH + 8.f;

    ImU32 aBg   = m_autoPlay ? IM_COL32(220, 140, 50, 240) : IM_COL32(60, 60, 70, 200);
    ImU32 aText = m_autoPlay ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 200, 210, 230);

    ImVec2 mPos = ImGui::GetIO().MousePos;
    bool aHover = mPos.x >= abx && mPos.x <= abx + abtnW &&
                  mPos.y >= aby && mPos.y <= aby + abtnH;
    if (aHover && !m_autoPlay) aBg = IM_COL32(90, 90, 100, 230);
    if (aHover &&  m_autoPlay) aBg = IM_COL32(240, 160, 70, 255);

    dl->AddRectFilled(ImVec2(abx, aby), ImVec2(abx + abtnW, aby + abtnH), aBg, 6.f);
    dl->AddRect(ImVec2(abx, aby), ImVec2(abx + abtnW, aby + abtnH),
                IM_COL32(180, 180, 200, 180), 6.f, 0, 1.2f);

    const char* aLabel = m_autoPlay ? "AUTO PLAY: ON" : "AUTO PLAY: OFF";
    ImVec2 aSz = ImGui::CalcTextSize(aLabel);
    dl->AddText(ImVec2(abx + (abtnW - aSz.x) * 0.5f, aby + (abtnH - aSz.y) * 0.5f),
                aText, aLabel);

    ImGui::SetCursorScreenPos(ImVec2(abx, aby));
    if (ImGui::InvisibleButton("##autoplay_btn", ImVec2(abtnW, abtnH))) {
        m_autoPlay = !m_autoPlay;
    }
}

// ── Hierarchy panel (right side) ─────────────────────────────────────────────

void MusicSelectionEditor::renderHierarchy(float width, float height) {
    ImGui::Text("Hierarchy");
    ImGui::Separator();

    // ── Page background (drawn behind the whole scene) ──────────────────────
    // Frosted-glass overlay kicks in automatically when a background is set.
    {
        ImGui::TextUnformatted("Page Background:");
        const float zoneW = ImGui::GetContentRegionAvail().x - 74.f;
        const float zoneH = 40.f;
        ImVec2 zonePos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##pagebgzone", ImVec2(zoneW, zoneH));
        ImDrawList* dlBg = ImGui::GetWindowDrawList();
        VkDescriptorSet bgDesc = m_pageBackground.empty()
            ? VK_NULL_HANDLE : getThumb(m_pageBackground);
        ImU32 border = ImGui::IsItemHovered()
            ? IM_COL32(100, 160, 255, 255) : IM_COL32(100, 100, 120, 180);
        if (bgDesc) {
            dlBg->AddImage((ImTextureID)(uint64_t)bgDesc, zonePos,
                           ImVec2(zonePos.x + zoneW, zonePos.y + zoneH));
        } else {
            dlBg->AddRectFilled(zonePos, ImVec2(zonePos.x + zoneW, zonePos.y + zoneH),
                                IM_COL32(30, 30, 45, 180), 4.f);
            const char* hint = "Drop background image here";
            ImVec2 tsz = ImGui::CalcTextSize(hint);
            dlBg->AddText(ImVec2(zonePos.x + zoneW * 0.5f - tsz.x * 0.5f,
                                 zonePos.y + zoneH * 0.5f - tsz.y * 0.5f),
                          IM_COL32(120, 120, 140, 200), hint);
        }
        dlBg->AddRect(zonePos, ImVec2(zonePos.x + zoneW, zonePos.y + zoneH),
                      border, 4.f, 0, 1.5f);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                std::string rel(static_cast<const char*>(payload->Data),
                                payload->DataSize - 1);
                m_pageBackground = rel;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear##pagebg"))
            m_pageBackground.clear();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ── Achievement badge images (page-level) ────────────────────────────────
    // Shared by every song/chart in the game; moved here from the per-chart
    // SongEditor so authors only manage one badge pair.
    {
        auto badgeDropZone = [&](const char* label, const char* idBase,
                                  std::string& outPath) {
            ImGui::TextUnformatted(label);
            // Square zone so badges (which are usually circular or
            // rhombus-shaped) display at their real aspect ratio.
            const float zoneSide = 96.f;
            ImVec2 zonePos = ImGui::GetCursorScreenPos();
            ImGui::PushID(idBase);
            ImGui::InvisibleButton("##zone", ImVec2(zoneSide, zoneSide));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            VkDescriptorSet thumb = outPath.empty()
                ? VK_NULL_HANDLE : getThumb(outPath);
            ImU32 border = ImGui::IsItemHovered()
                ? IM_COL32(100, 160, 255, 255) : IM_COL32(100, 100, 120, 180);
            // Backing panel
            dl->AddRectFilled(zonePos,
                              ImVec2(zonePos.x + zoneSide, zonePos.y + zoneSide),
                              IM_COL32(30, 30, 45, 220), 4.f);
            if (thumb) {
                // Aspect-fit the image inside the square so it isn't stretched.
                ImVec2 imgSz(64.f, 64.f);
                auto it = m_thumbCache.find(outPath);
                if (it != m_thumbCache.end() &&
                    it->second.tex.width > 0 && it->second.tex.height > 0) {
                    imgSz = ImVec2((float)it->second.tex.width,
                                   (float)it->second.tex.height);
                }
                float scale = std::min(zoneSide / imgSz.x, zoneSide / imgSz.y);
                float fitW  = imgSz.x * scale;
                float fitH  = imgSz.y * scale;
                ImVec2 imgMin(zonePos.x + (zoneSide - fitW) * 0.5f,
                              zonePos.y + (zoneSide - fitH) * 0.5f);
                ImVec2 imgMax(imgMin.x + fitW, imgMin.y + fitH);
                dl->AddImage((ImTextureID)(uint64_t)thumb, imgMin, imgMax);
            } else {
                const char* hint = "Drop\nbadge\nhere";
                ImVec2 tsz = ImGui::CalcTextSize(hint);
                dl->AddText(ImVec2(zonePos.x + zoneSide * 0.5f - tsz.x * 0.5f,
                                   zonePos.y + zoneSide * 0.5f - tsz.y * 0.5f),
                            IM_COL32(120, 120, 140, 200), hint);
            }
            dl->AddRect(zonePos,
                        ImVec2(zonePos.x + zoneSide, zonePos.y + zoneSide),
                        border, 4.f, 0, 1.5f);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    outPath = std::string(
                        static_cast<const char*>(payload->Data),
                        payload->DataSize - 1);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (ImGui::Button("Clear")) outPath.clear();
            ImGui::EndGroup();
            ImGui::PopID();
        };

        ImGui::TextUnformatted("Achievement Badges:");
        ImGui::TextDisabled("Shown on the results screen when a chart clears FC / AP.");
        ImGui::Spacing();
        badgeDropZone("Full Combo (FC)",  "fcbadge", m_fcImage);
        ImGui::Spacing();
        badgeDropZone("All Perfect (AP)", "apbadge", m_apImage);
        ImGui::Spacing();
        const char* btnLabel = m_showAchievementPreview
            ? "Hide Badge Preview" : "Preview Badges in Scene";
        if (ImGui::Button(btnLabel, ImVec2(-1, 0)))
            m_showAchievementPreview = !m_showAchievementPreview;
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ── Add Set button ───────────────────────────────────────────────────────
    if (ImGui::Button("+ Add Set", ImVec2(-1, 0))) {
        m_showAddSetDialog = true;
        m_newSetName[0] = '\0';
    }

    ImGui::Spacing();

    // ── Tree: Sets -> Songs ──────────────────────────────────────────────────
    for (int si = 0; si < (int)m_sets.size(); ++si) {
        auto& set = m_sets[si];

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
        if (si == m_selectedSet && m_selectedSong < 0)
            flags |= ImGuiTreeNodeFlags_Selected;

        bool open = ImGui::TreeNodeEx((void*)(intptr_t)si, flags, "%s", set.name.c_str());

        // Click to select set
        if (ImGui::IsItemClicked()) {
            m_selectedSet = si;
            m_setScrollTarget = (float)si;
            m_selectedSong = -1;
            m_songScrollTarget = 0.f;
        }

        // Context menu for set
        char setPopupId[32];
        snprintf(setPopupId, sizeof(setPopupId), "set_ctx_%d", si);
        if (ImGui::BeginPopupContextItem(setPopupId)) {
            if (ImGui::MenuItem("Add Song")) {
                m_selectedSet = si;
                m_showAddSongDialog = true;
                m_newSongName[0] = '\0';
                m_newSongArtist[0] = '\0';
            }
            if (ImGui::MenuItem("Rename")) {
                // inline rename is complex; for now just copy name to edit
                strncpy(m_newSetName, set.name.c_str(), 127);
                m_newSetName[127] = '\0';
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Set")) {
                m_sets.erase(m_sets.begin() + si);
                if (m_selectedSet >= (int)m_sets.size())
                    m_selectedSet = (int)m_sets.size() - 1;
                m_selectedSong = -1;
                ImGui::EndPopup();
                if (open) ImGui::TreePop();
                break; // iterator invalidated
            }
            ImGui::EndPopup();
        }

        if (open) {
            // Add Song button inside tree
            char addSongId[32];
            snprintf(addSongId, sizeof(addSongId), "##addsong_%d", si);
            if (ImGui::SmallButton(("+ Add Song" + std::string(addSongId)).c_str())) {
                m_selectedSet = si;
                m_showAddSongDialog = true;
                m_newSongName[0] = '\0';
                m_newSongArtist[0] = '\0';
            }

            for (int soi = 0; soi < (int)set.songs.size(); ++soi) {
                auto& song = set.songs[soi];

                ImGuiTreeNodeFlags songFlags =
                    ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (si == m_selectedSet && soi == m_selectedSong)
                    songFlags |= ImGuiTreeNodeFlags_Selected;

                ImGui::TreeNodeEx((void*)(intptr_t)(si * 1000 + soi), songFlags,
                                  "%s", song.name.c_str());

                if (ImGui::IsItemClicked()) {
                    m_selectedSet  = si;
                    m_selectedSong = soi;
                    m_setScrollTarget  = (float)si;
                    m_songScrollTarget = (float)soi;
                }

                // Double-click to open SongEditor
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_selectedSet  = si;
                    m_selectedSong = soi;
                    if (m_engine) {
                        m_engine->songEditor().setSong(&song, m_projectPath);
                        m_engine->switchLayer(EditorLayer::SongEditor);
                    }
                }

                // Context menu for song
                char songPopupId[48];
                snprintf(songPopupId, sizeof(songPopupId), "song_ctx_%d_%d", si, soi);
                if (ImGui::BeginPopupContextItem(songPopupId)) {
                    if (ImGui::MenuItem("Delete Song")) {
                        set.songs.erase(set.songs.begin() + soi);
                        if (m_selectedSong >= (int)set.songs.size())
                            m_selectedSong = (int)set.songs.size() - 1;
                        ImGui::EndPopup();
                        break; // iterator invalidated
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::TreePop();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // ── Properties for selected item ─────────────────────────────────────────
    if (m_selectedSet >= 0 && m_selectedSet < (int)m_sets.size()) {
        auto& set = m_sets[m_selectedSet];

        if (m_selectedSong >= 0 && m_selectedSong < (int)set.songs.size()) {
            // Song properties (slim: name, artist, cover only)
            auto& song = set.songs[m_selectedSong];
            ImGui::Text("Song Properties");
            ImGui::Separator();

            char nameBuf[128];
            strncpy(nameBuf, song.name.c_str(), 127); nameBuf[127] = '\0';
            if (ImGui::InputText("Name", nameBuf, 128))
                song.name = nameBuf;

            char artistBuf[128];
            strncpy(artistBuf, song.artist.c_str(), 127); artistBuf[127] = '\0';
            if (ImGui::InputText("Artist", artistBuf, 128))
                song.artist = artistBuf;

            // Cover image picker: thumbnail + Browse + drag-drop
            ImGui::Text("Cover:");
            VkDescriptorSet coverDesc = getCoverDesc(song.coverImage);
            float thumbSz = 100.f;
            if (coverDesc) {
                ImGui::Image((ImTextureID)(uint64_t)coverDesc, ImVec2(thumbSz, thumbSz));
            } else {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(pos, ImVec2(pos.x + thumbSz, pos.y + thumbSz),
                                  IM_COL32(50, 50, 70, 255), 4.f);
                ImVec2 textSz = ImGui::CalcTextSize("No Cover");
                dl->AddText(ImVec2(pos.x + (thumbSz - textSz.x) * 0.5f,
                                   pos.y + (thumbSz - textSz.y) * 0.5f),
                            IM_COL32(140, 140, 160, 200), "No Cover");
                ImGui::Dummy(ImVec2(thumbSz, thumbSz));
            }
            // Drag-drop target on the cover zone
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    std::string rel(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                    m_coverCache.erase(song.coverImage);
                    song.coverImage = rel;
                }
                ImGui::EndDragDropTarget();
            }
#ifdef _WIN32
            ImGui::SameLine();
            if (ImGui::Button("Browse##songcover")) {
                OPENFILENAMEW ofn = {};
                wchar_t szFile[512] = {};
                ofn.lStructSize  = sizeof(ofn);
                ofn.hwndOwner    = m_window ? glfwGetWin32Window(m_window) : nullptr;
                ofn.lpstrFile    = szFile;
                ofn.nMaxFile     = static_cast<DWORD>(sizeof(szFile) / sizeof(wchar_t));
                ofn.lpstrFilter  = L"Images\0*.png;*.jpg;*.jpeg\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, nullptr, 0, nullptr, nullptr);
                    std::string srcPath(len - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, szFile, -1, srcPath.data(), len, nullptr, nullptr);

                    fs::path absProject = fs::absolute(fs::path(m_projectPath));
                    fs::path destDir    = absProject / "assets" / "textures";
                    fs::create_directories(destDir);
                    fs::path dest = destDir / fs::path(srcPath).filename();
                    try {
                        fs::copy_file(srcPath, dest, fs::copy_options::overwrite_existing);
                        std::string rel = fs::relative(dest, absProject).string();
                        std::replace(rel.begin(), rel.end(), '\\', '/');
                        m_coverCache.erase(song.coverImage);
                        song.coverImage = rel;
                    } catch (...) {}
                }
            }
#endif
            if (!song.coverImage.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##songcover"))
                    song.coverImage.clear();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Score and FC/AP badges are filled in by the");
            ImGui::TextDisabled("judgement system after the player clears a chart.");
            ImGui::Spacing();
            ImGui::TextDisabled("Double-click song to edit charts, audio, score...");
            if (ImGui::Button("Edit Song Details >>")) {
                if (m_engine) {
                    m_engine->songEditor().setSong(&song, m_projectPath);
                    m_engine->switchLayer(EditorLayer::SongEditor);
                }
            }
        } else {
            // Set properties
            ImGui::Text("Set Properties");
            ImGui::Separator();

            char nameBuf[128];
            strncpy(nameBuf, set.name.c_str(), 127); nameBuf[127] = '\0';
            if (ImGui::InputText("Set Name", nameBuf, 128))
                set.name = nameBuf;

            // Set cover image picker
            ImGui::Text("Cover:");
            VkDescriptorSet setCoverDesc = getCoverDesc(set.coverImage);
            float thumbSz = 100.f;
            if (setCoverDesc) {
                ImGui::Image((ImTextureID)(uint64_t)setCoverDesc, ImVec2(thumbSz, thumbSz));
            } else {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(pos, ImVec2(pos.x + thumbSz, pos.y + thumbSz),
                                  IM_COL32(50, 50, 70, 255), 4.f);
                ImVec2 textSz = ImGui::CalcTextSize("No Cover");
                dl->AddText(ImVec2(pos.x + (thumbSz - textSz.x) * 0.5f,
                                   pos.y + (thumbSz - textSz.y) * 0.5f),
                            IM_COL32(140, 140, 160, 200), "No Cover");
                ImGui::Dummy(ImVec2(thumbSz, thumbSz));
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    std::string rel(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                    m_coverCache.erase(set.coverImage);
                    set.coverImage = rel;
                }
                ImGui::EndDragDropTarget();
            }
#ifdef _WIN32
            ImGui::SameLine();
            if (ImGui::Button("Browse##setcover")) {
                OPENFILENAMEW ofn = {};
                wchar_t szFile[512] = {};
                ofn.lStructSize  = sizeof(ofn);
                ofn.hwndOwner    = m_window ? glfwGetWin32Window(m_window) : nullptr;
                ofn.lpstrFile    = szFile;
                ofn.nMaxFile     = static_cast<DWORD>(sizeof(szFile) / sizeof(wchar_t));
                ofn.lpstrFilter  = L"Images\0*.png;*.jpg;*.jpeg\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, szFile, -1, nullptr, 0, nullptr, nullptr);
                    std::string srcPath(len - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, szFile, -1, srcPath.data(), len, nullptr, nullptr);

                    fs::path absProject = fs::absolute(fs::path(m_projectPath));
                    fs::path destDir    = absProject / "assets" / "textures";
                    fs::create_directories(destDir);
                    fs::path dest = destDir / fs::path(srcPath).filename();
                    try {
                        fs::copy_file(srcPath, dest, fs::copy_options::overwrite_existing);
                        std::string rel = fs::relative(dest, absProject).string();
                        std::replace(rel.begin(), rel.end(), '\\', '/');
                        m_coverCache.erase(set.coverImage);
                        set.coverImage = rel;
                    } catch (...) {}
                }
            }
#endif
            if (!set.coverImage.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##setcover"))
                    set.coverImage.clear();
            }

            ImGui::Text("Songs: %d", (int)set.songs.size());
        }
    }

    // ── Add Set Dialog ───────────────────────────────────────────────────────
    if (m_showAddSetDialog) {
        ImGui::OpenPopup("Add Music Set");
        m_showAddSetDialog = false;
    }
    if (ImGui::BeginPopupModal("Add Music Set", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Set Name", m_newSetName, 128);
        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            if (m_newSetName[0] != '\0') {
                MusicSetInfo newSet;
                newSet.name = m_newSetName;
                m_sets.push_back(std::move(newSet));
                m_selectedSet = (int)m_sets.size() - 1;
                m_setScrollTarget = (float)m_selectedSet;
                m_selectedSong = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ── Add Song Dialog ──────────────────────────────────────────────────────
    if (m_showAddSongDialog) {
        ImGui::OpenPopup("Add Song");
        m_showAddSongDialog = false;
    }
    if (ImGui::BeginPopupModal("Add Song", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Song Name", m_newSongName, 128);
        ImGui::InputText("Artist", m_newSongArtist, 128);
        ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            if (m_newSongName[0] != '\0' && m_selectedSet >= 0 &&
                m_selectedSet < (int)m_sets.size()) {
                SongInfo newSong;
                newSong.name   = m_newSongName;
                newSong.artist = m_newSongArtist;
                m_sets[m_selectedSet].songs.push_back(std::move(newSong));
                m_selectedSong = (int)m_sets[m_selectedSet].songs.size() - 1;
                m_songScrollTarget = (float)m_selectedSong;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ── renderAssets ─────────────────────────────────────────────────────────────

void MusicSelectionEditor::renderAssets() {
    // ── toolbar ──────────────────────────────────────────────────────────────
    if (ImGui::Button("Open File...")) {
#ifdef _WIN32
        OPENFILENAMEW ofn = {};
        wchar_t szFile[4096] = {};
        ofn.lStructSize  = sizeof(ofn);
        ofn.hwndOwner    = m_window ? glfwGetWin32Window(m_window) : nullptr;
        ofn.lpstrFile    = szFile;
        ofn.nMaxFile     = static_cast<DWORD>(sizeof(szFile) / sizeof(wchar_t));
        ofn.lpstrFilter  = L"All Files\0*.*\0"
                           L"Audio\0*.mp3;*.ogg;*.wav;*.flac;*.aac\0"
                           L"Images\0*.png;*.jpg;*.jpeg;*.gif\0";
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
    const float tileSpacing = 6.f;
    std::string toDelete;

    // Wrap to next row when the next tile would overflow the panel width.
    auto flowNext = [&]() {
        float lastEndX  = ImGui::GetItemRectMax().x;
        float rightEdge = ImGui::GetWindowPos().x +
                          ImGui::GetWindowContentRegionMax().x;
        if (lastEndX + tileSpacing + thumbSize <= rightEdge)
            ImGui::SameLine(0.f, tileSpacing);
    };

    // Lambda to draw a horizontal strip of draggable thumbnails
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
            flowNext();
            ImGui::PopID();
        }
        ImGui::NewLine();
        ImGui::Spacing();
    };

    if (!m_assets.images.empty()) { ImGui::Text("Images:"); drawThumbs(m_assets.images); }
    if (!m_assets.gifs.empty())   { ImGui::Text("GIFs:");   drawThumbs(m_assets.gifs); }

    // Audio files — styled placeholder
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
            flowNext();
            ImGui::PopID();
        }
        ImGui::NewLine();
        ImGui::Spacing();
    }

    // Material tiles — same renderer as StartScreenEditor's Assets panel
    // so the user sees the same previews on every page.
    if (!m_assets.materials.empty()) {
        ImGui::Text("Materials:");
        for (int i = 0; i < (int)m_assets.materials.size(); ++i) {
            const std::string& f = m_assets.materials[i];
            std::string name = fs::path(f).filename().string();
            std::string stem = fs::path(f).stem().string();
            ImGui::PushID(2000 + i);
            ImGui::BeginGroup();
            ImVec2 thumbPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##m", ImVec2(thumbSize, thumbSize));
            ImDrawList* dl = ImGui::GetWindowDrawList();

            const MaterialAsset* matPtr = m_engine
                ? m_engine->materialLibrary().get(stem) : nullptr;
            if (matPtr && m_engine) {
                m_engine->startScreenEditor().drawMaterialPreviewAt(
                    *matPtr, thumbPos, ImVec2(thumbSize, thumbSize));
            } else {
                dl->AddRectFilled(thumbPos,
                                  ImVec2(thumbPos.x + thumbSize, thumbPos.y + thumbSize),
                                  IM_COL32(50, 30, 70, 255), 4.f);
                const char* icon = "MAT";
                ImVec2 mizs = ImGui::CalcTextSize(icon);
                dl->AddText(ImVec2(thumbPos.x + thumbSize * 0.5f - mizs.x * 0.5f,
                                   thumbPos.y + thumbSize * 0.5f - mizs.y * 0.5f),
                            IM_COL32(220, 180, 255, 220), icon);
            }
            if (ImGui::IsItemHovered()) {
                dl->AddRect(thumbPos,
                            ImVec2(thumbPos.x + thumbSize, thumbPos.y + thumbSize),
                            IM_COL32(200, 140, 255, 200), 4.f, 0, 2.f);
                if (matPtr && m_engine) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(f.c_str());
                    if (!matPtr->targetMode.empty() ||
                        !matPtr->targetSlotSlug.empty()) {
                        ImGui::TextDisabled("target: %s / %s",
                            matPtr->targetMode.empty()     ? "(any)" : matPtr->targetMode.c_str(),
                            matPtr->targetSlotSlug.empty() ? "(any)" : matPtr->targetSlotSlug.c_str());
                    }
                    m_engine->startScreenEditor().drawMaterialPreviewAt(
                        *matPtr, ImGui::GetCursorScreenPos(), ImVec2(260.f, 140.f));
                    ImGui::Dummy(ImVec2(260.f, 140.f));
                    ImGui::EndTooltip();
                } else {
                    ImGui::SetTooltip("%s", f.c_str());
                }
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ASSET_PATH", f.c_str(), f.size() + 1);
                ImGui::Text("%s", name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginPopupContextItem("##mctx")) {
                if (ImGui::MenuItem("Delete")) toDelete = f;
                ImGui::EndPopup();
            }
            std::string lbl = stem.size() > 11 ? stem.substr(0, 9) + ".." : stem;
            ImVec2 mlsz = ImGui::CalcTextSize(lbl.c_str());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (thumbSize - mlsz.x) * 0.5f);
            ImGui::TextDisabled("%s", lbl.c_str());
            ImGui::EndGroup();
            flowNext();
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
