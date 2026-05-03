#include "StartScreenView.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/BufferManager.h"
#include "renderer/vulkan/TextureManager.h"
#include "ui/ImGuiLayer.h"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cfloat>

namespace fs = std::filesystem;
using json = nlohmann::json;

void StartScreenView::initVulkan(VulkanContext& ctx, BufferManager& bufMgr,
                                 ImGuiLayer* imgui) {
    m_ctx    = &ctx;
    m_bufMgr = &bufMgr;
    m_imgui  = imgui;
}

void StartScreenView::shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr) {
    unloadBackground(ctx, bufMgr);
    unloadLogoImage(ctx, bufMgr);
}

void StartScreenView::unloadBackground(VulkanContext& ctx, BufferManager& bufMgr) {
    if (m_gifPlayer.isLoaded()) m_gifPlayer.unload(ctx, bufMgr);
    if (m_bgTexture.image != VK_NULL_HANDLE) {
        vkDestroySampler(ctx.device(), m_bgTexture.sampler, nullptr);
        vkDestroyImageView(ctx.device(), m_bgTexture.view, nullptr);
        vmaDestroyImage(bufMgr.allocator(), m_bgTexture.image, m_bgTexture.allocation);
        m_bgTexture = {};
        m_bgDesc    = VK_NULL_HANDLE;
    }
}

void StartScreenView::unloadLogoImage(VulkanContext& ctx, BufferManager& bufMgr) {
    if (m_logoTexture.image != VK_NULL_HANDLE) {
        vkDestroySampler(ctx.device(), m_logoTexture.sampler, nullptr);
        vkDestroyImageView(ctx.device(), m_logoTexture.view, nullptr);
        vmaDestroyImage(bufMgr.allocator(), m_logoTexture.image, m_logoTexture.allocation);
        m_logoTexture = {};
        m_logoDesc    = VK_NULL_HANDLE;
    }
}

void StartScreenView::reloadBackground() {
    if (!m_ctx || !m_bufMgr) return;
    unloadBackground(*m_ctx, *m_bufMgr);
    if (m_bgFile[0] == '\0') { m_bgType = BgType::None; return; }

    std::string fullPath = m_projectPath + "/" + m_bgFile;
    std::string ext = fs::path(fullPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".gif") {
        if (m_gifPlayer.load(fullPath, *m_ctx, *m_bufMgr))
            m_bgType = BgType::Gif;
        else
            m_bgType = BgType::None;
    } else if (ext == ".mp4" || ext == ".webm") {
        m_bgType = BgType::Video;
    } else {
        try {
            TextureManager texMgr;
            texMgr.init(*m_ctx, *m_bufMgr);
            m_bgTexture = texMgr.loadFromFile(*m_ctx, *m_bufMgr, fullPath);
            m_bgDesc    = ImGui_ImplVulkan_AddTexture(
                m_bgTexture.sampler, m_bgTexture.view,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_bgType    = BgType::Image;
        } catch (...) {
            m_bgType = BgType::None;
        }
    }
}

void StartScreenView::reloadLogoImage() {
    if (!m_ctx || !m_bufMgr) return;
    unloadLogoImage(*m_ctx, *m_bufMgr);
    if (m_logoImageFile[0] == '\0') return;
    std::string fullPath = m_projectPath + "/" + m_logoImageFile;
    try {
        TextureManager texMgr;
        texMgr.init(*m_ctx, *m_bufMgr);
        m_logoTexture = texMgr.loadFromFile(*m_ctx, *m_bufMgr, fullPath);
        m_logoDesc    = ImGui_ImplVulkan_AddTexture(
            m_logoTexture.sampler, m_logoTexture.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } catch (...) {}
}

void StartScreenView::load(const std::string& projectPath) {
    m_projectPath = projectPath;

    std::string configPath = projectPath + "/start_screen.json";
    std::ifstream f(configPath);
    if (!f.is_open()) return;

    json j;
    try { j = json::parse(f); } catch (...) { return; }

    if (j.contains("background")) {
        if (j["background"].is_object()) {
            std::string file = j["background"].value("file", "");
            strncpy(m_bgFile, file.c_str(), 255); m_bgFile[255] = '\0';
        } else if (j["background"].is_string()) {
            std::string file = j["background"].get<std::string>();
            strncpy(m_bgFile, file.c_str(), 255); m_bgFile[255] = '\0';
        } else {
            m_bgFile[0] = '\0';
        }
    } else {
        m_bgFile[0] = '\0';
    }

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
            m_logoType = LogoType::Text;
            std::string text = j["logo"].get<std::string>();
            strncpy(m_logoText, text.c_str(), 255); m_logoText[255] = '\0';
            if (j.contains("logoPosition")) {
                m_logoPos[0] = j["logoPosition"].value("x", 0.5f);
                m_logoPos[1] = j["logoPosition"].value("y", 0.3f);
            }
            m_logoScale = j.value("logoScale", 1.f);
        }
    }

    std::string tap = j.value("tapText", "Tap to Start");
    strncpy(m_tapText, tap.c_str(), 255); m_tapText[255] = '\0';
    m_tapTextSize = j.value("tapTextSize", 24);
    if (j.contains("tapTextPosition")) {
        m_tapTextPos[0] = j["tapTextPosition"].value("x", 0.5f);
        m_tapTextPos[1] = j["tapTextPosition"].value("y", 0.8f);
    }

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

    if (m_ctx && m_bufMgr) {
        reloadBackground();
        if (m_logoType == LogoType::Image) reloadLogoImage();
    }
}

