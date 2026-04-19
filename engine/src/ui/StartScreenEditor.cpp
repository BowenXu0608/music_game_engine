#include "StartScreenEditor.h"
#include "engine/Engine.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/BufferManager.h"
#include "renderer/MaterialAssetLibrary.h"
#include "renderer/ShaderCompiler.h"
#include "editor/ShaderGenClient.h"
#include "editor/AIEditorConfig.h"
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <ole2.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

// ── AI shader-gen state (pimpl) ───────────────────────────────────────────────

struct ShaderGenUIState {
    ShaderGenClient client;
    AIEditorConfig  cfg;
    bool            cfgLoaded     = false;
    char            prompt[4096]  = {};
    int             maxAttempts   = 3;
    std::string     finalLog;        // attemptsLog after completion
    std::string     finalError;      // errorMessage after completion
    std::string     finalSpvPath;    // on success
    bool            callbackBound = false;
};

// Same resolution as the Copilot's aiConfigPath() in SongEditor.cpp. Kept
// local here rather than extracted to keep the editor headers lightweight.
static std::string shaderGenConfigPath() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) {
        fs::path dir = fs::path(appdata) / "MusicGameEngine";
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!ec) return (dir / "ai_editor_config.json").string();
    }
#endif
    return "ai_editor_config.json";
}

StartScreenEditor::StartScreenEditor()
    : m_shaderGen(std::make_unique<ShaderGenUIState>()) {}
StartScreenEditor::~StartScreenEditor() = default;

// ── Vulkan lifecycle ──────────────────────────────────────────────────────────

void StartScreenEditor::initVulkan(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui,
                                   GLFWwindow* window) {
    m_ctx    = &ctx;
    m_bufMgr = &bufMgr;
    m_imgui  = &imgui;
    m_window = window;
}

void StartScreenEditor::shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr) {
    unloadBackground(ctx, bufMgr);
    unloadLogoImage(ctx, bufMgr);
    clearThumbnails(ctx, bufMgr);
}

// ── asset loading helpers ─────────────────────────────────────────────────────

void StartScreenEditor::unloadBackground(VulkanContext& ctx, BufferManager& bufMgr) {
    if (m_gifPlayer.isLoaded()) m_gifPlayer.unload(ctx, bufMgr);
    if (m_bgTexture.image != VK_NULL_HANDLE) {
        vkDestroySampler(ctx.device(), m_bgTexture.sampler, nullptr);
        vkDestroyImageView(ctx.device(), m_bgTexture.view, nullptr);
        vmaDestroyImage(bufMgr.allocator(), m_bgTexture.image, m_bgTexture.allocation);
        m_bgTexture = {};
        m_bgDesc    = VK_NULL_HANDLE;
    }
}

void StartScreenEditor::unloadLogoImage(VulkanContext& ctx, BufferManager& bufMgr) {
    if (m_logoTexture.image != VK_NULL_HANDLE) {
        vkDestroySampler(ctx.device(), m_logoTexture.sampler, nullptr);
        vkDestroyImageView(ctx.device(), m_logoTexture.view, nullptr);
        vmaDestroyImage(bufMgr.allocator(), m_logoTexture.image, m_logoTexture.allocation);
        m_logoTexture = {};
        m_logoDesc    = VK_NULL_HANDLE;
    }
}

void StartScreenEditor::loadBackground(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui) {
    unloadBackground(ctx, bufMgr);
    if (m_bgFile[0] == '\0') { m_bgType = BgType::None; return; }

    std::string fullPath = m_projectPath + "/" + m_bgFile;
    std::string ext = fs::path(fullPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".gif") {
        if (m_gifPlayer.load(fullPath, ctx, bufMgr, imgui))
            m_bgType = BgType::Gif;
        else
            m_bgType = BgType::None;
    } else if (ext == ".mp4" || ext == ".webm") {
        m_bgType = BgType::Video;
    } else {
        // PNG / JPG
        try {
            TextureManager texMgr;
            texMgr.init(ctx, bufMgr);
            m_bgTexture = texMgr.loadFromFile(ctx, bufMgr, fullPath);
            m_bgDesc    = imgui.addTexture(m_bgTexture.view, m_bgTexture.sampler);
            m_bgType    = BgType::Image;
        } catch (...) {
            m_bgType = BgType::None;
        }
    }
}

void StartScreenEditor::loadLogoImage(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui) {
    unloadLogoImage(ctx, bufMgr);
    if (m_logoImageFile[0] == '\0') return;
    std::string fullPath = m_projectPath + "/" + m_logoImageFile;
    try {
        TextureManager texMgr;
        texMgr.init(ctx, bufMgr);
        m_logoTexture = texMgr.loadFromFile(ctx, bufMgr, fullPath);
        m_logoDesc    = imgui.addTexture(m_logoTexture.view, m_logoTexture.sampler);
    } catch (...) {}
}

// ── thumbnail cache ───────────────────────────────────────────────────────────

void StartScreenEditor::clearThumbnails(VulkanContext& ctx, BufferManager& bufMgr) {
    for (auto& [path, entry] : m_thumbCache) {
        if (entry.tex.image != VK_NULL_HANDLE) {
            vkDestroySampler(ctx.device(), entry.tex.sampler, nullptr);
            vkDestroyImageView(ctx.device(), entry.tex.view, nullptr);
            vmaDestroyImage(bufMgr.allocator(), entry.tex.image, entry.tex.allocation);
        }
    }
    m_thumbCache.clear();
}

