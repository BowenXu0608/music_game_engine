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
#include <cfloat>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
// One row of an iOS-style picker mapped onto a vertical cylinder.
//  t       : signed offset in rows from the centered item.
//  depth   : 1 at the center, easing to 0 at the rim (edge-on).
//  alpha   : visibility, faded by depth.
//  y       : screen-space Y of the row's center.
struct CylSample { float y; float depth; float alpha; bool vis; };
CylSample cylSample(float t, float centerY, float radius, float angleStep) {
    float theta = t * angleStep;
    CylSample s;
    s.vis   = std::fabs(theta) < 1.48f;          // ~85deg; beyond is edge-on
    float c = std::cos(theta);
    s.depth = c > 0.f ? c : 0.f;
    s.y     = centerY + radius * std::sin(theta);
    s.alpha = std::pow(s.depth, 1.35f);
    return s;
}

// The fixed center selection band: faint accent fill + two hairlines, iOS-style.
void drawWheelBand(ImDrawList* dl, ImVec2 origin, float width, float centerY,
                   float rowPitch, ImU32 accent) {
    float pad = width * 0.06f;
    float x0 = origin.x + pad, x1 = origin.x + width - pad;
    float y0 = centerY - rowPitch * 0.5f, y1 = centerY + rowPitch * 0.5f;
    ImU32 fill = (accent & 0x00FFFFFFu) | (30u << 24);   // low-alpha accent
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill, 6.f);
    ImU32 line = IM_COL32(255, 255, 255, 48);
    dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y0), line, 1.2f);
    dl->AddLine(ImVec2(x0, y1), ImVec2(x1, y1), line, 1.2f);
}

