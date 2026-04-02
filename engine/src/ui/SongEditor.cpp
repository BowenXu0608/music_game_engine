#include "SongEditor.h"
#include "engine/Engine.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/BufferManager.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <iostream>
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
    if (m_projectPath.empty()) return;
    fs::path absProject = fs::absolute(fs::path(m_projectPath));
    int copied = 0;

    for (const auto& src : srcPaths) {
        std::string ext = fs::path(src).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        fs::path destDir;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif")
            destDir = absProject / "assets" / "textures";
        else if (ext == ".mp3" || ext == ".ogg" || ext == ".wav" || ext == ".flac" || ext == ".aac")
            destDir = absProject / "assets" / "audio";
        else if (ext == ".json" || ext == ".chart" || ext == ".ucf")
            destDir = absProject / "assets" / "charts";
        else
            destDir = absProject / "assets";

        try {
            fs::create_directories(destDir);
            fs::path dest = destDir / fs::path(src).filename();
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
            ++copied;
        } catch (...) {}
    }

    if (copied > 0) {
        m_statusMsg   = "Imported " + std::to_string(copied) + " file(s)";
        m_statusTimer = 3.f;
    }
    m_assetsScanned = false;
}

// ── Main render ──────────────────────────────────────────────────────────────

void SongEditor::render(Engine* engine) {
    if (m_statusTimer > 0.f) m_statusTimer -= ImGui::GetIO().DeltaTime;

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

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float splitterThick = 4.f;
    const float navH = 36.f;
    float totalH  = contentSize.y - navH - 8.f;
    float propsH  = totalH * m_vSplit - splitterThick * 0.5f;
    float assetsH = totalH * (1.f - m_vSplit) - splitterThick * 0.5f;

    // ── Top: Properties panel ────────────────────────────────────────────────
    ImGui::BeginChild("SEProperties", ImVec2(contentSize.x, propsH), true);
    renderProperties();
    ImGui::EndChild();

    // Horizontal splitter
    ImGui::InvisibleButton("se_hsplit", ImVec2(contentSize.x, splitterThick));
    if (ImGui::IsItemActive()) {
        m_vSplit += ImGui::GetIO().MouseDelta.y / totalH;
        m_vSplit = std::clamp(m_vSplit, 0.3f, 0.85f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    // ── Bottom: Assets panel ─────────────────────────────────────────────────
    ImGui::BeginChild("SEAssets", ImVec2(contentSize.x, assetsH), true);
    renderAssets();
    ImGui::EndChild();

    // ── Nav bar ──────────────────────────────────────────────────────────────
    if (ImGui::Button("< Back to Music Selection")) {
        if (engine) engine->switchLayer(EditorLayer::MusicSelection);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (engine) {
            engine->musicSelectionEditor().save();
            m_statusMsg   = "Saved!";
            m_statusTimer = 2.f;
        }
    }
    if (m_statusTimer > 0.f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "%s", m_statusMsg.c_str());
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
    }

    ImGui::Spacing();

    // ── Charts ───────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Charts", ImGuiTreeNodeFlags_DefaultOpen)) {
        const wchar_t* chartFilter = L"Charts\0*.json;*.chart;*.ucf\0All Files\0*.*\0";

        // Easy
        char easyBuf[256];
        strncpy(easyBuf, m_song->chartEasy.c_str(), 255); easyBuf[255] = '\0';
        if (ImGui::InputText("Easy Chart", easyBuf, 256))
            m_song->chartEasy = easyBuf;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                m_song->chartEasy = std::string(static_cast<const char*>(payload->Data), payload->DataSize - 1);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse##easy")) {
            std::string path = browseFile(chartFilter, "charts");
            if (!path.empty()) m_song->chartEasy = path;
        }

        // Medium
        char medBuf[256];
        strncpy(medBuf, m_song->chartMedium.c_str(), 255); medBuf[255] = '\0';
        if (ImGui::InputText("Medium Chart", medBuf, 256))
            m_song->chartMedium = medBuf;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                m_song->chartMedium = std::string(static_cast<const char*>(payload->Data), payload->DataSize - 1);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse##medium")) {
            std::string path = browseFile(chartFilter, "charts");
            if (!path.empty()) m_song->chartMedium = path;
        }

        // Hard
        char hardBuf[256];
        strncpy(hardBuf, m_song->chartHard.c_str(), 255); hardBuf[255] = '\0';
        if (ImGui::InputText("Hard Chart", hardBuf, 256))
            m_song->chartHard = hardBuf;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                m_song->chartHard = std::string(static_cast<const char*>(payload->Data), payload->DataSize - 1);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::Button("Browse##hard")) {
            std::string path = browseFile(chartFilter, "charts");
            if (!path.empty()) m_song->chartHard = path;
        }
    }

    ImGui::Spacing();

    // ── Score & Achievement ──────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Score & Achievement", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputInt("Score", &m_song->score);
        m_song->score = std::clamp(m_song->score, 0, 1000000);

        char achBuf[32];
        strncpy(achBuf, m_song->achievement.c_str(), 31); achBuf[31] = '\0';
        if (ImGui::InputText("Achievement", achBuf, 32))
            m_song->achievement = achBuf;
        ImGui::TextDisabled("Examples: FC (Full Combo), AP (All Perfect)");
    }
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
        ofn.lpstrFilter  = L"Images\0*.png;*.jpg;*.jpeg\0"
                           L"Audio\0*.mp3;*.ogg;*.wav;*.flac;*.aac\0"
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