VkDescriptorSet StartScreenEditor::getThumb(const std::string& relPath) {
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

// ── importFiles ───────────────────────────────────────────────────────────────

void StartScreenEditor::importFiles(const std::vector<std::string>& srcPaths) {
    int copied = importAssetsToProject(m_projectPath, srcPaths);
    if (copied > 0) {
        m_statusMsg   = "Imported " + std::to_string(copied) + " file(s)";
        m_statusTimer = 3.f;
    }
    m_assetsScanned = false;
}

// ── JSON load / save ──────────────────────────────────────────────────────────

void StartScreenEditor::load(const std::string& projectPath) {
    m_projectPath    = projectPath;
    m_assetsScanned  = false;
    if (m_ctx && m_bufMgr) clearThumbnails(*m_ctx, *m_bufMgr);

    std::string configPath = projectPath + "/start_screen.json";
    std::ifstream f(configPath);
    if (!f.is_open()) return;

    json j;
    try { j = json::parse(f); } catch (...) { return; }

    // Background — support both old flat string and new object format
    if (j.contains("background")) {
        if (j["background"].is_object()) {
            std::string file = j["background"].value("file", "");
            strncpy(m_bgFile, file.c_str(), 255); m_bgFile[255] = '\0';
        } else if (j["background"].is_string()) {
            // old format: "background": "path/to/file"
            std::string file = j["background"].get<std::string>();
            strncpy(m_bgFile, file.c_str(), 255); m_bgFile[255] = '\0';
        } else {
            m_bgFile[0] = '\0';
        }
    } else {
        m_bgFile[0] = '\0';
    }

    // Logo — support both old flat string and new object format
    if (j.contains("logo")) {
        if (j["logo"].is_object()) {
            auto& logo = j["logo"];
            std::string type = logo.value("type", "text");
            m_logoType = (type == "image") ? LogoType::Image : LogoType::Text;

            std::string text = logo.value("text", "");
            strncpy(m_logoText, text.c_str(), 255); m_logoText[255] = '\0';

            m_logoFontSize = logo.value("fontSize", 32.f);
            m_logoBold     = logo.value("bold", false);
            m_logoItalic   = logo.value("italic", false);

            if (logo.contains("color") && logo["color"].is_array() && logo["color"].size() == 4)
                for (int i = 0; i < 4; ++i) m_logoColor[i] = logo["color"][i].get<float>();

            std::string imgFile = logo.value("imageFile", "");
            strncpy(m_logoImageFile, imgFile.c_str(), 255); m_logoImageFile[255] = '\0';

            m_logoGlow       = logo.value("glow", false);
            m_logoGlowRadius = logo.value("glowRadius", 8.f);
            if (logo.contains("glowColor") && logo["glowColor"].is_array() && logo["glowColor"].size() == 4)
                for (int i = 0; i < 4; ++i) m_logoGlowColor[i] = logo["glowColor"][i].get<float>();

            if (logo.contains("position")) {
                m_logoPos[0] = logo["position"].value("x", 0.5f);
                m_logoPos[1] = logo["position"].value("y", 0.3f);
            }
            m_logoScale = logo.value("scale", 1.f);
        } else if (j["logo"].is_string()) {
            // old format: "logo": "MyGame"
            m_logoType = LogoType::Text;
            std::string text = j["logo"].get<std::string>();
            strncpy(m_logoText, text.c_str(), 255); m_logoText[255] = '\0';
            // read old-style position/scale fields if present
            if (j.contains("logoPosition")) {
                m_logoPos[0] = j["logoPosition"].value("x", 0.5f);
                m_logoPos[1] = j["logoPosition"].value("y", 0.3f);
            }
            m_logoScale = j.value("logoScale", 1.f);
        }
    }

    // Tap text
    std::string tap = j.value("tapText", "Tap to Start");
    strncpy(m_tapText, tap.c_str(), 255); m_tapText[255] = '\0';
    m_tapTextSize = j.value("tapTextSize", 24);
    if (j.contains("tapTextPosition")) {
        m_tapTextPos[0] = j["tapTextPosition"].value("x", 0.5f);
        m_tapTextPos[1] = j["tapTextPosition"].value("y", 0.8f);
    }

    // Transition
    if (j.contains("transition") && j["transition"].is_object()) {
        auto& tr = j["transition"];
        std::string eff = tr.value("effect", "fade");
        if      (eff == "slide_left") m_transition = TransitionEffect::SlideLeft;
        else if (eff == "zoom_in")    m_transition = TransitionEffect::ZoomIn;
        else if (eff == "ripple")     m_transition = TransitionEffect::Ripple;
        else if (eff == "custom")     m_transition = TransitionEffect::Custom;
        else                          m_transition = TransitionEffect::Fade;
        m_transitionDur = tr.value("duration", 0.5f);
        std::string cs = tr.value("customScript", "");
        strncpy(m_customScript, cs.c_str(), 255); m_customScript[255] = '\0';
    }

    // Audio
    if (j.contains("audio") && j["audio"].is_object()) {
        auto& a = j["audio"];
        std::string bgm = a.value("bgMusic", "");
        strncpy(m_bgMusic, bgm.c_str(), 255); m_bgMusic[255] = '\0';
        m_bgMusicVolume = a.value("bgMusicVolume", 1.0f);
        m_bgMusicLoop   = a.value("bgMusicLoop", true);
        std::string sfx = a.value("tapSfx", "");
        strncpy(m_tapSfx, sfx.c_str(), 255); m_tapSfx[255] = '\0';
        m_tapSfxVolume  = a.value("tapSfxVolume", 1.0f);
    }

    m_loaded = true;

    // Reload textures if Vulkan is already initialised (e.g. switching projects)
    if (m_ctx && m_bufMgr && m_imgui) {
        loadBackground(*m_ctx, *m_bufMgr, *m_imgui);
        if (m_logoType == LogoType::Image)
            loadLogoImage(*m_ctx, *m_bufMgr, *m_imgui);
    }
}

void StartScreenEditor::save() {
    if (m_projectPath.empty()) return;

    json j;

    // Background
    j["background"]["file"] = m_bgFile;
    switch (m_bgType) {
        case BgType::Image: j["background"]["type"] = "image"; break;
        case BgType::Gif:   j["background"]["type"] = "gif";   break;
        case BgType::Video: j["background"]["type"] = "video"; break;
        default:            j["background"]["type"] = "none";  break;
    }

    // Logo
    j["logo"]["type"]      = (m_logoType == LogoType::Image) ? "image" : "text";
    j["logo"]["text"]      = m_logoText;
    j["logo"]["fontSize"]  = m_logoFontSize;
    j["logo"]["bold"]      = m_logoBold;
    j["logo"]["italic"]    = m_logoItalic;
    j["logo"]["color"]     = {m_logoColor[0], m_logoColor[1], m_logoColor[2], m_logoColor[3]};
    j["logo"]["imageFile"] = m_logoImageFile;
    j["logo"]["glow"]      = m_logoGlow;
    j["logo"]["glowRadius"]= m_logoGlowRadius;
    j["logo"]["glowColor"] = {m_logoGlowColor[0], m_logoGlowColor[1], m_logoGlowColor[2], m_logoGlowColor[3]};
    j["logo"]["position"]  = {{"x", m_logoPos[0]}, {"y", m_logoPos[1]}};
    j["logo"]["scale"]     = m_logoScale;

    // Tap text
    j["tapText"]                    = m_tapText;
    j["tapTextPosition"]["x"]       = m_tapTextPos[0];
    j["tapTextPosition"]["y"]       = m_tapTextPos[1];
    j["tapTextSize"]                = m_tapTextSize;

    // Transition
    static const char* effectNames[] = {"fade","slide_left","zoom_in","ripple","custom"};
    j["transition"]["effect"]       = effectNames[static_cast<int>(m_transition)];
    j["transition"]["duration"]     = m_transitionDur;
    j["transition"]["customScript"] = m_customScript;

    // Audio
    j["audio"]["bgMusic"]       = m_bgMusic;
    j["audio"]["bgMusicVolume"] = m_bgMusicVolume;
    j["audio"]["bgMusicLoop"]   = m_bgMusicLoop;
    j["audio"]["tapSfx"]        = m_tapSfx;
    j["audio"]["tapSfxVolume"]  = m_tapSfxVolume;

    std::ofstream out(m_projectPath + "/start_screen.json");
    if (out.is_open()) out << j.dump(2);
}

// ── render ────────────────────────────────────────────────────────────────────

void StartScreenEditor::render(Engine* engine) {
    m_engine = engine;
    // Lazy-load assets on first render after load()
    if (m_loaded && m_ctx && m_bufMgr && m_imgui) {
        if (m_bgType == BgType::None && m_bgFile[0] != '\0')
            loadBackground(*m_ctx, *m_bufMgr, *m_imgui);
        if (m_logoType == LogoType::Image && m_logoDesc == VK_NULL_HANDLE && m_logoImageFile[0] != '\0')
            loadLogoImage(*m_ctx, *m_bufMgr, *m_imgui);
    }

    // Scan assets once per project
    if (!m_assetsScanned && !m_projectPath.empty()) {
        m_assets        = scanAssets(m_projectPath);
        m_assetsScanned = true;
    }

    // Tick status message
    if (m_statusTimer > 0.f) m_statusTimer -= ImGui::GetIO().DeltaTime;

    // ── Test mode: full-screen game start screen ──────────────────────────────
    if (engine && engine->isTestMode()) {
        ImVec2 displaySz = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(displaySz);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##test_start", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        renderGamePreview(origin, displaySz);
        ImGui::Dummy(displaySz);

        // Transition overlay (fade to black)
        if (engine->isTestTransitioning()) {
            float t = engine->testTransProgress();
            float eased = t * t * (3.f - 2.f * t);
            int alpha = (int)(255 * eased);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(0, 0), displaySz, IM_COL32(0, 0, 0, alpha));
        }

        // Click anywhere to advance to Music Selection (with transition)
        if (!engine->isTestTransitioning() && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            engine->musicSelectionEditor().load(m_projectPath);
            engine->testTransitionTo(EditorLayer::MusicSelection);
        }

        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Start Screen Editor", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    const float splitterThick = 4.f;
    const float navH  = 36.f;
    float totalH  = contentSize.y - navH - 8.f;
    float topH    = totalH * m_vSplit - splitterThick * 0.5f;
    float assetsH = totalH * (1.f - m_vSplit) - splitterThick * 0.5f;
    float previewW = contentSize.x * m_hSplit - splitterThick * 0.5f;
    float propsW   = contentSize.x * (1.f - m_hSplit) - splitterThick * 0.5f;

    // ── Top row: Preview | vsplitter | Properties ─────────────────────────────
    ImGui::BeginChild("Preview", ImVec2(previewW, topH), true);
    renderPreview();
    ImGui::EndChild();

    ImGui::SameLine();

    // Vertical splitter
    ImGui::InvisibleButton("vsplit", ImVec2(splitterThick, topH));
    if (ImGui::IsItemActive()) {
        m_hSplit += ImGui::GetIO().MouseDelta.x / contentSize.x;
        m_hSplit = std::clamp(m_hSplit, 0.2f, 0.8f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    ImGui::SameLine();

    ImGui::BeginChild("Properties", ImVec2(propsW, topH), true);
    if (ImGui::BeginTabBar("start_props_tabs")) {
        if (ImGui::BeginTabItem("Start Screen")) {
            renderProperties();
            ImGui::EndTabItem();
        }
        // Force-open the Materials tab when a .mat tile in the assets panel
        // is clicked. Reset the flag after the first pass so subsequent
        // clicks on other tabs (Start Screen) stick.
        ImGuiTabItemFlags matFlags = ImGuiTabItemFlags_None;
        if (m_materialsTabRequested) {
            matFlags = ImGuiTabItemFlags_SetSelected;
            m_materialsTabRequested = false;
        }
        if (ImGui::BeginTabItem("Materials", nullptr, matFlags)) {
            renderMaterials(engine);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // Horizontal splitter
    ImGui::InvisibleButton("hsplit", ImVec2(contentSize.x, splitterThick));
    if (ImGui::IsItemActive()) {
        m_vSplit += ImGui::GetIO().MouseDelta.y / totalH;
        m_vSplit = std::clamp(m_vSplit, 0.3f, 0.9f);
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    // ── Bottom strip: Assets ──────────────────────────────────────────────────
    ImGui::BeginChild("Assets", ImVec2(contentSize.x, assetsH), true);
    renderAssets();
    ImGui::EndChild();

    // ── Nav bar ───────────────────────────────────────────────────────────────
    if (ImGui::Button("< Back")) {
        if (engine) engine->switchLayer(EditorLayer::ProjectHub);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        save();
        m_statusMsg   = "Saved!";
        m_statusTimer = 2.f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        load(m_projectPath);
        m_statusMsg   = "Loaded!";
        m_statusTimer = 2.f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        m_logoType = LogoType::Text;
        m_logoText[0] = '\0';
        m_logoColor[0] = m_logoColor[1] = m_logoColor[2] = m_logoColor[3] = 1.f;
        m_logoFontSize = 32.f;
        m_logoBold = m_logoItalic = false;
        m_logoPos[0] = 0.5f; m_logoPos[1] = 0.3f;
        m_logoScale = 1.f;
        m_logoGlow = false;
        strcpy(m_tapText, "Tap to Start");
        m_tapTextPos[0] = 0.5f; m_tapTextPos[1] = 0.8f;
        m_tapTextSize = 24;
        m_transition = TransitionEffect::Fade;
        m_transitionDur = 0.5f;
    }
    if (m_statusTimer > 0.f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), "%s", m_statusMsg.c_str());
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(contentSize.x - 200.f);
    if (ImGui::Button("Next: Music Selection >")) {
        if (engine) {
            engine->musicSelectionEditor().load(m_projectPath);
            engine->switchLayer(EditorLayer::MusicSelection);
        }
    }

    ImGui::End();
}

// ── renderPreview ─────────────────────────────────────────────────────────────

void StartScreenEditor::renderPreview() {

    ImVec2 previewSize = ImGui::GetContentRegionAvail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float pw = previewSize.x, ph = previewSize.y;

    // Background
    switch (m_bgType) {
        case BgType::Image:
            if (m_bgDesc)
                dl->AddImage((ImTextureID)(uint64_t)m_bgDesc, p, ImVec2(p.x + pw, p.y + ph));
            else
                dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(40, 40, 50, 255));
            break;
        case BgType::Gif:
            if (m_gifPlayer.isLoaded()) {
                m_gifPlayer.update(ImGui::GetIO().DeltaTime);
                VkDescriptorSet frame = m_gifPlayer.currentFrame();
                if (frame) dl->AddImage((ImTextureID)(uint64_t)frame, p, ImVec2(p.x + pw, p.y + ph));
            } else {
                dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(40, 40, 50, 255));
            }
            break;
        case BgType::Video: {
            dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(20, 20, 30, 255));
            std::string label = std::string("Video: ") + fs::path(m_bgFile).filename().string();
            ImVec2 sz = ImGui::CalcTextSize(label.c_str());
            dl->AddText(ImVec2(p.x + pw * 0.5f - sz.x * 0.5f, p.y + ph * 0.5f - sz.y * 0.5f),
                        IM_COL32(180, 180, 180, 200), label.c_str());
            break;
        }
        default:
            dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(40, 40, 50, 255));
            break;
    }

    // Logo
    float logoX = p.x + pw * m_logoPos[0];
    float logoY = p.y + ph * m_logoPos[1];

    if (m_logoType == LogoType::Image && m_logoDesc) {
        float iw = m_logoTexture.width  * m_logoScale;
        float ih = m_logoTexture.height * m_logoScale;
        ImVec2 tl(logoX - iw * 0.5f, logoY - ih * 0.5f);
        ImVec2 br(logoX + iw * 0.5f, logoY + ih * 0.5f);
        if (m_logoGlow) {
            ImU32 gc = IM_COL32(
                (int)(m_logoGlowColor[0]*255), (int)(m_logoGlowColor[1]*255),
                (int)(m_logoGlowColor[2]*255), (int)(m_logoGlowColor[3]*255));
            float r = m_logoGlowRadius;
            dl->AddRectFilled(ImVec2(tl.x-r, tl.y-r), ImVec2(br.x+r, br.y+r), gc, r);
        }
        dl->AddImage((ImTextureID)(uint64_t)m_logoDesc, tl, br);
    } else if (m_logoType == LogoType::Text && m_logoText[0] != '\0') {
        // Use the Roboto font loaded at the closest size for smooth rendering
        ImFont* logoFont = m_imgui ? m_imgui->getLogoFont(m_logoFontSize) : ImGui::GetFont();
        float   fontSize = m_logoFontSize * m_logoScale;
        ImU32 col = IM_COL32(
            (int)(m_logoColor[0]*255), (int)(m_logoColor[1]*255),
            (int)(m_logoColor[2]*255), (int)(m_logoColor[3]*255));
        ImVec2 textSz = logoFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, m_logoText);
        ImVec2 textPos(logoX - textSz.x * 0.5f, logoY - textSz.y * 0.5f);
        if (m_logoGlow) {
            ImU32 gc = IM_COL32(
                (int)(m_logoGlowColor[0]*255), (int)(m_logoGlowColor[1]*255),
                (int)(m_logoGlowColor[2]*255), (int)(m_logoGlowColor[3]*255));
            float r = m_logoGlowRadius;
            static const float offsets[][2] = {
                {-r,0},{r,0},{0,-r},{0,r},{-r,-r},{r,-r},{-r,r},{r,r}
            };
            for (auto& o : offsets)
                dl->AddText(logoFont, fontSize,
                            ImVec2(textPos.x+o[0], textPos.y+o[1]), gc, m_logoText);
        }
        // Fake bold: draw twice with 1px horizontal offset
        if (m_logoBold)
            dl->AddText(logoFont, fontSize, ImVec2(textPos.x + 1.f, textPos.y), col, m_logoText);
        dl->AddText(logoFont, fontSize, textPos, col, m_logoText);
    } else {
        float logoSize = 100.f * m_logoScale;
        dl->AddRect(ImVec2(logoX - logoSize*0.5f, logoY - logoSize*0.5f),
                    ImVec2(logoX + logoSize*0.5f, logoY + logoSize*0.5f),
                    IM_COL32(120,120,120,200), 0, 0, 1.5f);
        ImVec2 hint = ImGui::CalcTextSize("Logo");
        dl->AddText(ImVec2(logoX - hint.x*0.5f, logoY - hint.y*0.5f),
                    IM_COL32(120,120,120,200), "Logo");
    }

    // Tap text
    float textX = p.x + pw * m_tapTextPos[0];
    float textY = p.y + ph * m_tapTextPos[1];
    ImFont* tapFont = m_imgui ? m_imgui->getLogoFont(m_tapTextSize) : ImGui::GetFont();
    float   tapFontSize = static_cast<float>(m_tapTextSize);
    ImVec2 tapSz = tapFont->CalcTextSizeA(tapFontSize, FLT_MAX, 0.f, m_tapText);
    dl->AddText(tapFont, tapFontSize,
                ImVec2(textX - tapSz.x * 0.5f, textY - tapSz.y * 0.5f),
                IM_COL32(255, 255, 255, 255), m_tapText);
}