// One cover+name roller row, cylinder-projected (foreshortened + faded).
// Returns the right edge X of the cover so callers can append extras.
float drawRollerRow(ImDrawList* dl, ImFont* font, float baseFont, ImVec2 origin,
                    float width, float rowPitch, const CylSample& cs,
                    VkDescriptorSet cover, const char* name, bool isSel,
                    ImU32 /*accent*/) {
    int ai = (int)(cs.alpha * 255.f);
    if (ai <= 3) return origin.x;
    float ds = 0.62f + 0.38f * cs.depth;
    float coverBase = std::min(rowPitch * 0.70f, width * 0.26f);
    float cw  = coverBase * ds;
    float ch  = cw * (0.45f + 0.55f * cs.depth);       // vertical foreshorten
    float cxc = origin.x + width * 0.09f + cw * 0.5f;
    float cyc = cs.y;
    ImVec2 c0(cxc - cw * 0.5f, cyc - ch * 0.5f), c1(cxc + cw * 0.5f, cyc - ch * 0.5f),
           c2(cxc + cw * 0.5f, cyc + ch * 0.5f), c3(cxc - cw * 0.5f, cyc + ch * 0.5f);
    if (cover)
        dl->AddImageQuad((ImTextureID)(uint64_t)cover, c0, c1, c2, c3,
                         ImVec2(0, 0), ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1),
                         IM_COL32(255, 255, 255, ai));
    else
        dl->AddRectFilled(c0, c2, IM_COL32(70, 72, 90, (int)(ai * 0.8f)), 3.f);

    float fontSz = baseFont * (0.86f + 0.30f * cs.depth);
    ImU32 col = isSel ? IM_COL32(255, 255, 255, ai)
                      : IM_COL32(206, 210, 226, (int)(ai * 0.85f));
    float tx  = cxc + cw * 0.5f + width * 0.06f;
    float trx = origin.x + width - width * 0.05f;
    ImVec2 tsz = font->CalcTextSizeA(fontSz, FLT_MAX, 0.f, name);
    float ty = cyc - tsz.y * 0.5f;
    dl->PushClipRect(ImVec2(tx, cyc - rowPitch), ImVec2(trx, cyc + rowPitch), true);
    dl->AddText(font, fontSz, ImVec2(tx, ty), col, name);
    dl->PopClipRect();
    return cxc + cw * 0.5f;
}
}  // namespace

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
    m_wheelScrollSfx = j.value("wheelScrollSfx", "");
    m_difficultySfx  = j.value("difficultySfx", "");
    m_wheelClickSfx  = j.value("wheelClickSfx", "");

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

                        // Free camera. Defaults reproduce the legacy per-mode
                        // framing when the keys are absent (2D vs 3D drop).
                        {
                            CameraDefaults cd = cameraDefaultsFor(song.gameMode.dimension);
                            auto& mc  = song.gameMode;
                            mc.cameraProjection =
                                (gm.value("cameraProjection", std::string("perspective")) == "orthographic")
                                    ? CameraProjection::Orthographic : CameraProjection::Perspective;
                            if (gm.contains("cameraPosition") && gm["cameraPosition"].is_array() && gm["cameraPosition"].size() >= 3)
                                for (int i = 0; i < 3; ++i) mc.cameraPosition[i] = gm["cameraPosition"][i].get<float>();
                            else for (int i = 0; i < 3; ++i) mc.cameraPosition[i] = cd.position[i];
                            if (gm.contains("cameraRotationDeg") && gm["cameraRotationDeg"].is_array() && gm["cameraRotationDeg"].size() >= 3)
                                for (int i = 0; i < 3; ++i) mc.cameraRotationDeg[i] = gm["cameraRotationDeg"][i].get<float>();
                            else for (int i = 0; i < 3; ++i) mc.cameraRotationDeg[i] = cd.rotationDeg[i];
                            mc.cameraFovYDeg   = gm.value("cameraFovYDeg",   cd.fovYDeg);
                            mc.cameraOrthoSize = gm.value("cameraOrthoSize", cd.orthoSize);
                            mc.cameraNearClip  = gm.value("cameraNearClip",  cd.nearClip);
                            mc.cameraFarClip   = gm.value("cameraFarClip",   cd.farClip);
                            mc.playfieldWidth  = gm.value("playfieldWidth",  cd.playfieldWidth);
                            mc.playfieldLength = gm.value("playfieldLength", cd.playfieldLength);
                        }

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
                                na.loopSfxPath = it.value().value("loopSfxPath", "");
                                song.gameMode.noteAssets[it.key()] = na;
                            }
                        }

                        if (gm.contains("particleEffects") &&
                            gm["particleEffects"].is_object()) {
                            for (auto it = gm["particleEffects"].begin();
                                 it != gm["particleEffects"].end(); ++it) {
                                if (it.value().is_string())
                                    song.gameMode.particleEffects[it.key()] =
                                        it.value().get<std::string>();
                            }
                        }

                        if (gm.contains("judgmentLabels") &&
                            gm["judgmentLabels"].is_object()) {
                            const auto& jl = gm["judgmentLabels"];
                            JudgmentLabels& L = song.gameMode.judgmentLabels;
                            L.perfect   = jl.value("perfect",   L.perfect);
                            L.goodEarly = jl.value("goodEarly", L.goodEarly);
                            L.goodLate  = jl.value("goodLate",  L.goodLate);
                            L.badEarly  = jl.value("badEarly",  L.badEarly);
                            L.badLate   = jl.value("badLate",   L.badLate);
                            L.miss      = jl.value("miss",      L.miss);
                            L.yOffset   = jl.value("yOffset",   L.yOffset);
                            L.fontSize  = jl.value("fontSize",  L.fontSize);
                            L.enabled   = jl.value("enabled",   L.enabled);
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

            gmJ["cameraProjection"] =
                (song.gameMode.cameraProjection == CameraProjection::Orthographic) ? "orthographic" : "perspective";
            gmJ["cameraPosition"]    = {song.gameMode.cameraPosition[0], song.gameMode.cameraPosition[1], song.gameMode.cameraPosition[2]};
            gmJ["cameraRotationDeg"] = {song.gameMode.cameraRotationDeg[0], song.gameMode.cameraRotationDeg[1], song.gameMode.cameraRotationDeg[2]};
            gmJ["cameraFovYDeg"]   = song.gameMode.cameraFovYDeg;
            gmJ["cameraOrthoSize"] = song.gameMode.cameraOrthoSize;
            gmJ["cameraNearClip"]  = song.gameMode.cameraNearClip;
            gmJ["cameraFarClip"]   = song.gameMode.cameraFarClip;
            gmJ["playfieldWidth"]  = song.gameMode.playfieldWidth;
            gmJ["playfieldLength"] = song.gameMode.playfieldLength;

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
                    entry["loopSfxPath"] = toUtf8(kv.second.loopSfxPath);
                    naJ[toUtf8(kv.first)] = entry;
                }
                gmJ["noteAssets"] = naJ;
            }

            if (!song.gameMode.particleEffects.empty()) {
                json peJ = json::object();
                for (const auto& kv : song.gameMode.particleEffects)
                    peJ[toUtf8(kv.first)] = toUtf8(kv.second);
                gmJ["particleEffects"] = peJ;
            }

            {
                const JudgmentLabels& L = song.gameMode.judgmentLabels;
                json jl;
                jl["perfect"]   = toUtf8(L.perfect);
                jl["goodEarly"] = toUtf8(L.goodEarly);
                jl["goodLate"]  = toUtf8(L.goodLate);
                jl["badEarly"]  = toUtf8(L.badEarly);
                jl["badLate"]   = toUtf8(L.badLate);
                jl["miss"]      = toUtf8(L.miss);
                jl["yOffset"]   = L.yOffset;
                jl["fontSize"]  = L.fontSize;
                jl["enabled"]   = L.enabled;
                gmJ["judgmentLabels"] = jl;
            }

            songJ["gameMode"] = gmJ;
            songsArr.push_back(songJ);
        }
        sj["songs"] = songsArr;
        setsArr.push_back(sj);
    }
    j["sets"]       = setsArr;
    j["background"]     = toUtf8(m_pageBackground);
    j["fcImage"]        = toUtf8(m_fcImage);
    j["apImage"]        = toUtf8(m_apImage);
    j["wheelScrollSfx"] = toUtf8(m_wheelScrollSfx);
    j["difficultySfx"]  = toUtf8(m_difficultySfx);
    j["wheelClickSfx"]  = toUtf8(m_wheelClickSfx);

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

