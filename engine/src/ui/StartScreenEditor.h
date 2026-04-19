#pragma once
#include "GifPlayer.h"
#include "AssetBrowser.h"
#include "ImageEditor.h"
#include "renderer/vulkan/TextureManager.h"
#include "renderer/MaterialAsset.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

class Engine;
class VulkanContext;
class BufferManager;
class ImGuiLayer;
struct ShaderGenUIState;   // defined in StartScreenEditor.cpp; holds async shader-gen state

enum class LogoType  { Text, Image };
enum class BgType    { None, Image, Gif, Video };

enum class TransitionEffect {
    Fade      = 0,
    SlideLeft = 1,
    ZoomIn    = 2,
    Ripple    = 3,
    Custom    = 4
};

class StartScreenEditor {
public:
    StartScreenEditor();
    ~StartScreenEditor();

    void render(Engine* engine);
    void load(const std::string& projectPath);
    void save();
    void renderGamePreview(ImVec2 origin, ImVec2 size);

    void initVulkan(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui,
                    GLFWwindow* window = nullptr);
    void shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr);

    // Called by Engine::dropCallback when files are dragged onto the window
    void notifyFilesDropped() { m_assetsScanned = false; }
    const std::string& projectPath() const { return m_projectPath; }
    TransitionEffect transitionEffect() const { return m_transition; }
    float transitionDuration() const { return m_transitionDur; }
    void importFiles(const std::vector<std::string>& srcPaths);

private:
    void renderPreview();
    void renderProperties();
    void renderAssets();
    // Project-level Material asset CRUD panel. Lives in the Properties
    // split as a sibling tab. Reads/writes through Engine::materialLibrary().
    void renderMaterials(Engine* engine);

    void loadBackground(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui);
    void loadLogoImage (VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui);
    void unloadBackground(VulkanContext& ctx, BufferManager& bufMgr);
    void unloadLogoImage (VulkanContext& ctx, BufferManager& bufMgr);

    // ── project ──────────────────────────────────────────────────────────────
    std::string m_projectPath;
    bool        m_loaded = false;

    // ── Vulkan handles (set by initVulkan) ───────────────────────────────────
    VulkanContext* m_ctx    = nullptr;
    BufferManager* m_bufMgr = nullptr;
    ImGuiLayer*    m_imgui  = nullptr;
    GLFWwindow*    m_window = nullptr;

    // ── background ───────────────────────────────────────────────────────────
    BgType      m_bgType = BgType::None;
    char        m_bgFile[256] = "";          // relative path from project root
    Texture     m_bgTexture   = {};
    VkDescriptorSet m_bgDesc  = VK_NULL_HANDLE;
    GifPlayer   m_gifPlayer;

    // ── logo ─────────────────────────────────────────────────────────────────
    LogoType m_logoType = LogoType::Text;

    // text logo
    char  m_logoText[256]  = "";
    float m_logoColor[4]   = {1.f, 1.f, 1.f, 1.f};
    float m_logoFontSize   = 32.f;
    bool  m_logoBold       = false;
    bool  m_logoItalic     = false;

    // image logo
    char        m_logoImageFile[256] = "";
    Texture     m_logoTexture        = {};
    VkDescriptorSet m_logoDesc       = VK_NULL_HANDLE;

    // shared logo properties
    float m_logoPos[2]   = {0.5f, 0.3f};
    float m_logoScale    = 1.0f;
    bool  m_logoGlow     = false;
    float m_logoGlowColor[4] = {1.f, 0.8f, 0.2f, 0.8f};
    float m_logoGlowRadius   = 8.f;

    // ── tap text ─────────────────────────────────────────────────────────────
    char  m_tapText[256]    = "Tap to Start";
    float m_tapTextPos[2]   = {0.5f, 0.8f};
    int   m_tapTextSize     = 24;

    // ── audio ─────────────────────────────────────────────────────────────────
    char  m_bgMusic[256]       = "";
    float m_bgMusicVolume      = 1.0f;
    bool  m_bgMusicLoop        = true;
    char  m_tapSfx[256]        = "";
    float m_tapSfxVolume       = 1.0f;

    // ── transition ───────────────────────────────────────────────────────────
    TransitionEffect m_transition     = TransitionEffect::Fade;
    float            m_transitionDur  = 0.5f;
    char             m_customScript[256] = "";

    // ── asset browser cache ──────────────────────────────────────────────────
    AssetList   m_assets;
    bool        m_assetsScanned = false;
    // ImageEditor m_imageEditor;

    // ── thumbnail cache ──────────────────────────────────────────────────────
    struct ThumbEntry { Texture tex; VkDescriptorSet desc = VK_NULL_HANDLE; };
    std::unordered_map<std::string, ThumbEntry> m_thumbCache;
    void clearThumbnails(VulkanContext& ctx, BufferManager& bufMgr);
    VkDescriptorSet getThumb(const std::string& relPath);

    // ── panel split ratios (draggable) ───────────────────────────────────────
    float m_hSplit = 0.60f;   // Preview / Properties horizontal split
    float m_vSplit = 0.72f;   // Top row / Assets vertical split

    // ── preview tab ─────────────────────────────────────────────────────────
    int  m_previewTab = 0;    // 0=Editor, 1=Game Preview
    Engine* m_engine  = nullptr;

    // ── status feedback ──────────────────────────────────────────────────────
    std::string m_statusMsg;
    float       m_statusTimer = 0.f;

    // ── Materials panel state ───────────────────────────────────────────────
    // Form copy so edits don't touch the library until the user clicks Save.
    std::string   m_selectedMaterial;
    MaterialAsset m_editingMaterial;
    bool          m_materialEditLoaded = false;    // did we populate the form?
    std::string   m_materialCompileLog;
    bool          m_showNewMaterialDialog = false;
    char          m_newMaterialNameBuf[128] = {};
    // Set when a material tile is clicked in the Assets browser. On the next
    // render of the Properties tab bar, force the Materials tab to open and
    // clear the flag.
    bool          m_materialsTabRequested = false;

    // ── AI shader generator (Custom kind only) ──────────────────────────────
    // Pimpl'd because ShaderGenClient pulls <thread>/<atomic>/<mutex> — we
    // don't want those leaking into Engine.h and GameFlowPreview.h.
    std::unique_ptr<ShaderGenUIState> m_shaderGen;
};