// ── renderGamePreview ─────────────────────────────────────────────────────────

void StartScreenEditor::renderGamePreview(ImVec2 p, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pw = size.x, ph = size.y;

    // Background
    switch (m_bgType) {
        case BgType::Image:
            if (m_bgDesc)
                dl->AddImage((ImTextureID)(uint64_t)m_bgDesc, p, ImVec2(p.x + pw, p.y + ph));
            else
                dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(40, 40, 50, 255));
            break;
        case BgType::Gif:
            if (m_gifPlayer.isLoaded()) {
                m_gifPlayer.update(ImGui::GetIO().DeltaTime);
                VkDescriptorSet frame = m_gifPlayer.currentFrame();
                if (frame) dl->AddImage((ImTextureID)(uint64_t)frame, p, ImVec2(p.x + pw, p.y + ph));
            } else {
                dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(40, 40, 50, 255));
            }
            break;
        default:
            dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(40, 40, 50, 255));
            break;
    }

    // Logo
    float logoX = p.x + pw * m_logoPos[0];
    float logoY = p.y + ph * m_logoPos[1];

    if (m_logoType == LogoType::Image && m_logoDesc) {
        float iw = m_logoTexture.width  * m_logoScale;
        float ih = m_logoTexture.height * m_logoScale;
        ImVec2 tl(logoX - iw * 0.5f, logoY - ih * 0.5f);
        ImVec2 br(logoX + iw * 0.5f, logoY + ih * 0.5f);
        if (m_logoGlow) {
            ImU32 gc = IM_COL32((int)(m_logoGlowColor[0]*255), (int)(m_logoGlowColor[1]*255),
                                (int)(m_logoGlowColor[2]*255), (int)(m_logoGlowColor[3]*255));
            dl->AddRectFilled(ImVec2(tl.x-m_logoGlowRadius, tl.y-m_logoGlowRadius),
                              ImVec2(br.x+m_logoGlowRadius, br.y+m_logoGlowRadius), gc, m_logoGlowRadius);
        }
        dl->AddImage((ImTextureID)(uint64_t)m_logoDesc, tl, br);
    } else if (m_logoType == LogoType::Text && m_logoText[0] != '\0') {
        ImFont* logoFont = m_imgui ? m_imgui->getLogoFont(m_logoFontSize) : ImGui::GetFont();
        float fontSize = m_logoFontSize * m_logoScale;
        ImU32 col = IM_COL32((int)(m_logoColor[0]*255), (int)(m_logoColor[1]*255),
                             (int)(m_logoColor[2]*255), (int)(m_logoColor[3]*255));
        ImVec2 textSz = logoFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, m_logoText);
        ImVec2 textPos(logoX - textSz.x * 0.5f, logoY - textSz.y * 0.5f);
        if (m_logoGlow) {
            ImU32 gc = IM_COL32((int)(m_logoGlowColor[0]*255), (int)(m_logoGlowColor[1]*255),
                                (int)(m_logoGlowColor[2]*255), (int)(m_logoGlowColor[3]*255));
            float r = m_logoGlowRadius;
            static const float offsets[][2] = {{-r,0},{r,0},{0,-r},{0,r},{-r,-r},{r,-r},{-r,r},{r,r}};
            for (auto& o : offsets)
                dl->AddText(logoFont, fontSize, ImVec2(textPos.x+o[0], textPos.y+o[1]), gc, m_logoText);
        }
        if (m_logoBold)
            dl->AddText(logoFont, fontSize, ImVec2(textPos.x + 1.f, textPos.y), col, m_logoText);
        dl->AddText(logoFont, fontSize, textPos, col, m_logoText);
    }

    // Tap text
    float textX = p.x + pw * m_tapTextPos[0];
    float textY = p.y + ph * m_tapTextPos[1];
    ImFont* tapFont = m_imgui ? m_imgui->getLogoFont(m_tapTextSize) : ImGui::GetFont();
    float tapFontSize = static_cast<float>(m_tapTextSize);
    ImVec2 tapSz = tapFont->CalcTextSizeA(tapFontSize, FLT_MAX, 0.f, m_tapText);
    dl->AddText(tapFont, tapFontSize,
                ImVec2(textX - tapSz.x * 0.5f, textY - tapSz.y * 0.5f),
                IM_COL32(255, 255, 255, 255), m_tapText);
}