void MusicSelectionView::playWheelSfx(IPlayerEngine* engine, const std::string& relPath,
                                      DefaultSfx::Role fallback) {
    if (!engine) return;
    if (!relPath.empty())
        engine->audio().playSfxFile(m_projectPath + "/" + relPath);
    else
        engine->audio().playCachedSfx(DefaultSfx::cacheKeyForRole(fallback));
}

void MusicSelectionView::update(float dt, IPlayerEngine* engine) {
    // Roller scroll physics (drag/flick/snap) live in tickWheelInput, called
    // each frame from the wheel render. update() only seeds initial selection.
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

    renderSetWheel(ImVec2(p.x, p.y), wheelW, ph, engine);
    renderSongWheel(ImVec2(p.x + pw - wheelW, p.y), wheelW, ph, engine);

    // The song/set wheels use a sound (not a particle spark) for selection
    // feedback. Suppress the button-tap spark while the cursor is over either
    // wheel band; the center buttons (difficulty / play) still spark.
    if (engine) {
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool overLeftWheel  = mp.x >= p.x && mp.x <= p.x + wheelW;
        bool overRightWheel = mp.x >= p.x + pw - wheelW && mp.x <= p.x + pw;
        bool inVert         = mp.y >= p.y && mp.y <= p.y + ph;
        if (inVert && (overLeftWheel || overRightWheel))
            engine->suppressUiTapParticle();
    }

    float centerX = p.x + wheelW + centerW * 0.5f;
    float coverY  = p.y + ph * 0.08f;
    renderCoverPhoto(ImVec2(centerX, coverY), coverSize);

    float diffY = coverY + coverSize + ph * 0.04f;
    renderDifficultyButtons(ImVec2(centerX, diffY), centerW, engine);

    float playY = diffY + 50.f;
    renderPlayButton(ImVec2(centerX, playY), centerW, engine);
}

int MusicSelectionView::tickWheelInput(IPlayerEngine* engine, const char* areaId,
        ImVec2 origin, float width, float height, int count, float rowPitch,
        float angleStep, float radius, float& cur, float& tgt, float& vel,
        DragWheel which, int prevCentered) {
    ImGuiIO& io = ImGui::GetIO();
    float dt = (io.DeltaTime > 0.f && io.DeltaTime < 0.1f) ? io.DeltaTime : 1.f / 60.f;
    float maxIdx  = (float)(count - 1);
    float centerY = origin.y + height * 0.5f;

    // Full-area transparent capture for drag/tap (covers the whole roller band).
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton(areaId, ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft);
    bool active      = ImGui::IsItemActive();
    bool activated   = ImGui::IsItemActivated();
    bool deactivated = ImGui::IsItemDeactivated();
    bool hovered     = ImGui::IsItemHovered();

    if (activated) {
        m_dragWheel = which;
        m_dragLastY = io.MousePos.y;
        m_dragTotal = 0.f;
        m_dragMoved = false;
        vel = 0.f;
    }
    bool owning = (m_dragWheel == which);

    if (active && owning) {
        // 1:1 finger tracking — content follows the drag.
        float dy = io.MousePos.y - m_dragLastY;
        m_dragLastY  = io.MousePos.y;
        m_dragTotal += std::fabs(dy);
        if (m_dragTotal > 6.f) m_dragMoved = true;
        if (rowPitch > 0.f && dy != 0.f) {
            float drows = -dy / rowPitch;
            cur += drows;
            vel  = drows / dt;            // carried into momentum on release
        }
        cur = std::clamp(cur, 0.f, maxIdx);
        tgt = cur;
    } else {
        if (hovered && io.MouseWheel != 0.f)   // desktop wheel = flick impulse
            vel += -io.MouseWheel * 7.f;

        if (std::fabs(vel) > 0.5f) {
            cur += vel * dt;
            vel *= std::exp(-9.f * dt);        // friction
            if (cur <= 0.f)    { cur = 0.f;    vel = 0.f; }
            if (cur >= maxIdx) { cur = maxIdx; vel = 0.f; }
            tgt = std::round(std::clamp(cur, 0.f, maxIdx));
        } else {
            vel = 0.f;                          // settle: snap to nearest row
            cur += (tgt - cur) * std::min(1.f, 14.f * dt);
        }
    }

    if (deactivated && owning) {
        if (!m_dragMoved && angleStep != 0.f && radius != 0.f) {
            // Tap (no drag): bring the tapped row to center.
            float ratio = std::clamp((io.MousePos.y - centerY) / radius, -1.f, 1.f);
            float t     = std::asin(ratio) / angleStep;
            tgt = std::round(std::clamp(cur + t, 0.f, maxIdx));
            vel = 0.f;
        }
        m_dragWheel = DragWheel::None;
    }

    int centered = (int)std::lround(std::clamp(cur, 0.f, maxIdx));
    if (centered != prevCentered)
        playWheelSfx(engine, m_wheelScrollSfx, DefaultSfx::Role::UiScroll);
    return centered;
}

