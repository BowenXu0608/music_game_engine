#pragma once
#include "renderer/vulkan/TextureManager.h"
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <filesystem>

// Forward declarations — full includes only in ImageEditor.cpp
class VulkanContext;
class BufferManager;
class ImGuiLayer;

// ── Shared image editor popup ────────────────────────────────────────────────
// Usage: call open() to load an image, render() each frame, check needsRescan()
// after changes are applied.

class ImageEditor {
public:
    // Open the editor for a given asset (relPath relative to project root)
    void open(const std::string& projectPath, const std::string& relPath,
              VulkanContext* ctx, BufferManager* bufMgr, ImGuiLayer* imgui);

    // Call every frame — renders the modal popup
    void render();

    // Call before Vulkan shutdown to release GPU resources safely
    void shutdown();

    // After render(), check if the image was saved — caller should rescan assets & evict cache
    bool needsRescan() {
        bool r = m_applied;
        m_applied = false;
        return r;
    }

    const std::string& editedRelPath() const { return m_relPath; }

    ~ImageEditor();

private:
    // ── Pixel operations ────────────────────────────────────────────────────
    void applyCrop();
    void applyResize();
    void rotateCW();
    void rotateCCW();
    void rotate180();
    void flipH();
    void flipV();
    void saveImage();
    void handleCropDrag(ImVec2 imgMin, float dispW, float dispH, float scale);
    void clampCrop();
    void resetCropAndResize();
    void uploadPreview();
    void freePreview();
    void freePixels();

    // ── State ───────────────────────────────────────────────────────────────
    std::string m_projectPath;
    std::string m_relPath;
    VulkanContext* m_ctx    = nullptr;
    BufferManager* m_bufMgr = nullptr;
    ImGuiLayer*    m_imgui  = nullptr;

    // Source image
    uint8_t* m_pixels = nullptr;
    int m_srcW = 0, m_srcH = 0, m_srcCh = 0;

    // Working copy (RGBA)
    std::vector<uint8_t> m_work;
    int m_workW = 0, m_workH = 0;

    // Crop rect (in work pixel coords)
    int m_cropL = 0, m_cropT = 0, m_cropR = 0, m_cropB = 0;

    // Resize target
    int  m_resizeW = 0, m_resizeH = 0;
    bool m_keepAspect = true;

    // Crop drag state
    int    m_dragCorner = -1;
    ImVec2 m_dragStart;
    int    m_dragCropL = 0, m_dragCropT = 0, m_dragCropR = 0, m_dragCropB = 0;

    // Preview texture
    Texture         m_previewTex  = {};
    VkDescriptorSet m_previewDesc = VK_NULL_HANDLE;

    bool m_dirty       = false;
    bool m_applied     = false;
    bool m_pendingOpen = false;
};
