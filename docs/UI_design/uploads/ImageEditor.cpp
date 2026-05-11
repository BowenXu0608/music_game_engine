#include "ImageEditor.h"
#include "renderer/vulkan/VulkanContext.h"
#include "renderer/vulkan/BufferManager.h"
#include "ImGuiLayer.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <vk_mem_alloc.h>

namespace fs = std::filesystem;

ImageEditor::~ImageEditor() {
    freePixels();
    // Vulkan resources must be freed via shutdown() before device is destroyed
}

void ImageEditor::open(const std::string& projectPath, const std::string& relPath,
                       VulkanContext* ctx, BufferManager* bufMgr, ImGuiLayer* imgui) {
    m_projectPath = projectPath;
    m_relPath     = relPath;
    m_ctx         = ctx;
    m_bufMgr      = bufMgr;
    m_imgui       = imgui;
    m_dirty       = false;
    m_applied     = false;

    freePixels();
    freePreview();

    std::string fullPath = projectPath + "/" + relPath;
    m_pixels = stbi_load(fullPath.c_str(), &m_srcW, &m_srcH, &m_srcCh, 4);
    if (!m_pixels) {
        std::cout << "[ImageEditor] Failed to load: " << fullPath << "\n";
        return;
    }

    m_workW = m_srcW;
    m_workH = m_srcH;
    m_work.assign(m_pixels, m_pixels + m_workW * m_workH * 4);

    m_cropL = 0; m_cropT = 0;
    m_cropR = m_workW; m_cropB = m_workH;
    m_resizeW = m_workW;
    m_resizeH = m_workH;
    m_keepAspect = true;

    uploadPreview();
    m_pendingOpen = true;
}