// ── renderProperties ──────────────────────────────────────────────────────────

void StartScreenEditor::renderProperties() {
    ImGui::Text("Properties");
    ImGui::Separator();

    // ── Background ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Drop zone — drag a thumbnail from the asset panel below onto this area
        const float zoneW = ImGui::GetContentRegionAvail().x - 74.f;
        const float zoneH = 54.f;
        ImVec2 zonePos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##bgzone", ImVec2(zoneW, zoneH));
        ImDrawList* dlBg = ImGui::GetWindowDrawList();
        ImU32 borderCol = ImGui::IsItemHovered()
            ? IM_COL32(100, 160, 255, 255) : IM_COL32(100, 100, 120, 180);
        if (m_bgType == BgType::Image && m_bgDesc) {
            dlBg->AddImage((ImTextureID)(uint64_t)m_bgDesc, zonePos,
                           ImVec2(zonePos.x + zoneW, zonePos.y + zoneH));
        } else if (m_bgFile[0] != '\0') {
            dlBg->AddRectFilled(zonePos, ImVec2(zonePos.x + zoneW, zonePos.y + zoneH),
                                IM_COL32(30, 30, 45, 255), 4.f);
            std::string fname = fs::path(m_bgFile).filename().string();
            ImVec2 tsz = ImGui::CalcTextSize(fname.c_str());
            dlBg->AddText(ImVec2(zonePos.x + zoneW * 0.5f - tsz.x * 0.5f,
                                 zonePos.y + zoneH * 0.5f - tsz.y * 0.5f),
                          IM_COL32(200, 200, 200, 255), fname.c_str());
        } else {
            dlBg->AddRectFilled(zonePos, ImVec2(zonePos.x + zoneW, zonePos.y + zoneH),
                                IM_COL32(30, 30, 45, 180), 4.f);
            const char* hint = "Drop background here";
            ImVec2 tsz = ImGui::CalcTextSize(hint);
            dlBg->AddText(ImVec2(zonePos.x + zoneW * 0.5f - tsz.x * 0.5f,
                                 zonePos.y + zoneH * 0.5f - tsz.y * 0.5f),
                          IM_COL32(120, 120, 140, 200), hint);
        }
        dlBg->AddRect(zonePos, ImVec2(zonePos.x + zoneW, zonePos.y + zoneH),
                      borderCol, 4.f, 0, 1.5f);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                std::string rel(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                strncpy(m_bgFile, rel.c_str(), 255); m_bgFile[255] = '\0';
                if (m_ctx) loadBackground(*m_ctx, *m_bufMgr, *m_imgui);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Button("Clear##bg")) {
            m_bgFile[0] = '\0';
            if (m_ctx) unloadBackground(*m_ctx, *m_bufMgr);
            m_bgType = BgType::None;
        }
        ImGui::EndGroup();
    }

    ImGui::Spacing();

    // ── Logo ──────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Logo", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* logoTypes[] = {"Text", "Image"};
        int lt = static_cast<int>(m_logoType);
        if (ImGui::Combo("Logo Type", &lt, logoTypes, 2))
            m_logoType = static_cast<LogoType>(lt);

        if (m_logoType == LogoType::Text) {
            ImGui::InputText("Text##logo", m_logoText, 256);
            ImGui::SliderFloat("Font Size", &m_logoFontSize, 12.f, 96.f);
            ImGui::ColorEdit4("Color##logo", m_logoColor);
            ImGui::Checkbox("Bold", &m_logoBold);
            ImGui::SameLine();
            ImGui::Checkbox("Italic", &m_logoItalic);
        } else {
            // Image logo — drag from asset panel
            const float lzoneW = ImGui::GetContentRegionAvail().x - 74.f;
            const float lzoneH = 54.f;
            ImVec2 lzonePos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##logozone", ImVec2(lzoneW, lzoneH));
            ImDrawList* dlLogo = ImGui::GetWindowDrawList();
            ImU32 lborderCol = ImGui::IsItemHovered()
                ? IM_COL32(100, 160, 255, 255) : IM_COL32(100, 100, 120, 180);
            if (m_logoDesc) {
                dlLogo->AddImage((ImTextureID)(uint64_t)m_logoDesc, lzonePos,
                                 ImVec2(lzonePos.x + lzoneW, lzonePos.y + lzoneH));
            } else if (m_logoImageFile[0] != '\0') {
                dlLogo->AddRectFilled(lzonePos, ImVec2(lzonePos.x + lzoneW, lzonePos.y + lzoneH),
                                     IM_COL32(30, 30, 45, 255), 4.f);
                std::string fname = fs::path(m_logoImageFile).filename().string();
                ImVec2 tsz = ImGui::CalcTextSize(fname.c_str());
                dlLogo->AddText(ImVec2(lzonePos.x + lzoneW * 0.5f - tsz.x * 0.5f,
                                      lzonePos.y + lzoneH * 0.5f - tsz.y * 0.5f),
                                IM_COL32(200, 200, 200, 255), fname.c_str());
            } else {
                dlLogo->AddRectFilled(lzonePos, ImVec2(lzonePos.x + lzoneW, lzonePos.y + lzoneH),
                                     IM_COL32(30, 30, 45, 180), 4.f);
                const char* hint = "Drop logo image here";
                ImVec2 tsz = ImGui::CalcTextSize(hint);
                dlLogo->AddText(ImVec2(lzonePos.x + lzoneW * 0.5f - tsz.x * 0.5f,
                                      lzonePos.y + lzoneH * 0.5f - tsz.y * 0.5f),
                                IM_COL32(120, 120, 140, 200), hint);
            }
            dlLogo->AddRect(lzonePos, ImVec2(lzonePos.x + lzoneW, lzonePos.y + lzoneH),
                            lborderCol, 4.f, 0, 1.5f);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    std::string rel(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                    strncpy(m_logoImageFile, rel.c_str(), 255); m_logoImageFile[255] = '\0';
                    if (m_ctx) loadLogoImage(*m_ctx, *m_bufMgr, *m_imgui);
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (ImGui::Button("Clear##logo")) {
                m_logoImageFile[0] = '\0';
                if (m_ctx) unloadLogoImage(*m_ctx, *m_bufMgr);
            }
            ImGui::EndGroup();
        }

        ImGui::Separator();
        ImGui::SliderFloat2("Position##logo", m_logoPos, 0.f, 1.f);
        ImGui::SliderFloat("Scale##logo", &m_logoScale, 0.1f, 10.f);
        ImGui::Separator();
        ImGui::Checkbox("Glow / Outline", &m_logoGlow);
        if (m_logoGlow) {
            ImGui::ColorEdit4("Glow Color", m_logoGlowColor);
            ImGui::SliderFloat("Glow Radius", &m_logoGlowRadius, 1.f, 32.f);
        }
    }

    ImGui::Spacing();

    // ── Tap Text ──────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Tap Text", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("Text##tap", m_tapText, 256);
        ImGui::SliderFloat2("Position##tap", m_tapTextPos, 0.f, 1.f);
        ImGui::SliderInt("Size##tap", &m_tapTextSize, 12, 120);
    }

    ImGui::Spacing();

    // ── Transition ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Transition Effect", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* effects[] = {"Fade to Black", "Slide Left", "Zoom In", "Ripple", "Custom"};
        int eff = static_cast<int>(m_transition);
        ImGui::Combo("Effect", &eff, effects, 5);
        m_transition = static_cast<TransitionEffect>(eff);
        ImGui::SliderFloat("Duration (s)", &m_transitionDur, 0.1f, 2.f);
        if (m_transition == TransitionEffect::Custom) {
            ImGui::InputText("Script Path", m_customScript, 256);
            ImGui::TextDisabled("Lua script receives: progress, tap_x, tap_y");
        }
    }

    ImGui::Spacing();

    // ── Audio ─────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
        // helper to draw a small audio drop zone
        auto audioZone = [&](const char* label, char* buf, float& vol, bool* loop) {
            ImGui::TextDisabled("%s", label);
            const float azW = ImGui::GetContentRegionAvail().x - 74.f;
            const float azH = 36.f;
            ImVec2 azPos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(label, ImVec2(azW, azH));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 border = ImGui::IsItemHovered()
                ? IM_COL32(100, 160, 255, 255) : IM_COL32(100, 100, 120, 180);
            dl->AddRectFilled(azPos, ImVec2(azPos.x + azW, azPos.y + azH),
                              IM_COL32(25, 30, 40, 220), 4.f);
            if (buf[0] != '\0') {
                std::string fname = fs::path(buf).filename().string();
                std::string display = "[BGM]  " + fname;
                ImVec2 adtsz = ImGui::CalcTextSize(display.c_str());
                dl->AddText(ImVec2(azPos.x + 8.f, azPos.y + azH * 0.5f - adtsz.y * 0.5f),
                            IM_COL32(180, 220, 255, 255), display.c_str());
            } else {
                const char* hint = "Drop audio file here";
                ImVec2 adtsz2 = ImGui::CalcTextSize(hint);
                dl->AddText(ImVec2(azPos.x + azW * 0.5f - adtsz2.x * 0.5f,
                                   azPos.y + azH * 0.5f - adtsz2.y * 0.5f),
                            IM_COL32(120, 120, 140, 200), hint);
            }
            dl->AddRect(azPos, ImVec2(azPos.x + azW, azPos.y + azH), border, 4.f, 0, 1.5f);
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    std::string rel(static_cast<const char*>(payload->Data), payload->DataSize - 1);
                    strncpy(buf, rel.c_str(), 255); buf[255] = '\0';
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::BeginGroup();
            std::string clearId = std::string("Clear##") + label;
            if (ImGui::Button(clearId.c_str())) buf[0] = '\0';
            ImGui::EndGroup();
            ImGui::SliderFloat((std::string("Volume##") + label).c_str(), &vol, 0.f, 1.f);
            if (loop) ImGui::Checkbox((std::string("Loop##") + label).c_str(), loop);
            ImGui::Spacing();
        };

        audioZone("Background Music", m_bgMusic, m_bgMusicVolume, &m_bgMusicLoop);
        audioZone("Tap Sound Effect", m_tapSfx,  m_tapSfxVolume,  nullptr);
    }
}

