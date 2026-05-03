#pragma once
#include "ui/GifPlayer.h"
#include "renderer/vulkan/TextureManager.h"
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <string>

class VulkanContext;
class BufferManager;
class ImGuiLayer;

enum class LogoType  { Text, Image };
enum class BgType    { None, Image, Gif, Video };

enum class TransitionEffect {
    Fade      = 0,
    SlideLeft = 1,
    ZoomIn    = 2,
    Ripple    = 3,
    Custom    = 4
};

// Player-facing start-screen rendering. Owns the start_screen.json scene state
// (background, logo, tap prompt) and draws it into an ImGui window. Compiled
// into both desktop (where StartScreenEditor wraps it) and Android (where
// AndroidEngine drives it directly).
class StartScreenView {
public:
    StartScreenView() = default;
    virtual ~StartScreenView() = default;

    // Pass nullptr for `imgui` on Android (no ImGuiLayer wrapper there).
    // Texture registration uses ImGui_ImplVulkan_AddTexture directly so the
    // ImGuiLayer is only consulted for the optional getLogoFont fallback.
    void initVulkan(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer* imgui);
    void shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr);

    virtual void load(const std::string& projectRoot);
    void save();

    // Draw the player-facing scene at (origin, size).
    void renderGamePreview(ImVec2 origin, ImVec2 size);

    // Re-upload textures after editor changes a file path.
    void reloadBackground();
    void reloadLogoImage();
    void unloadBackground(VulkanContext& ctx, BufferManager& bufMgr);
    void unloadLogoImage (VulkanContext& ctx, BufferManager& bufMgr);

    const std::string& projectPath() const { return m_projectPath; }
    TransitionEffect   transitionEffect() const { return m_transition; }
    float              transitionDuration() const { return m_transitionDur; }

protected:
    // ── project ──────────────────────────────────────────────────────────────
    std::string m_projectPath;
    bool        m_loaded = false;

    // ── Vulkan handles (set by initVulkan) ───────────────────────────────────
    VulkanContext* m_ctx    = nullptr;
    BufferManager* m_bufMgr = nullptr;
    ImGuiLayer*    m_imgui  = nullptr;  // optional — null on Android

    // ── background ───────────────────────────────────────────────────────────
    BgType          m_bgType  = BgType::None;
    char            m_bgFile[256] = "";
    Texture         m_bgTexture   = {};
    VkDescriptorSet m_bgDesc      = VK_NULL_HANDLE;
    GifPlayer       m_gifPlayer;

    // ── logo ─────────────────────────────────────────────────────────────────
    LogoType m_logoType = LogoType::Text;

    // text logo
    char  m_logoText[256] = "";
    float m_logoColor[4]  = {1.f, 1.f, 1.f, 1.f};
    float m_logoFontSize  = 32.f;
    bool  m_logoBold      = false;
    bool  m_logoItalic    = false;

    // image logo
    char            m_logoImageFile[256] = "";
    Texture         m_logoTexture        = {};
    VkDescriptorSet m_logoDesc           = VK_NULL_HANDLE;

    // shared logo properties
    float m_logoPos[2]       = {0.5f, 0.3f};
    float m_logoScale        = 1.0f;
    bool  m_logoGlow         = false;
    float m_logoGlowColor[4] = {1.f, 0.8f, 0.2f, 0.8f};
    float m_logoGlowRadius   = 8.f;

    // ── tap text ─────────────────────────────────────────────────────────────
    char  m_tapText[256]  = "Tap to Start";
    float m_tapTextPos[2] = {0.5f, 0.8f};
    int   m_tapTextSize   = 24;

    // ── audio ────────────────────────────────────────────────────────────────
    char  m_bgMusic[256]  = "";
    float m_bgMusicVolume = 1.0f;
    bool  m_bgMusicLoop   = true;
    char  m_tapSfx[256]   = "";
    float m_tapSfxVolume  = 1.0f;

    // ── transition ───────────────────────────────────────────────────────────
    TransitionEffect m_transition        = TransitionEffect::Fade;
    float            m_transitionDur     = 0.5f;
    char             m_customScript[256] = "";
};