void StartScreenView::save() {
    if (m_projectPath.empty()) return;

    json j;

    j["background"]["file"] = m_bgFile;
    switch (m_bgType) {
        case BgType::Image: j["background"]["type"] = "image"; break;
        case BgType::Gif:   j["background"]["type"] = "gif";   break;
        case BgType::Video: j["background"]["type"] = "video"; break;
        default:            j["background"]["type"] = "none";  break;
    }

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

    j["tapText"]              = m_tapText;
    j["tapTextPosition"]["x"] = m_tapTextPos[0];
    j["tapTextPosition"]["y"] = m_tapTextPos[1];
    j["tapTextSize"]          = m_tapTextSize;

    static const char* effectNames[] = {"fade","slide_left","zoom_in","ripple","custom"};
    j["transition"]["effect"]       = effectNames[static_cast<int>(m_transition)];
    j["transition"]["duration"]     = m_transitionDur;
    j["transition"]["customScript"] = m_customScript;

    j["audio"]["bgMusic"]       = m_bgMusic;
    j["audio"]["bgMusicVolume"] = m_bgMusicVolume;
    j["audio"]["bgMusicLoop"]   = m_bgMusicLoop;
    j["audio"]["tapSfx"]        = m_tapSfx;
    j["audio"]["tapSfxVolume"]  = m_tapSfxVolume;

    std::ofstream out(m_projectPath + "/start_screen.json");
    if (out.is_open()) out << j.dump(2);
}

void StartScreenView::renderGamePreview(ImVec2 p, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pw = size.x, ph = size.y;

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
        float maxTextW = pw * 0.96f;
        if (textSz.x > maxTextW && textSz.x > 0.f) {
            fontSize *= (maxTextW / textSz.x);
            textSz    = logoFont->CalcTextSizeA(fontSize, FLT_MAX, 0.f, m_logoText);
        }
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

    float textX = p.x + pw * m_tapTextPos[0];
    float textY = p.y + ph * m_tapTextPos[1];
    ImFont* tapFont = m_imgui ? m_imgui->getLogoFont(m_tapTextSize) : ImGui::GetFont();
    float tapFontSize = static_cast<float>(m_tapTextSize);
    ImVec2 tapSz = tapFont->CalcTextSizeA(tapFontSize, FLT_MAX, 0.f, m_tapText);
    float tapMaxW = pw * 0.96f;
    if (tapSz.x > tapMaxW && tapSz.x > 0.f) {
        tapFontSize *= (tapMaxW / tapSz.x);
        tapSz        = tapFont->CalcTextSizeA(tapFontSize, FLT_MAX, 0.f, m_tapText);
    }
    dl->AddText(tapFont, tapFontSize,
                ImVec2(textX - tapSz.x * 0.5f, textY - tapSz.y * 0.5f),
                IM_COL32(255, 255, 255, 255), m_tapText);
}