// ── renderMaterials ───────────────────────────────────────────────────────────

void StartScreenEditor::renderMaterials(Engine* engine) {
    if (!engine) {
        ImGui::TextDisabled("Engine unavailable");
        return;
    }
    MaterialAssetLibrary& lib = engine->materialLibrary();

    // ── Library list + toolbar ───────────────────────────────────────────────
    if (ImGui::Button("+ New Material")) {
        m_showNewMaterialDialog  = true;
        m_newMaterialNameBuf[0]  = '\0';
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%d in project)", (int)lib.all().size());

    // Create-new popup
    if (m_showNewMaterialDialog) {
        ImGui::OpenPopup("New Material");
        m_showNewMaterialDialog = false;
    }
    if (ImGui::BeginPopupModal("New Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", m_newMaterialNameBuf, sizeof(m_newMaterialNameBuf));
        std::string name = m_newMaterialNameBuf;
        bool valid = !name.empty() && !lib.get(name);
        if (!valid && !name.empty())
            ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "Name already exists");
        if (!valid) ImGui::BeginDisabled();
        if (ImGui::Button("Create", ImVec2(100, 0))) {
            MaterialAsset a;
            a.name = name;
            lib.upsert(a);
            m_selectedMaterial   = name;
            m_editingMaterial    = a;
            m_materialEditLoaded = true;
            m_materialCompileLog.clear();
            ImGui::CloseCurrentPopup();
        }
        if (!valid) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // Selectable list — kept tight so the edit panel below has room.
    float listH = ImGui::GetContentRegionAvail().y * 0.35f;
    ImGui::BeginChild("mat_list", ImVec2(0, listH), true);
    for (const auto& name : lib.allNames()) {
        bool selected = (name == m_selectedMaterial);
        if (ImGui::Selectable(name.c_str(), selected)) {
            m_selectedMaterial = name;
            if (const MaterialAsset* a = lib.get(name)) {
                m_editingMaterial    = *a;
                m_materialEditLoaded = true;
                m_materialCompileLog.clear();
            }
        }
    }
    ImGui::EndChild();

    // ── Edit panel for the selected material ────────────────────────────────
    if (m_selectedMaterial.empty() || !m_materialEditLoaded) {
        ImGui::TextDisabled("Select a material (or create one) to edit.");
        return;
    }
    if (!lib.get(m_selectedMaterial)) {
        // Underlying asset was deleted — clear local state.
        m_selectedMaterial.clear();
        m_materialEditLoaded = false;
        return;
    }

    ImGui::Text("Editing: %s", m_editingMaterial.name.c_str());
    ImGui::Separator();

    // ── Compatibility: which (mode, slot) this material applies to. ──────────
    // The SongEditor slot dropdowns filter by these fields. Leaving both
    // blank makes the material "universal" (shows up in every slot); common
    // usage is to pin a material to one specific mode+slot pair.
    static const char* kModeLabels[] = {
        "(any mode)", "bandori", "arcaea", "cytus", "lanota", "phigros"
    };
    int modeIdx = 0;
    if      (m_editingMaterial.targetMode == "bandori") modeIdx = 1;
    else if (m_editingMaterial.targetMode == "arcaea")  modeIdx = 2;
    else if (m_editingMaterial.targetMode == "cytus")   modeIdx = 3;
    else if (m_editingMaterial.targetMode == "lanota")  modeIdx = 4;
    else if (m_editingMaterial.targetMode == "phigros") modeIdx = 5;
    if (ImGui::Combo("Target mode", &modeIdx, kModeLabels, IM_ARRAYSIZE(kModeLabels))) {
        m_editingMaterial.targetMode     = (modeIdx == 0) ? ""
                                         : kModeLabels[modeIdx];
        // Changing mode invalidates the slot slug — clear it so the slot
        // combo below rebuilds against the new mode's slot list.
        m_editingMaterial.targetSlotSlug.clear();
    }

    // Build slot list for the selected mode. "(any slot)" is always first.
    std::vector<std::string> slotSlugs;
    std::vector<std::string> slotLabels;
    slotSlugs.push_back("");       // index 0 = any
    slotLabels.push_back("(any slot)");
    if (modeIdx != 0) {
        MaterialModeKey mk =
              (modeIdx == 1) ? MaterialModeKey::Bandori
            : (modeIdx == 2) ? MaterialModeKey::Arcaea
            : (modeIdx == 3) ? MaterialModeKey::Cytus
            : (modeIdx == 4) ? MaterialModeKey::Lanota
            :                  MaterialModeKey::Phigros;
        for (const auto& s : getMaterialSlotsForMode(mk)) {
            slotSlugs.push_back(materialSlotSlug(s));
            std::string label = s.displayName;
            if (s.group && s.group[0]) label = std::string(s.group) + " / " + label;
            slotLabels.push_back(label);
        }
    }
    int slotIdx = 0;
    for (size_t i = 0; i < slotSlugs.size(); ++i)
        if (slotSlugs[i] == m_editingMaterial.targetSlotSlug) { slotIdx = (int)i; break; }
    std::vector<const char*> slotLabelPtrs;
    slotLabelPtrs.reserve(slotLabels.size());
    for (auto& l : slotLabels) slotLabelPtrs.push_back(l.c_str());
    if (ImGui::Combo("Target slot", &slotIdx, slotLabelPtrs.data(), (int)slotLabelPtrs.size()))
        m_editingMaterial.targetSlotSlug = slotSlugs[slotIdx];

    ImGui::Separator();

    const char* kindLabels = "Unlit\0Glow\0Scroll\0Pulse\0Gradient\0Custom\0\0";
    int kindIdx = (int)m_editingMaterial.kind;
    if (ImGui::Combo("Kind", &kindIdx, kindLabels))
        m_editingMaterial.kind = (MaterialKind)kindIdx;

    ImGui::ColorEdit4("Tint", m_editingMaterial.tint.data());

    // Param slider count depends on kind — match the legend in Material.h.
    int paramCount = 0;
    const char* paramLabels[4] = {"param0", "param1", "param2", "param3"};
    switch (m_editingMaterial.kind) {
        case MaterialKind::Glow:
            paramCount = 3;
            paramLabels[0] = "intensity"; paramLabels[1] = "falloff"; paramLabels[2] = "hdrCap";
            break;
        case MaterialKind::Scroll:
            paramCount = 4;
            paramLabels[0] = "uSpeed"; paramLabels[1] = "vSpeed";
            paramLabels[2] = "uTile";  paramLabels[3] = "vTile";
            break;
        case MaterialKind::Pulse:
            paramCount = 3;
            paramLabels[0] = "lastHitTime"; paramLabels[1] = "decay"; paramLabels[2] = "peakMult";
            break;
        case MaterialKind::Gradient:
            paramCount = 4;
            paramLabels[0] = "bottomR"; paramLabels[1] = "bottomG";
            paramLabels[2] = "bottomB"; paramLabels[3] = "mode";
            break;
        case MaterialKind::Custom:
            // Custom shaders get all 4 sliders — user's .frag decides meaning.
            paramCount = 4;
            break;
        default: break;
    }
    for (int i = 0; i < paramCount; ++i)
        ImGui::DragFloat(paramLabels[i], &m_editingMaterial.params[i], 0.01f);

    // Texture path: relative-to-project. Plain text input for now — full
    // AssetBrowser integration comes later.
    char texBuf[256] = {};
    std::snprintf(texBuf, sizeof(texBuf), "%s", m_editingMaterial.texturePath.c_str());
    if (ImGui::InputText("Texture", texBuf, sizeof(texBuf)))
        m_editingMaterial.texturePath = texBuf;

    // Custom-kind-only fields: shader path + Compile button.
    if (m_editingMaterial.kind == MaterialKind::Custom) {
        ImGui::Spacing();
        ImGui::TextDisabled("Custom shader");

        char shBuf[256] = {};
        std::snprintf(shBuf, sizeof(shBuf), "%s", m_editingMaterial.customShaderPath.c_str());
        // Accepts .frag (GLSL source, compiled via glslc) or .spv
        // (pre-compiled SPIR-V, loaded as-is).
        if (ImGui::InputText("Shader (.frag or .spv)", shBuf, sizeof(shBuf)))
            m_editingMaterial.customShaderPath = shBuf;

        if (ImGui::Button("Compile")) {
            // Resolve project-relative path before handing to the compiler.
            fs::path absShader = fs::path(lib.projectDir()) / m_editingMaterial.customShaderPath;
            ShaderCompileResult r = compileFragmentToSpv(absShader, true);
            if (r.ok) {
                m_materialCompileLog = "OK — " + r.spvPath;
                if (!r.errorLog.empty()) m_materialCompileLog += "\n" + r.errorLog;
            } else {
                m_materialCompileLog = "FAILED\n" + r.errorLog;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Template...")) {
            // Emit a boilerplate frag that conforms to the push-constant
            // block, so new users have something that at least compiles.
            fs::path target = fs::path(lib.projectDir()) /
                              (m_editingMaterial.customShaderPath.empty()
                                   ? ("assets/shaders/" + m_editingMaterial.name + ".frag")
                                   : m_editingMaterial.customShaderPath);
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            if (!fs::exists(target, ec)) {
                std::ofstream f(target);
                f << "#version 450\n"
                     "layout(set = 0, binding = 0) uniform FrameUBO { mat4 viewProj; float time; } ubo;\n"
                     "layout(set = 1, binding = 0) uniform sampler2D texSampler;\n"
                     "layout(push_constant) uniform PC {\n"
                     "    mat4  model;\n"
                     "    vec4  tint;\n"
                     "    vec4  uvTransform;\n"
                     "    vec4  params;\n"
                     "    uint  kind;\n"
                     "    uint  _pad[3];\n"
                     "} pc;\n"
                     "layout(location = 0) in vec2 fragUV;\n"
                     "layout(location = 1) in vec4 fragColor;\n"
                     "layout(location = 0) out vec4 outColor;\n"
                     "void main() {\n"
                     "    outColor = texture(texSampler, fragUV) * fragColor;\n"
                     "    if (outColor.a < 0.01) discard;\n"
                     "}\n";
            }
            // Make the path the material references project-relative.
            fs::path rel = fs::relative(target, lib.projectDir(), ec);
            m_editingMaterial.customShaderPath = rel.generic_string();
        }

        if (!m_materialCompileLog.empty()) {
            ImGui::Spacing();
            ImGui::TextUnformatted("Compile log:");
            ImGui::InputTextMultiline("##compile_log",
                m_materialCompileLog.data(), m_materialCompileLog.size() + 1,
                ImVec2(-1, 100),
                ImGuiInputTextFlags_ReadOnly);
        }

        // ── AI Generate (Custom shaders) ────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();

        if (!m_shaderGen->cfgLoaded) {
            loadAIEditorConfig(shaderGenConfigPath(), m_shaderGen->cfg);
            m_shaderGen->cfgLoaded = true;
        }
        // Deliver any completed generation to the callback. Called each frame
        // the Materials tab is visible - if the user navigates away mid-flight,
        // delivery happens when they return. No Vulkan / ImGui on the worker.
        m_shaderGen->client.pollCompletion();

        ImGui::TextUnformatted("AI Generate");
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", m_shaderGen->cfg.model.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Settings##shadergen"))
            ImGui::OpenPopup("shadergen_settings");

        if (ImGui::BeginPopup("shadergen_settings")) {
            bool changed = false;
            char endpointBuf[512];
            std::strncpy(endpointBuf, m_shaderGen->cfg.endpoint.c_str(), sizeof(endpointBuf) - 1);
            endpointBuf[sizeof(endpointBuf) - 1] = 0;
            ImGui::SetNextItemWidth(300);
            if (ImGui::InputText("Endpoint", endpointBuf, sizeof(endpointBuf))) {
                m_shaderGen->cfg.endpoint = endpointBuf; changed = true;
            }

            char modelBuf[256];
            std::strncpy(modelBuf, m_shaderGen->cfg.model.c_str(), sizeof(modelBuf) - 1);
            modelBuf[sizeof(modelBuf) - 1] = 0;
            ImGui::SetNextItemWidth(300);
            if (ImGui::InputText("Model", modelBuf, sizeof(modelBuf))) {
                m_shaderGen->cfg.model = modelBuf; changed = true;
            }

            char keyBuf[512];
            std::strncpy(keyBuf, m_shaderGen->cfg.apiKey.c_str(), sizeof(keyBuf) - 1);
            keyBuf[sizeof(keyBuf) - 1] = 0;
            ImGui::SetNextItemWidth(300);
            if (ImGui::InputText("API key", keyBuf, sizeof(keyBuf))) {
                m_shaderGen->cfg.apiKey = keyBuf; changed = true;
            }

            int timeout = m_shaderGen->cfg.timeoutSecs;
            if (ImGui::SliderInt("Timeout (s)", &timeout, 10, 600)) {
                m_shaderGen->cfg.timeoutSecs = timeout; changed = true;
            }

            ImGui::SliderInt("Max attempts", &m_shaderGen->maxAttempts, 1, 6);

            if (changed)
                saveAIEditorConfig(shaderGenConfigPath(), m_shaderGen->cfg);

            ImGui::EndPopup();
        }

        ImGui::TextWrapped("Describe the effect in plain English. The AI will write a .frag, compile it, and retry on glslc errors.");
        ImGui::InputTextMultiline("##shadergen_prompt",
            m_shaderGen->prompt, sizeof(m_shaderGen->prompt),
            ImVec2(-1, 80));

        const bool inFlight = m_shaderGen->client.isRunning();
        if (inFlight) ImGui::BeginDisabled();
        if (ImGui::Button("Generate")) {
            // Bind the result callback exactly once - it captures `this` and
            // writes to main-thread UI state on poll.
            if (!m_shaderGen->callbackBound) {
                m_shaderGen->client.setCallback([this](ShaderGenResult r) {
                    m_shaderGen->finalLog     = r.attemptsLog;
                    m_shaderGen->finalError   = r.errorMessage;
                    m_shaderGen->finalSpvPath = r.spvPath;
                    if (r.success) {
                        m_statusMsg   = "Shader gen OK (attempts=" +
                                        std::to_string(r.attempts) + ")";
                        m_materialCompileLog = "OK (AI) - " + r.spvPath +
                                               "\n\n" + r.attemptsLog;
                    } else {
                        m_statusMsg = "Shader gen failed: " + r.errorMessage;
                        m_materialCompileLog = "FAILED (AI)\n" +
                                               r.errorMessage + "\n\n" +
                                               r.attemptsLog;
                    }
                    m_statusTimer = 3.f;
                });
                m_shaderGen->callbackBound = true;
            }

            // Resolve target .frag path: reuse the material's existing path
            // if set, otherwise synthesize one from the material name and
            // record it so the existing Compile button picks it up next.
            fs::path target;
            std::error_code ec;
            if (!m_editingMaterial.customShaderPath.empty()) {
                target = fs::path(lib.projectDir()) /
                         m_editingMaterial.customShaderPath;
            } else {
                std::string rel = "assets/shaders/" +
                                  m_editingMaterial.name + ".frag";
                target = fs::path(lib.projectDir()) / rel;
                m_editingMaterial.customShaderPath = rel;
            }
            fs::create_directories(target.parent_path(), ec);

            std::string userPrompt = m_shaderGen->prompt;
            if (userPrompt.empty())
                userPrompt = "A simple animated colored quad using ubo.time.";

            // Inject the material's declared target mode + slot so the model
            // can tailor the shader to its visual role. Enforces the Phase 4
            // "a material belongs to one game-object type" constraint at the
            // prompt level.
            std::string ctxLine;
            const std::string& tMode = m_editingMaterial.targetMode;
            const std::string& tSlot = m_editingMaterial.targetSlotSlug;
            if (!tMode.empty() && !tSlot.empty()) {
                ctxLine = "This shader is for the '" + tSlot +
                          "' slot in the '" + tMode +
                          "' game mode. Tailor the visual to that role "
                          "(short-lived tap vs. long hold body vs. persistent "
                          "track surface, etc.). Do not produce generic effects "
                          "that would only make sense on a different slot.\n\n";
            } else if (!tMode.empty()) {
                ctxLine = "This shader is for the '" + tMode +
                          "' game mode (any slot). Keep it broadly applicable "
                          "to that mode's visuals.\n\n";
            }
            if (!ctxLine.empty()) userPrompt = ctxLine + userPrompt;

            m_shaderGen->finalLog.clear();
            m_shaderGen->finalError.clear();
            m_shaderGen->finalSpvPath.clear();

            m_shaderGen->client.startGeneration(
                m_shaderGen->cfg, userPrompt, target.string(),
                m_shaderGen->maxAttempts);
        }
        if (inFlight) ImGui::EndDisabled();

        // Live status while a request is in flight.
        if (inFlight) {
            auto s = m_shaderGen->client.liveStatus();
            ImGui::SameLine();
            ImGui::Text("[%d/%d] %s",
                        s.attempt, s.maxAttempts,
                        s.phase.empty() ? "starting..." : s.phase.c_str());
        }

        // Final log / error after completion.
        if (!inFlight &&
            (!m_shaderGen->finalLog.empty() || !m_shaderGen->finalError.empty()))
        {
            ImGui::Spacing();
            ImGui::TextUnformatted("Generation log:");
            std::string combined = m_shaderGen->finalLog;
            if (!m_shaderGen->finalError.empty())
                combined += "\n---ERROR---\n" + m_shaderGen->finalError;
            ImGui::InputTextMultiline("##gen_log",
                combined.data(), combined.size() + 1,
                ImVec2(-1, 120),
                ImGuiInputTextFlags_ReadOnly);
        }
    }

    // ── Save / Delete ───────────────────────────────────────────────────────
    ImGui::Separator();
    if (ImGui::Button("Save")) {
        lib.upsert(m_editingMaterial);
        m_statusMsg   = "Saved material: " + m_editingMaterial.name;
        m_statusTimer = 3.f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        lib.remove(m_editingMaterial.name);
        m_selectedMaterial.clear();
        m_materialEditLoaded = false;
        m_statusMsg   = "Deleted material";
        m_statusTimer = 3.f;
    }
}

