#include "MusicSelectionView.h"
#include "engine/IPlayerEngine.h"
#include "engine/AudioEngine.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/BufferManager.h"
#include "renderer/vulkan/TextureManager.h"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// Re-encode `s` as valid UTF-8 (CP_ACP fallback on Windows). See
// MusicSelectionEditor.cpp for the original rationale; duplicated here so the
// view can save() without depending on the editor TU.
static std::string toUtf8(const std::string& s) {
    auto isValidUtf8 = [](const std::string& str) {
        size_t i = 0;
        while (i < str.size()) {
            unsigned char c = (unsigned char)str[i];
            int extra;
            if      (c < 0x80)              extra = 0;
            else if ((c & 0xE0) == 0xC0)    extra = 1;
            else if ((c & 0xF0) == 0xE0)    extra = 2;
            else if ((c & 0xF8) == 0xF0)    extra = 3;
            else                            return false;
            if (i + extra >= str.size()) return false;
            for (int k = 1; k <= extra; ++k) {
                if ((((unsigned char)str[i + k]) & 0xC0) != 0x80) return false;
            }
            i += extra + 1;
        }
        return true;
    };
    if (s.empty() || isValidUtf8(s)) return s;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), nullptr, 0);
    if (wlen <= 0) return s;
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), &w[0], wlen);
    int u8len = WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return s;
    std::string out(u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, &out[0], u8len, nullptr, nullptr);
    return out;
#else
    return s;
#endif
}

void MusicSelectionView::initVulkan(VulkanContext& ctx, BufferManager& bufMgr,
                                    ImGuiLayer* imgui) {
    m_ctx    = &ctx;
    m_bufMgr = &bufMgr;
    m_imgui  = imgui;
}

void MusicSelectionView::shutdownVulkan(VulkanContext& /*ctx*/, BufferManager& /*bufMgr*/) {
    clearCovers();
}