void MusicSelectionView::renderSetWheel(ImVec2 origin, float width, float height, IPlayerEngine* engine) {
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
    float centerY = origin.y + height * 0.5f;

    float rowPitch  = std::clamp(height / 7.f, 46.f, 100.f);
    float angleStep = 0.34f;                       // radians between rows
    float radius    = rowPitch / std::sin(angleStep);

    // Input + physics (drag / flick / snap / wheel); plays scroll SFX on change.
    int prevSel = (m_selectedSet < 0) ? 0 : m_selectedSet;
    int sel = tickWheelInput(engine, "##setwheel_area", origin, width, height,
                             count, rowPitch, angleStep, radius,
                             m_setScrollCurrent, m_setScrollTarget, m_setScrollVel,
                             DragWheel::Set, prevSel);
    if (sel != m_selectedSet) {
        m_selectedSet = sel;
        // Changing the set resets the song roller to its first entry.
        m_songScrollCurrent = 0.f;
        m_songScrollTarget  = 0.f;
        m_songScrollVel     = 0.f;
        m_selectedSong = m_sets[sel].songs.empty() ? -1 : 0;
    }

    drawWheelBand(dl, origin, width, centerY, rowPitch, IM_COL32(96, 132, 232, 255));

    // Far rows first so the centered row paints on top.
    std::vector<int> order;
    for (int i = 0; i < count; ++i) {
        if (std::fabs((float)i - m_setScrollCurrent) <= 5.0f) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return std::fabs((float)a - m_setScrollCurrent) >
               std::fabs((float)b - m_setScrollCurrent);
    });

    ImFont* font = ImGui::GetFont();
    float baseFont = ImGui::GetFontSize();
    for (int i : order) {
        CylSample cs = cylSample((float)i - m_setScrollCurrent, centerY, radius, angleStep);
        if (!cs.vis) continue;
        bool isSel = (i == sel);
        drawRollerRow(dl, font, baseFont, origin, width, rowPitch, cs,
                      getCoverDesc(m_sets[i].coverImage), m_sets[i].name.c_str(),
                      isSel, IM_COL32(150, 180, 255, 255));
    }
}