// ── renderAssets ──────────────────────────────────────────────────────────────

void StartScreenEditor::renderAssets() {
    // ── toolbar ───────────────────────────────────────────────────────────────
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
                           L"Images & GIFs\0*.png;*.jpg;*.jpeg;*.gif\0"
                           L"Videos\0*.mp4;*.webm\0";
        ofn.nFilterIndex = 1;
        ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST |
                           OFN_ALLOWMULTISELECT | OFN_EXPLORER;
        if (GetOpenFileNameW(&ofn)) {
            std::vector<std::string> paths;
            wchar_t* p = szFile;
            std::wstring dir = p;
            p += dir.size() + 1;
            if (*p == L'\0') {
                // Single file
                int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1,
                                              nullptr, 0, nullptr, nullptr);
                std::string path(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1,
                                    path.data(), len, nullptr, nullptr);
                paths.push_back(std::move(path));
            } else {
                while (*p) {
                    std::wstring fname = p;
                    std::wstring full  = dir + L"\\" + fname;
                    int len = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1,
                                                  nullptr, 0, nullptr, nullptr);
                    std::string path(len - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1,
                                        path.data(), len, nullptr, nullptr);
                    paths.push_back(std::move(path));
                    p += fname.size() + 1;
                }
            }
            if (!paths.empty()) importFiles(paths);
        }
