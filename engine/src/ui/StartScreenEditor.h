#pragma once
#include "game/screens/StartScreenView.h"
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

class StartScreenEditor : public StartScreenView {
public:
    StartScreenEditor();
    ~StartScreenEditor() override;

    void render(Engine* engine);
    // initVulkan with a non-null ImGuiLayer reference (editor side).
    void initVulkan(VulkanContext& ctx, BufferManager& bufMgr, ImGuiLayer& imgui,
                    GLFWwindow* window = nullptr);
    void shutdownVulkan(VulkanContext& ctx, BufferManager& bufMgr);
    void load(const std::string& projectPath) override;

    // Called by Engine::dropCallback when files are dragged onto the window
    void notifyFilesDropped() { m_assetsScanned = false; }
    void importFiles(const std::vector<std::string>& srcPaths);

    // Project-level Material asset CRUD panel. Lives on the SongEditor
    // (gameplay) page now — exposed here so that editor can call in.
    // Reads/writes through Engine::materialLibrary().
    // When hideSelector is true the inline selectable list is suppressed;
    // callers that expose their own tile grid (SongEditor Material tab,
    // which clicks through the Assets strip) own selection instead.
    void renderMaterials(Engine* engine, bool hideSelector = false);

    // Load `name` into the edit form so the next renderMaterials() call
    // draws that material's settings. Called when the user clicks a MAT
    // tile in another editor's Assets browser.
    void selectMaterialByName(const std::string& name);

    // Position-based material preview (public so other editors — notably
    // MusicSelectionEditor — can share the same tile render logic).
    void drawMaterialPreviewAt(const MaterialAsset& m, ImVec2 p0, ImVec2 size);

private:
    void renderPreview();
    void renderProperties();
    void renderAssets();

    // Draw a synthetic preview of a material into a fixed-size rect at the
    // current cursor position. Shape follows the material's target slot
    // (note / track / arc / disk / ...), fill follows tint + texture + kind
    // (Scroll animates stripes, Pulse modulates brightness, etc.). Used in
    // both the Materials editor and the Assets browser tooltip.
    void drawMaterialPreview(const MaterialAsset& m, ImVec2 size);
    // drawMaterialPreviewAt declared above (public) so other editors
    // can share the tile render logic.

    GLFWwindow* m_window = nullptr;

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
    bool  m_assetsBarOpen = true;  // Bottom Assets strip expanded?
    float m_assetsBarH   = 200.f;  // Expanded height

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