void ImageEditor::render() {
    if (m_pendingOpen) {
        ImGui::OpenPopup("Image Editor");
        m_pendingOpen = false;
    }
    ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Image Editor", nullptr,
                                 ImGuiWindowFlags_NoScrollbar)) return;

    if (m_work.empty()) {
        ImGui::Text("No image loaded.");
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // ── Preview area ────────────────────────────────────────────────────
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float panelH = 180.f;
    float previewH = avail.y - panelH;
    if (previewH < 100.f) previewH = 100.f;
    float previewW = avail.x;

    float scale = std::min(previewW / (float)m_workW, previewH / (float)m_workH);
    if (scale > 1.f) scale = 1.f;
    float dispW = m_workW * scale;
    float dispH = m_workH * scale;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float offX = (previewW - dispW) * 0.5f;
    float offY = (previewH - dispH) * 0.5f;
    ImVec2 imgMin(cursor.x + offX, cursor.y + offY);
    ImVec2 imgMax(imgMin.x + dispW, imgMin.y + dispH);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cursor, ImVec2(cursor.x + previewW, cursor.y + previewH),
                      IM_COL32(20, 20, 25, 255));

    if (m_previewDesc) {
        dl->AddImage((ImTextureID)(uint64_t)m_previewDesc, imgMin, imgMax);
    }

    // Draw crop overlay
    {
        float cl = imgMin.x + (m_cropL / (float)m_workW) * dispW;
        float ct = imgMin.y + (m_cropT / (float)m_workH) * dispH;
        float cr = imgMin.x + (m_cropR / (float)m_workW) * dispW;
        float cb = imgMin.y + (m_cropB / (float)m_workH) * dispH;

        ImU32 dim = IM_COL32(0, 0, 0, 120);
        dl->AddRectFilled(imgMin, ImVec2(imgMax.x, ct), dim);
        dl->AddRectFilled(ImVec2(imgMin.x, cb), imgMax, dim);
        dl->AddRectFilled(ImVec2(imgMin.x, ct), ImVec2(cl, cb), dim);
        dl->AddRectFilled(ImVec2(cr, ct), ImVec2(imgMax.x, cb), dim);

        dl->AddRect(ImVec2(cl, ct), ImVec2(cr, cb),
                    IM_COL32(255, 200, 50, 220), 0.f, 0, 2.f);

        float hs = 6.f;
        ImU32 handleCol = IM_COL32(255, 220, 80, 255);
        dl->AddRectFilled(ImVec2(cl-hs, ct-hs), ImVec2(cl+hs, ct+hs), handleCol);
        dl->AddRectFilled(ImVec2(cr-hs, ct-hs), ImVec2(cr+hs, ct+hs), handleCol);
        dl->AddRectFilled(ImVec2(cl-hs, cb-hs), ImVec2(cl+hs, cb+hs), handleCol);
        dl->AddRectFilled(ImVec2(cr-hs, cb-hs), ImVec2(cr+hs, cb+hs), handleCol);

        handleCropDrag(imgMin, dispW, dispH, scale);
    }

    ImGui::Dummy(ImVec2(previewW, previewH));

    ImGui::Text("Original: %dx%d  |  Current: %dx%d  |  Crop: [%d,%d]-[%d,%d] (%dx%d)",
                m_srcW, m_srcH, m_workW, m_workH,
                m_cropL, m_cropT, m_cropR, m_cropB,
                m_cropR - m_cropL, m_cropB - m_cropT);
    ImGui::Separator();

    // ── Controls ────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Crop", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed = false;
        ImGui::PushItemWidth(80);
        changed |= ImGui::InputInt("Left",   &m_cropL, 1, 10);
        ImGui::SameLine();
        changed |= ImGui::InputInt("Top",    &m_cropT, 1, 10);
        ImGui::SameLine();
        changed |= ImGui::InputInt("Right",  &m_cropR, 1, 10);
        ImGui::SameLine();
        changed |= ImGui::InputInt("Bottom", &m_cropB, 1, 10);
        ImGui::PopItemWidth();
        if (changed) clampCrop();
        ImGui::SameLine();
        if (ImGui::Button("Apply Crop")) { applyCrop(); }
        ImGui::SameLine();
        if (ImGui::Button("Reset Crop")) {
            m_cropL = 0; m_cropT = 0;
            m_cropR = m_workW; m_cropB = m_workH;
        }
    }

    if (ImGui::CollapsingHeader("Resize", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushItemWidth(100);
        float aspect = (m_workH > 0) ? (float)m_workW / m_workH : 1.f;
        if (ImGui::InputInt("Width##rsz", &m_resizeW, 1, 10)) {
            if (m_resizeW < 1) m_resizeW = 1;
            if (m_keepAspect) m_resizeH = std::max(1, (int)std::round(m_resizeW / aspect));
        }
        ImGui::SameLine();
        if (ImGui::InputInt("Height##rsz", &m_resizeH, 1, 10)) {
            if (m_resizeH < 1) m_resizeH = 1;
            if (m_keepAspect) m_resizeW = std::max(1, (int)std::round(m_resizeH * aspect));
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::Checkbox("Lock Aspect", &m_keepAspect);
        ImGui::SameLine();
        if (ImGui::Button("Apply Resize")) { applyResize(); }
    }

    if (ImGui::CollapsingHeader("Rotate / Flip", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("90 CW"))  { rotateCW();  }
        ImGui::SameLine();
        if (ImGui::Button("90 CCW")) { rotateCCW(); }
        ImGui::SameLine();
        if (ImGui::Button("180"))    { rotate180(); }
        ImGui::SameLine();
        if (ImGui::Button("Flip H")) { flipH(); }
        ImGui::SameLine();
        if (ImGui::Button("Flip V")) { flipV(); }
    }

    ImGui::Separator();

    if (ImGui::Button("Save", ImVec2(100, 0))) {
        saveImage();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ImageEditor::shutdown() { freePreview(); freePixels(); }

// ── Pixel operations ────────────────────────────────────────────────────

void ImageEditor::applyCrop() {
    clampCrop();
    int cw = m_cropR - m_cropL;
    int ch = m_cropB - m_cropT;
    if (cw <= 0 || ch <= 0) return;

    std::vector<uint8_t> cropped(cw * ch * 4);
    for (int y = 0; y < ch; ++y) {
        memcpy(&cropped[y * cw * 4],
               &m_work[((m_cropT + y) * m_workW + m_cropL) * 4],
               cw * 4);
    }
    m_work  = std::move(cropped);
    m_workW = cw;
    m_workH = ch;
    m_cropL = 0; m_cropT = 0;
    m_cropR = m_workW; m_cropB = m_workH;
    m_resizeW = m_workW; m_resizeH = m_workH;
    m_dirty = true;
    uploadPreview();
}

void ImageEditor::applyResize() {
    if (m_resizeW <= 0 || m_resizeH <= 0) return;
    if (m_resizeW == m_workW && m_resizeH == m_workH) return;

    std::vector<uint8_t> resized(m_resizeW * m_resizeH * 4);
    for (int dy = 0; dy < m_resizeH; ++dy) {
        float sy = (dy + 0.5f) * m_workH / (float)m_resizeH - 0.5f;
        int y0 = std::max(0, (int)std::floor(sy));
        int y1 = std::min(m_workH - 1, y0 + 1);
        float fy = sy - y0;
        for (int dx = 0; dx < m_resizeW; ++dx) {
            float sx = (dx + 0.5f) * m_workW / (float)m_resizeW - 0.5f;
            int x0 = std::max(0, (int)std::floor(sx));
            int x1 = std::min(m_workW - 1, x0 + 1);
            float fx = sx - x0;
            for (int c = 0; c < 4; ++c) {
                float v00 = m_work[(y0 * m_workW + x0) * 4 + c];
                float v10 = m_work[(y0 * m_workW + x1) * 4 + c];
                float v01 = m_work[(y1 * m_workW + x0) * 4 + c];
                float v11 = m_work[(y1 * m_workW + x1) * 4 + c];
                float v = v00*(1-fx)*(1-fy) + v10*fx*(1-fy) + v01*(1-fx)*fy + v11*fx*fy;
                resized[(dy * m_resizeW + dx) * 4 + c] = (uint8_t)std::clamp(v, 0.f, 255.f);
            }
        }
    }
    m_work  = std::move(resized);
    m_workW = m_resizeW;
    m_workH = m_resizeH;
    m_cropL = 0; m_cropT = 0;
    m_cropR = m_workW; m_cropB = m_workH;
    m_dirty = true;
    uploadPreview();
}

void ImageEditor::rotateCW() {
    std::vector<uint8_t> rot(m_workW * m_workH * 4);
    int nw = m_workH, nh = m_workW;
    for (int y = 0; y < m_workH; ++y)
        for (int x = 0; x < m_workW; ++x)
            memcpy(&rot[((x) * nw + (m_workH - 1 - y)) * 4],
                   &m_work[(y * m_workW + x) * 4], 4);
    m_work = std::move(rot);
    m_workW = nw; m_workH = nh;
    resetCropAndResize();
    uploadPreview();
}

void ImageEditor::rotateCCW() {
    std::vector<uint8_t> rot(m_workW * m_workH * 4);
    int nw = m_workH, nh = m_workW;
    for (int y = 0; y < m_workH; ++y)
        for (int x = 0; x < m_workW; ++x)
            memcpy(&rot[((m_workW - 1 - x) * nw + y) * 4],
                   &m_work[(y * m_workW + x) * 4], 4);
    m_work = std::move(rot);
    m_workW = nw; m_workH = nh;
    resetCropAndResize();
    uploadPreview();
}

void ImageEditor::rotate180() {
    int total = m_workW * m_workH;
    for (int i = 0; i < total / 2; ++i) {
        int j = total - 1 - i;
        for (int c = 0; c < 4; ++c)
            std::swap(m_work[i * 4 + c], m_work[j * 4 + c]);
    }
    m_dirty = true;
    uploadPreview();
}

void ImageEditor::flipH() {
    for (int y = 0; y < m_workH; ++y)
        for (int x = 0; x < m_workW / 2; ++x) {
            int x2 = m_workW - 1 - x;
            for (int c = 0; c < 4; ++c)
                std::swap(m_work[(y * m_workW + x) * 4 + c],
                          m_work[(y * m_workW + x2) * 4 + c]);
        }
    m_dirty = true;
    uploadPreview();
}

void ImageEditor::flipV() {
    for (int y = 0; y < m_workH / 2; ++y) {
        int y2 = m_workH - 1 - y;
        for (int x = 0; x < m_workW; ++x)
            for (int c = 0; c < 4; ++c)
                std::swap(m_work[(y * m_workW + x) * 4 + c],
                          m_work[(y2 * m_workW + x) * 4 + c]);
    }
    m_dirty = true;
    uploadPreview();
}

void ImageEditor::saveImage() {
    std::string fullPath = m_projectPath + "/" + m_relPath;
    std::string ext = fs::path(m_relPath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    bool ok = false;
    if (ext == ".png") {
        ok = stbi_write_png(fullPath.c_str(), m_workW, m_workH, 4,
                            m_work.data(), m_workW * 4) != 0;
    } else if (ext == ".jpg" || ext == ".jpeg") {
        ok = stbi_write_jpg(fullPath.c_str(), m_workW, m_workH, 4,
                            m_work.data(), 92) != 0;
    } else {
        std::string pngPath = fullPath.substr(0, fullPath.rfind('.')) + ".png";
        ok = stbi_write_png(pngPath.c_str(), m_workW, m_workH, 4,
                            m_work.data(), m_workW * 4) != 0;
    }

    if (ok) {
        std::cout << "[ImageEditor] Saved " << m_workW << "x" << m_workH
                  << " -> " << fullPath << "\n";
        m_applied = true;
    } else {
        std::cout << "[ImageEditor] Save failed: " << fullPath << "\n";
    }
}

void ImageEditor::handleCropDrag(ImVec2 imgMin, float dispW, float dispH, float scale) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;

    auto px2scr = [&](int px, int py) -> ImVec2 {
        return ImVec2(imgMin.x + px * scale, imgMin.y + py * scale);
    };
    auto scr2px = [&](ImVec2 s) -> ImVec2 {
        return ImVec2((s.x - imgMin.x) / scale, (s.y - imgMin.y) / scale);
    };

    float hs = 10.f;
    ImVec2 tl = px2scr(m_cropL, m_cropT);
    ImVec2 br = px2scr(m_cropR, m_cropB);

    if (ImGui::IsMouseClicked(0)) {
        m_dragCorner = -1;
        if (fabsf(mouse.x - tl.x) < hs && fabsf(mouse.y - tl.y) < hs) m_dragCorner = 0;
        else if (fabsf(mouse.x - br.x) < hs && fabsf(mouse.y - tl.y) < hs) m_dragCorner = 1;
        else if (fabsf(mouse.x - tl.x) < hs && fabsf(mouse.y - br.y) < hs) m_dragCorner = 2;
        else if (fabsf(mouse.x - br.x) < hs && fabsf(mouse.y - br.y) < hs) m_dragCorner = 3;
        else if (mouse.x > tl.x && mouse.x < br.x && mouse.y > tl.y && mouse.y < br.y) {
            m_dragCorner = 4;
            m_dragStart = mouse;
            m_dragCropL = m_cropL; m_dragCropT = m_cropT;
            m_dragCropR = m_cropR; m_dragCropB = m_cropB;
        }
    }

    if (m_dragCorner >= 0 && ImGui::IsMouseDown(0)) {
        ImVec2 px = scr2px(mouse);
        int mx = std::clamp((int)px.x, 0, m_workW);
        int my = std::clamp((int)px.y, 0, m_workH);

        if (m_dragCorner == 0) { m_cropL = mx; m_cropT = my; }
        else if (m_dragCorner == 1) { m_cropR = mx; m_cropT = my; }
        else if (m_dragCorner == 2) { m_cropL = mx; m_cropB = my; }
        else if (m_dragCorner == 3) { m_cropR = mx; m_cropB = my; }
        else if (m_dragCorner == 4) {
            float dx = mouse.x - m_dragStart.x;
            float dy = mouse.y - m_dragStart.y;
            int dpx = (int)(dx / scale);
            int dpy = (int)(dy / scale);
            int cw = m_dragCropR - m_dragCropL;
            int ch = m_dragCropB - m_dragCropT;
            m_cropL = std::clamp(m_dragCropL + dpx, 0, m_workW - cw);
            m_cropT = std::clamp(m_dragCropT + dpy, 0, m_workH - ch);
            m_cropR = m_cropL + cw;
            m_cropB = m_cropT + ch;
        }
        clampCrop();
    }

    if (ImGui::IsMouseReleased(0)) m_dragCorner = -1;
}

void ImageEditor::clampCrop() {
    m_cropL = std::clamp(m_cropL, 0, m_workW);
    m_cropR = std::clamp(m_cropR, 0, m_workW);
    m_cropT = std::clamp(m_cropT, 0, m_workH);
    m_cropB = std::clamp(m_cropB, 0, m_workH);
    if (m_cropL > m_cropR) std::swap(m_cropL, m_cropR);
    if (m_cropT > m_cropB) std::swap(m_cropT, m_cropB);
}

void ImageEditor::resetCropAndResize() {
    m_cropL = 0; m_cropT = 0;
    m_cropR = m_workW; m_cropB = m_workH;
    m_resizeW = m_workW; m_resizeH = m_workH;
    m_dirty = true;
}

void ImageEditor::uploadPreview() {
    freePreview();
    if (m_work.empty() || !m_ctx || !m_bufMgr || !m_imgui) return;
    try {
        TextureManager texMgr;
        texMgr.init(*m_ctx, *m_bufMgr);
        m_previewTex  = texMgr.createFromPixels(*m_ctx, *m_bufMgr,
                                                 m_work.data(), m_workW, m_workH);
        m_previewDesc = m_imgui->addTexture(m_previewTex.view, m_previewTex.sampler);
    } catch (...) {
        std::cout << "[ImageEditor] Preview upload failed\n";
    }
}

void ImageEditor::freePreview() {
    if (m_previewTex.image != VK_NULL_HANDLE && m_ctx && m_bufMgr) {
        vkDeviceWaitIdle(m_ctx->device());
        vkDestroySampler(m_ctx->device(), m_previewTex.sampler, nullptr);
        vkDestroyImageView(m_ctx->device(), m_previewTex.view, nullptr);
        vmaDestroyImage(m_bufMgr->allocator(), m_previewTex.image,
                        m_previewTex.allocation);
        m_previewTex  = {};
        m_previewDesc = VK_NULL_HANDLE;
    }
}

void ImageEditor::freePixels() {
    if (m_pixels) { stbi_image_free(m_pixels); m_pixels = nullptr; }
}