#endif
    }
    ImGui::Separator();

    // ── file list (horizontal groups) ─────────────────────────────────────────
    if (!m_assetsScanned) {
        ImGui::TextDisabled("(not scanned)");
        return;
    }

    bool anyFiles = !m_assets.images.empty() || !m_assets.gifs.empty() ||
                    !m_assets.videos.empty() || !m_assets.audios.empty() ||
                    !m_assets.materials.empty();
    if (!anyFiles) {
        // Drop-zone hint
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p  = ImGui::GetCursorScreenPos();
        ImVec2 sz = ImGui::GetContentRegionAvail();
        dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y - 4),
                    IM_COL32(120, 120, 120, 100), 4.f, 0, 1.5f);
        const char* hint = "Drop image / GIF / video files here, or click Open File...";
        ImVec2 tsz = ImGui::CalcTextSize(hint);
        dl->AddText(ImVec2(p.x + sz.x * 0.5f - tsz.x * 0.5f,
                           p.y + sz.y * 0.5f - tsz.y * 0.5f),
                    IM_COL32(150, 150, 150, 200), hint);
        return;
    }

    // Render each group as a horizontal strip of thumbnails (draggable)
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

            // Centered filename label below thumbnail
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

    drawThumbs(m_assets.images);
    drawThumbs(m_assets.gifs);
    drawThumbs(m_assets.videos);

    // Audio files — no image preview, just a styled placeholder
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

    // Material tiles — clicking one opens the Materials editor tab with
    // that asset selected. Rendered with a distinctive "MAT" icon so they
    // don't blend into the audio group.
    for (int i = 0; i < (int)m_assets.materials.size(); ++i) {
        const std::string& f = m_assets.materials[i];
        std::string name = fs::path(f).filename().string();
        std::string stem = fs::path(f).stem().string();
        ImGui::PushID(2000 + i);
        ImGui::BeginGroup();
        ImVec2 thumbPos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##m", ImVec2(thumbSize, thumbSize));
        bool clicked = ImGui::IsItemHovered() && ImGui::IsMouseClicked(0);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(thumbPos, ImVec2(thumbPos.x + thumbSize, thumbPos.y + thumbSize),
                          IM_COL32(50, 30, 70, 255), 4.f);
        const char* icon = "MAT";
        ImVec2 mizs = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(thumbPos.x + thumbSize * 0.5f - mizs.x * 0.5f,
                           thumbPos.y + thumbSize * 0.5f - mizs.y * 0.5f),
                    IM_COL32(220, 180, 255, 220), icon);
        if (ImGui::IsItemHovered()) {
            dl->AddRect(thumbPos, ImVec2(thumbPos.x + thumbSize, thumbPos.y + thumbSize),
                        IM_COL32(200, 140, 255, 200), 4.f, 0, 2.f);
            ImGui::SetTooltip("%s\n(click to edit)", f.c_str());
        }
        if (clicked) {
            m_selectedMaterial = stem;
            if (m_engine) {
                if (const MaterialAsset* a = m_engine->materialLibrary().get(stem)) {
                    m_editingMaterial    = *a;
                    m_materialEditLoaded = true;
                }
            }
            m_materialCompileLog.clear();
            m_materialsTabRequested = true;
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
        ImGui::SameLine(0.f, 6.f);
        ImGui::PopID();
    }

    if (!toDelete.empty()) {
        std::string fullPath = m_projectPath + "/" + toDelete;
        try { fs::remove(fullPath); } catch (...) {}

        // Clear references if deleted file was in use
        if (std::string(m_bgFile) == toDelete) {
            m_bgFile[0] = '\0';
            if (m_ctx) unloadBackground(*m_ctx, *m_bufMgr);
            m_bgType = BgType::None;
        }
        if (std::string(m_logoImageFile) == toDelete) {
            m_logoImageFile[0] = '\0';
            if (m_ctx) unloadLogoImage(*m_ctx, *m_bufMgr);
        }
        if (std::string(m_bgMusic) == toDelete) m_bgMusic[0] = '\0';
        if (std::string(m_tapSfx)  == toDelete) m_tapSfx[0]  = '\0';

        // Evict from thumbnail cache
        auto it = m_thumbCache.find(toDelete);
        if (it != m_thumbCache.end()) {
            if (it->second.tex.image != VK_NULL_HANDLE && m_ctx && m_bufMgr) {
                vkDestroySampler(m_ctx->device(), it->second.tex.sampler, nullptr);
                vkDestroyImageView(m_ctx->device(), it->second.tex.view, nullptr);
                vmaDestroyImage(m_bufMgr->allocator(), it->second.tex.image, it->second.tex.allocation);
            }
            m_thumbCache.erase(it);
        }

        m_assetsScanned = false; // trigger rescan next frame
    }
}