void MusicSelectionView::renderSongWheel(ImVec2 origin, float width, float height, IPlayerEngine* engine) {
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
    float centerY = origin.y + height * 0.5f;

    float rowPitch  = std::clamp(height / 7.f, 46.f, 100.f);
    float angleStep = 0.34f;
    float radius    = rowPitch / std::sin(angleStep);

    int prevSel = (m_selectedSong < 0) ? 0 : m_selectedSong;
    int sel = tickWheelInput(engine, "##songwheel_area", origin, width, height,
                             count, rowPitch, angleStep, radius,
                             m_songScrollCurrent, m_songScrollTarget, m_songScrollVel,
                             DragWheel::Song, prevSel);
    m_selectedSong = sel;

    // Double-click the centered row to launch immediately (editor hook).
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        onSongCardDoubleClick(sel);

    drawWheelBand(dl, origin, width, centerY, rowPitch, IM_COL32(228, 96, 140, 255));

    std::vector<int> order;
    for (int i = 0; i < count; ++i)
        if (std::fabs((float)i - m_songScrollCurrent) <= 5.0f) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return std::fabs((float)a - m_songScrollCurrent) >
               std::fabs((float)b - m_songScrollCurrent);
    });

    ImFont* font = ImGui::GetFont();
    float baseFont = ImGui::GetFontSize();

    // Per-difficulty score + achievement for the centered row's badges.
    int diffScore = 0;
    const std::string* diffAch = nullptr;
    {
        auto& s = songs[std::clamp(sel, 0, count - 1)];
        switch (m_selectedDifficulty) {
            case Difficulty::Easy:   diffScore = s.scoreEasy;   diffAch = &s.achievementEasy;   break;
            case Difficulty::Medium: diffScore = s.scoreMedium; diffAch = &s.achievementMedium; break;
            case Difficulty::Hard:   diffScore = s.scoreHard;   diffAch = &s.achievementHard;   break;
        }
    }
    bool fcUnlocked = false, apUnlocked = false;
    if (diffAch && !diffAch->empty()) {
        std::string low = *diffAch;
        for (char& c : low) c = (char)std::tolower((unsigned char)c);
        if      (low == "ap") { fcUnlocked = apUnlocked = true; }
        else if (low == "fc") { fcUnlocked = true; }
    }

    VkDescriptorSet fcTex = m_fcImage.empty() ? VK_NULL_HANDLE : getCoverDesc(m_fcImage);
    VkDescriptorSet apTex = m_apImage.empty() ? VK_NULL_HANDLE : getCoverDesc(m_apImage);

    for (int i : order) {
        CylSample cs = cylSample((float)i - m_songScrollCurrent, centerY, radius, angleStep);
        if (!cs.vis) continue;
        bool isSel = (i == sel);
        float coverR = drawRollerRow(dl, font, baseFont, origin, width, rowPitch, cs,
                                     getCoverDesc(songs[i].coverImage),
                                     songs[i].name.c_str(), isSel,
                                     IM_COL32(255, 150, 195, 255));

        if (!isSel || cs.depth <= 0.85f) continue;

        // Centered row extras: score line + FC/AP rhombus badges.
        int ai = (int)(cs.alpha * 255.f);
        char scoreBuf[32];
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", diffScore);
        float scoreSz = baseFont * 0.80f;
        dl->AddText(font, scoreSz, ImVec2(coverR + width * 0.06f, cs.y + rowPitch * 0.22f),
                    IM_COL32(208, 212, 230, ai), scoreBuf);

        float rW   = std::min(rowPitch * 0.40f, width * 0.13f);
        float apCX = origin.x + width - width * 0.09f - rW * 0.5f;
        float fcCX = apCX - rW * 0.85f;
        auto rhombus = [&](float cx, VkDescriptorSet tex, bool unlocked, ImU32 fill) {
            float hw = rW * 0.5f;
            ImVec2 pN(cx, cs.y - hw), pE(cx + hw, cs.y), pS(cx, cs.y + hw), pW(cx - hw, cs.y);
            dl->AddQuadFilled(pN, pE, pS, pW,
                unlocked ? fill : IM_COL32(45, 48, 60, (int)(170 * cs.alpha)));
            if (tex)
                dl->AddImageQuad((ImTextureID)(uint64_t)tex, pN, pE, pS, pW,
                    ImVec2(0.5f, 0), ImVec2(1, 0.5f), ImVec2(0.5f, 1), ImVec2(0, 0.5f),
                    IM_COL32(255, 255, 255, unlocked ? (int)(230 * cs.alpha) : (int)(55 * cs.alpha)));
            dl->AddQuad(pN, pE, pS, pW,
                unlocked ? IM_COL32(255, 255, 255, (int)(200 * cs.alpha))
                         : IM_COL32(150, 150, 170, (int)(120 * cs.alpha)), 1.4f);
        };
        rhombus(fcCX, fcTex, fcUnlocked, IM_COL32(40, 110, 150, (int)(180 * cs.alpha)));
        rhombus(apCX, apTex, apUnlocked, IM_COL32(150, 120, 40, (int)(180 * cs.alpha)));
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

void MusicSelectionView::renderDifficultyButtons(ImVec2 origin, float /*width*/, IPlayerEngine* engine) {
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
            if (diffs[i].diff != m_selectedDifficulty)
                playWheelSfx(engine, m_difficultySfx, DefaultSfx::Role::UiTap);
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
            playWheelSfx(engine, m_wheelClickSfx, DefaultSfx::Role::UiConfirm); // confirm SFX on START
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
        if (engine) engine->audio().playCachedSfx(
            DefaultSfx::cacheKeyForRole(DefaultSfx::Role::UiToggle));
    }
}