void MusicSelectionView::clearCovers() {
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

VkDescriptorSet MusicSelectionView::getCoverDesc(const std::string& relPath) {
    if (relPath.empty()) return VK_NULL_HANDLE;
    auto it = m_coverCache.find(relPath);
    if (it != m_coverCache.end()) return it->second.desc;
    if (!m_ctx || !m_bufMgr) return VK_NULL_HANDLE;

    std::string fullPath = m_projectPath + "/" + relPath;
    try {
        CoverEntry entry;
        TextureManager texMgr;
        texMgr.init(*m_ctx, *m_bufMgr);
        entry.tex  = texMgr.loadFromFile(*m_ctx, *m_bufMgr, fullPath);
        entry.desc = ImGui_ImplVulkan_AddTexture(
            entry.tex.sampler, entry.tex.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        auto& stored = m_coverCache[relPath] = std::move(entry);
        return stored.desc;
    } catch (...) {
        m_coverCache[relPath] = {};
        return VK_NULL_HANDLE;
    }
}

void MusicSelectionView::load(const std::string& projectPath) {
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
                        song.gameMode.totalScore   = gm.value("totalScore", 1000000);
                        song.gameMode.fcImage      = gm.value("fcImage", "");
                        song.gameMode.apImage      = gm.value("apImage", "");

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

                        if (gm.contains("cameraEye") && gm["cameraEye"].is_array() && gm["cameraEye"].size() >= 3)
                            for (int i = 0; i < 3; ++i) song.gameMode.cameraEye[i] = gm["cameraEye"][i].get<float>();
                        if (gm.contains("cameraTarget") && gm["cameraTarget"].is_array() && gm["cameraTarget"].size() >= 3)
                            for (int i = 0; i < 3; ++i) song.gameMode.cameraTarget[i] = gm["cameraTarget"][i].get<float>();
                        song.gameMode.cameraFov = gm.value("cameraFov", 55.f);

                        song.gameMode.backgroundImage = gm.value("backgroundImage", "");
                        song.gameMode.skyHeight = gm.value("skyHeight", 1.f);

                        song.gameMode.diskInnerRadius  = gm.value("diskInnerRadius",  0.9f);
                        song.gameMode.diskBaseRadius   = gm.value("diskBaseRadius",   2.4f);
                        song.gameMode.diskRingSpacing  = gm.value("diskRingSpacing",  0.6f);
                        song.gameMode.diskInitialScale = gm.value("diskInitialScale", 1.0f);

                        if (gm.contains("noteAssets") && gm["noteAssets"].is_object()) {
                            for (auto it = gm["noteAssets"].begin();
                                 it != gm["noteAssets"].end(); ++it) {
                                GameModeConfig::NoteTypeAssets na;
                                na.texturePath = it.value().value("texturePath", "");
                                na.sfxPath     = it.value().value("sfxPath", "");
                                song.gameMode.noteAssets[it.key()] = na;
                            }
                        }
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

void MusicSelectionView::save() {
    if (m_projectPath.empty()) return;

    json j;
    json setsArr = json::array();
    for (auto& set : m_sets) {
        json sj;
        sj["name"]       = toUtf8(set.name);
        sj["coverImage"] = toUtf8(set.coverImage);
        json songsArr = json::array();
        for (auto& song : set.songs) {
            json songJ;
            songJ["name"]        = toUtf8(song.name);
            songJ["artist"]      = toUtf8(song.artist);
            songJ["coverImage"]  = toUtf8(song.coverImage);
            songJ["audioFile"]   = toUtf8(song.audioFile);
            songJ["chartEasy"]   = toUtf8(song.chartEasy);
            songJ["chartMedium"] = toUtf8(song.chartMedium);
            songJ["chartHard"]   = toUtf8(song.chartHard);
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
            gmJ["totalScore"]   = song.gameMode.totalScore;
            gmJ["fcImage"]      = toUtf8(song.gameMode.fcImage);
            gmJ["apImage"]      = toUtf8(song.gameMode.apImage);

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

            gmJ["cameraEye"]    = {song.gameMode.cameraEye[0], song.gameMode.cameraEye[1], song.gameMode.cameraEye[2]};
            gmJ["cameraTarget"] = {song.gameMode.cameraTarget[0], song.gameMode.cameraTarget[1], song.gameMode.cameraTarget[2]};
            gmJ["cameraFov"]    = song.gameMode.cameraFov;

            gmJ["backgroundImage"] = toUtf8(song.gameMode.backgroundImage);
            gmJ["skyHeight"] = song.gameMode.skyHeight;

            gmJ["diskInnerRadius"]  = song.gameMode.diskInnerRadius;
            gmJ["diskBaseRadius"]   = song.gameMode.diskBaseRadius;
            gmJ["diskRingSpacing"]  = song.gameMode.diskRingSpacing;
            gmJ["diskInitialScale"] = song.gameMode.diskInitialScale;

            if (!song.gameMode.noteAssets.empty()) {
                json naJ = json::object();
                for (const auto& kv : song.gameMode.noteAssets) {
                    json entry;
                    entry["texturePath"] = toUtf8(kv.second.texturePath);
                    entry["sfxPath"]     = toUtf8(kv.second.sfxPath);
                    naJ[toUtf8(kv.first)] = entry;
                }
                gmJ["noteAssets"] = naJ;
            }

            songJ["gameMode"] = gmJ;
            songsArr.push_back(songJ);
        }
        sj["songs"] = songsArr;
        setsArr.push_back(sj);
    }
    j["sets"]       = setsArr;
    j["background"] = toUtf8(m_pageBackground);
    j["fcImage"]    = toUtf8(m_fcImage);
    j["apImage"]    = toUtf8(m_apImage);

    std::ofstream out(m_projectPath + "/music_selection.json");
    if (!out.is_open()) return;
    try {
        out << j.dump(2);
    } catch (const std::exception& e) {
        std::cerr << "[MusicSelectionView::save] dump failed: "
                  << e.what() << " — file not written\n";
    }
}

void MusicSelectionView::updateAudioPreview(float dt, IPlayerEngine* engine) {
    if (!engine) return;
    if (!engine->isTestMode()) {
        if (m_previewPlaying) {
            engine->audio().stop();
            m_previewPlaying = false;
        }
        m_previewDwellT = 0.f;
        m_previewStopT  = 0.f;
        return;
    }
    AudioEngine& ae = engine->audio();

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
        m_previewDwellT += dt;
        if (m_previewDwellT < 0.5f) return;

        std::string full = m_projectPath + "/" + song.audioFile;
        if (m_previewPath != full) {
            if (!ae.load(full)) return;
            m_previewPath = full;
        }
        float start = song.previewStart;
        if (start < 0.f) {
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

void MusicSelectionView::update(float dt, IPlayerEngine* engine) {
    float lerpSpeed = 8.f;
    m_setScrollCurrent  += (m_setScrollTarget  - m_setScrollCurrent)  * std::min(1.f, lerpSpeed * dt);
    m_songScrollCurrent += (m_songScrollTarget - m_songScrollCurrent) * std::min(1.f, lerpSpeed * dt);

    if (m_selectedSet < 0 && !m_sets.empty()) {
        m_selectedSet     = 0;
        m_setScrollTarget = 0.f;
        if (!m_sets[0].songs.empty()) {
            m_selectedSong     = 0;
            m_songScrollTarget = 0.f;
        }
    }

    updateAudioPreview(dt, engine);
}

SongInfo* MusicSelectionView::getSelectedSong() {
    if (m_selectedSet < 0 || m_selectedSet >= (int)m_sets.size()) return nullptr;
    auto& set = m_sets[m_selectedSet];
    if (m_selectedSong < 0 || m_selectedSong >= (int)set.songs.size()) return nullptr;
    return &set.songs[m_selectedSong];
}

void MusicSelectionView::renderGamePreview(ImVec2 p, ImVec2 size, IPlayerEngine* engine) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pw = size.x, ph = size.y;

    VkDescriptorSet pageBg = m_pageBackground.empty()
        ? VK_NULL_HANDLE : getCoverDesc(m_pageBackground);
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
    renderPlayButton(ImVec2(centerX, playY), centerW, engine);
}

void MusicSelectionView::renderSetWheel(ImVec2 origin, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

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
        bool isSelected = (card.index == m_selectedSet);

        ImU32 bgCol = isSelected
            ? IM_COL32(60, 80, 160, alpha)
            : IM_COL32(40, 42, 55, alpha);
        dl->AddQuadFilled(tl, tr, br, bl, bgCol);

        if (isSelected) {
            dl->AddQuad(tl, tr, br, bl, IM_COL32(120, 160, 255, (int)(255 * alphaFactor)), 2.f);
        }

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

        float quadCX = (tl.x + tr.x + br.x + bl.x) * 0.25f;
        float quadCY = (tl.y + tr.y + br.y + bl.y) * 0.25f;
        float textOffsetX = sw * thumbFrac * 0.5f;

        const char* setName = m_sets[card.index].name.c_str();
        ImVec2 textSz = ImGui::CalcTextSize(setName);
        dl->AddText(ImVec2(quadCX + textOffsetX - textSz.x * 0.5f,
                           quadCY - textSz.y * 0.5f),
                    IM_COL32(230, 230, 240, (int)(255 * alphaFactor)),
                    setName);

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

void MusicSelectionView::renderSongWheel(ImVec2 origin, float width, float height) {
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

        ImU32 bgCol = isSelected
            ? IM_COL32(160, 60, 100, alpha)
            : IM_COL32(40, 42, 55, alpha);
        dl->AddQuadFilled(tl, tr, br, bl, bgCol);

        if (isSelected) {
            dl->AddQuad(tl, tr, br, bl, IM_COL32(255, 120, 180, (int)(255 * alphaFactor)), 2.f);
        }

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

        ImVec2 cardMin(std::min({tl.x, tr.x, br.x, bl.x}),
                       std::min({tl.y, tr.y, br.y, bl.y}));
        ImVec2 cardMax(std::max({tl.x, tr.x, br.x, bl.x}),
                       std::max({tl.y, tr.y, br.y, bl.y}));
        dl->PushClipRect(cardMin, cardMax, true);

        float quadCX = (tl.x + tr.x + br.x + bl.x) * 0.25f;
        float quadCY = (tl.y + tr.y + br.y + bl.y) * 0.25f;

        float cardWFull   = sw * 2.f;
        float coverW      = cardWFull * thumbFrac;
        float padding     = sw * 0.04f;
        float rhombusH    = std::min(sh * 1.60f, cardWFull * 0.20f);
        float rhombusW    = rhombusH;
        float overlap     = rhombusW * 0.25f;
        float rhombusPairW = rhombusW + (rhombusW - overlap);
        float rhombusAreaW = rhombusPairW + padding;

        float textColLeft  = tl.x + coverW + padding;
        float textColRight = tr.x - rhombusAreaW;
        if (textColRight < textColLeft + 10.f)
            textColRight = textColLeft + 10.f;
        float textBaseX    = (textColLeft + textColRight) * 0.5f;

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
            ? VK_NULL_HANDLE : getCoverDesc(m_fcImage);
        VkDescriptorSet apTex = m_apImage.empty()
            ? VK_NULL_HANDLE : getCoverDesc(m_apImage);

        bool fcUnlocked = false, apUnlocked = false;
        if (diffAch && !diffAch->empty()) {
            std::string low = *diffAch;
            for (char& c : low) c = (char)std::tolower((unsigned char)c);
            if      (low == "ap") { fcUnlocked = true; apUnlocked = true; }
            else if (low == "fc") { fcUnlocked = true; }
        }

        const char* songName = song.name.c_str();
        ImVec2 nameSz  = ImGui::CalcTextSize(songName);
        char scoreBuf[32];
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", diffScore);
        ImVec2 scoreSz = ImGui::CalcTextSize(scoreBuf);

        float nameY  = quadCY - sh * 0.32f;
        float scoreY = quadCY + sh * 0.12f;
        float rhombusRight = tr.x - padding;
        float apCX = rhombusRight - rhombusW * 0.5f;
        float fcCX = apCX - (rhombusW - overlap);
        float rhombusCY = quadCY;

        auto drawRhombusSlot = [&](float cx_, float cy_,
                                    VkDescriptorSet tex, bool unlocked,
                                    ImU32 fillCol) {
            float hw = rhombusW * 0.5f;
            float hh = rhombusH * 0.5f;
            ImVec2 pN(cx_,      cy_ - hh);
            ImVec2 pE(cx_ + hw, cy_);
            ImVec2 pS(cx_,      cy_ + hh);
            ImVec2 pW(cx_ - hw, cy_);

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

        dl->AddText(ImVec2(textBaseX - nameSz.x * 0.5f, nameY),
                    IM_COL32(240, 240, 250, (int)(255 * alphaFactor)),
                    songName);
        dl->AddText(ImVec2(textBaseX - scoreSz.x * 0.5f, scoreY),
                    IM_COL32(200, 200, 220, (int)(220 * alphaFactor)),
                    scoreBuf);

        dl->PopClipRect();

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
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_selectedSong = card.index;
            m_songScrollTarget = (float)card.index;
            onSongCardDoubleClick(card.index);
        }
    }
}

void MusicSelectionView::renderCoverPhoto(ImVec2 origin, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

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

    dl->AddRectFilled(ImVec2(tl.x + 4.f, tl.y + 4.f),
                      ImVec2(br.x + 4.f, br.y + 4.f),
                      IM_COL32(0, 0, 0, 120), 8.f);

    VkDescriptorSet desc = getCoverDesc(coverPath);
    if (desc) {
        dl->AddImageRounded((ImTextureID)(uint64_t)desc, tl, br,
                            ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32(255, 255, 255, 255), 8.f);
    } else {
        dl->AddRectFilled(tl, br, IM_COL32(45, 45, 65, 255), 8.f);
        const char* placeholder = coverPath.empty() ? "No Cover" : "Loading...";
        ImVec2 textSz = ImGui::CalcTextSize(placeholder);
        dl->AddText(ImVec2(origin.x - textSz.x * 0.5f, origin.y + size * 0.5f - textSz.y * 0.5f),
                    IM_COL32(150, 150, 170, 200), placeholder);
    }

    dl->AddRect(tl, br, IM_COL32(100, 110, 140, 180), 8.f, 0, 1.5f);

    if (!label.empty()) {
        ImVec2 labelSz = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(origin.x - labelSz.x * 0.5f, br.y + 6.f),
                    IM_COL32(220, 220, 240, 255), label.c_str());
    }
}

void MusicSelectionView::renderDifficultyButtons(ImVec2 origin, float /*width*/) {
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

        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        char id[32];
        snprintf(id, sizeof(id), "##diff_%d", i);
        if (ImGui::InvisibleButton(id, ImVec2(btnW, btnH))) {
            m_selectedDifficulty = diffs[i].diff;
        }
    }
}

void MusicSelectionView::renderPlayButton(ImVec2 origin, float /*width*/, IPlayerEngine* engine) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    float btnW = 160.f;
    float btnH = 44.f;
    float bx = origin.x - btnW * 0.5f;
    float by = origin.y;

    bool canPlay = (m_selectedSet >= 0 && m_selectedSet < (int)m_sets.size() &&
                    m_selectedSong >= 0 && m_selectedSong < (int)m_sets[m_selectedSet].songs.size());

    ImU32 bgCol   = canPlay ? IM_COL32(50, 120, 220, 240) : IM_COL32(60, 60, 70, 180);
    ImU32 textCol = canPlay ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 120, 130, 200);

    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool hovered = canPlay &&
        mousePos.x >= bx && mousePos.x <= bx + btnW &&
        mousePos.y >= by && mousePos.y <= by + btnH;

    if (hovered)
        bgCol = IM_COL32(70, 150, 255, 255);

    dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + btnW, by + btnH), bgCol, 8.f);
    dl->AddRect(ImVec2(bx, by), ImVec2(bx + btnW, by + btnH),
                IM_COL32(140, 180, 255, canPlay ? 200 : 80), 8.f, 0, 1.5f);

    const char* playText = "START";
    ImVec2 textSz = ImGui::CalcTextSize(playText);
    dl->AddText(ImVec2(bx + (btnW - textSz.x) * 0.5f, by + (btnH - textSz.y) * 0.5f),
                textCol, playText);

    float triSize = 10.f;
    float triX = bx + btnW * 0.5f - textSz.x * 0.5f - 20.f;
    float triY = by + btnH * 0.5f;
    dl->AddTriangleFilled(
        ImVec2(triX, triY - triSize),
        ImVec2(triX, triY + triSize),
        ImVec2(triX + triSize * 1.2f, triY),
        textCol);

    ImGui::SetCursorScreenPos(ImVec2(bx, by));
    if (ImGui::InvisibleButton("##play_btn", ImVec2(btnW, btnH)) && canPlay) {
        if (engine) {
            auto& song = m_sets[m_selectedSet].songs[m_selectedSong];
            engine->launchGameplay(song, m_selectedDifficulty, m_projectPath, m_autoPlay);
        }
    }

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
