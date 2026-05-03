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

// Returns a copy of `s` re-encoded as valid UTF-8. ASCII passes through
// unchanged. On Windows, any byte sequence that is not already valid UTF-8 is
// re-decoded from CP_ACP (the system code page, e.g. CP936 for zh-CN) and
// converted to UTF-8 so song names / filenames typed into the editor on a
// Chinese-locale machine round-trip correctly. If the conversion fails the
// original bytes are kept (we'd rather hand nlohmann::json a still-invalid
// string and surface the error than silently corrupt user data).
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

// ── Vulkan lifecycle ─────────────────────────────────────────────────────────

void MusicSelectionEditor::initVulkan(VulkanContext& ctx, BufferManager& bufMgr,
                                      ImGuiLayer& imgui, GLFWwindow* window) {
    MusicSelectionView::initVulkan(ctx, bufMgr, &imgui);
    m_window = window;
}

void MusicSelectionEditor::shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr) {
    MusicSelectionView::shutdownVulkan(ctx, bufMgr);
    clearThumbnails();
}

void MusicSelectionEditor::onSongCardDoubleClick(int /*songIdx*/) {
    if (m_engine) {
        SongInfo* s = getSelectedSong();
        if (s) {
            m_engine->songEditor().setSong(s, m_projectPath);
            m_engine->switchLayer(EditorLayer::SongEditor);
        }
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

void MusicSelectionEditor::load(const std::string& projectPath) {
    clearThumbnails();
    MusicSelectionView::load(projectPath);
}

// ── Main render ──────────────────────────────────────────────────────────────

void MusicSelectionEditor::render(Engine* engine) {
    m_engine = engine;
    if (m_statusTimer > 0.f) m_statusTimer -= ImGui::GetIO().DeltaTime;

    float dt = ImGui::GetIO().DeltaTime;
    update(dt, engine);

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
        renderGamePreview(origin, displaySz, engine);

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

    {
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(ds.x, ds.y));
    }
    ImGui::Begin("Music Selection", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Scan assets once per project
    if (!m_assetsScanned && !m_projectPath.empty()) {
        m_assets        = scanAssets(m_projectPath);
        m_assetsScanned = true;
    }

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float splitterThick = 4.f;
    const float navH = 36.f;
    // Copilot sidebar occupies only the top body column so Assets strip
    // below can span the full width (mirrors SongEditor layout).
    const float copilotW = engine ? engine->songEditor().copilotOverlayWidth() : 0.f;
    const float bodyW    = std::max(200.f, contentSize.x - copilotW);
    // Pinned bottom Assets strip - same pattern as SongEditor.
    const float assetsHeaderH = ImGui::GetFrameHeightWithSpacing();
    float assetsH = m_assetsBarOpen
        ? std::clamp(m_assetsBarH, 80.f, contentSize.y * 0.5f)
        : assetsHeaderH;
    float totalH   = std::max(100.f, contentSize.y - navH - 8.f - assetsH - 4.f);
    float topH     = totalH;
    float previewW = bodyW * m_hSplit - splitterThick * 0.5f;
    float hierW    = bodyW * (1.f - m_hSplit) - splitterThick * 0.5f;

    // Tell the overlay how many pixels to leave free at the bottom so the
    // Copilot sidebar stops above the Assets strip + nav bar. Computed from
    // stable inputs (no GetCursorScreenPos dependency) so the value is
    // identical regardless of which earlier layout work has run, and so the
    // overlay does not snap to a different height when transitioning to or
    // from this page.
    if (engine) {
        engine->songEditor().setOverlayBottomReserve(navH + 8.f + assetsH + 4.f);
    }

    // ── Top row: Preview | vsplitter | Hierarchy ─────────────────────────────
    ImGui::BeginChild("MSPreview", ImVec2(previewW, topH), true);
    renderPreview(previewW, topH);
    ImGui::EndChild();

    ImGui::SameLine();

    // Vertical splitter
    ImGui::InvisibleButton("ms_vsplit", ImVec2(splitterThick, topH));
    if (ImGui::IsItemActive()) {
        m_hSplit += ImGui::GetIO().MouseDelta.x / std::max(1.f, bodyW);
        m_hSplit = std::clamp(m_hSplit, 0.4f, 0.85f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImGui::SameLine();

    ImGui::BeginChild("MSHierarchy", ImVec2(hierW, topH), true);
    renderHierarchy(hierW, topH);
    ImGui::EndChild();

    // ── Bottom: pinned Assets strip (always docked, collapsible) ────────────
    ImGui::BeginChild("MSAssets", ImVec2(contentSize.x, assetsH), true);
    {
        ImGui::SetNextItemOpen(m_assetsBarOpen, ImGuiCond_Always);
        bool open = ImGui::CollapsingHeader("Assets##msbottom");
        m_assetsBarOpen = open;
        if (open)
            renderAssets();
    }
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
    renderPlayButton(ImVec2(centerX, playY), centerW, m_engine);

    // Preview toggle is handled inside renderSongWheel — when on, every
    // song card renders both rhombus slots as "unlocked" so the author can
    // judge how the badge images fill the real slots.

    dl->PopClipRect();
}

// ── renderGamePreview ────────────────────────────────────────────────────────

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
                    !m_assets.videos.empty() || !m_assets.audios.empty() ||
                    !m_assets.materials.empty();
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
                ImGui::SetTooltip("%s", shortenForTooltip(name).c_str());
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
    if (!m_assets.videos.empty()) { ImGui::Text("Videos:"); drawThumbs(m_assets.videos); }

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
                ImGui::SetTooltip("%s", shortenForTooltip(name).c_str());
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
                    ImGui::TextUnformatted(shortenForTooltip(name).c_str());
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
                    ImGui::SetTooltip("%s", shortenForTooltip(name).c_str());
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
