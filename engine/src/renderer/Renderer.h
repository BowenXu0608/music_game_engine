#pragma once
#include "PostProcess.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/Swapchain.h"
#include "vulkan/RenderPass.h"
#include "vulkan/BufferManager.h"
#include "vulkan/TextureManager.h"
#include "vulkan/DescriptorManager.h"
#include "vulkan/CommandManager.h"
#include "vulkan/SyncObjects.h"
#include "QuadBatch.h"
#include "LineBatch.h"
#include "MeshRenderer.h"
#include "ParticleSystem.h"
#include "Camera.h"
#include "Material.h"
#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>

class Renderer {
public:
    void init(GLFWwindow* window, const std::string& shaderDir,
              bool validation = false, bool vsync = true);
    void shutdown();

    // Called at start of frame — acquires swapchain image
    bool beginFrame();
    // Called at end of frame — submits and presents
    void endFrame();

    // Close swapchain render pass and submit (call after ImGui render)
    void finishFrame();

    // Resize — call from GLFW framebuffer size callback
    void onResize(GLFWwindow* window);

    // Batchers — game modes write to these
    QuadBatch&      quads()     { return m_quads; }
    LineBatch&      lines()     { return m_lines; }
    MeshRenderer&   meshes()    { return m_meshes; }
    ParticleSystem& particles() { return m_particles; }
    // Second particle instance bound to the SWAPCHAIN render pass, so its
    // particles draw on top of the ImGui UI (for button-tap feedback). Emit
    // into it from the UI layer; flushUiParticles() draws it after ImGui.
    ParticleSystem& uiParticles() { return m_uiParticles; }
    TextureManager& textures()   { return m_texMgr; }
    BufferManager&  buffers()    { return m_bufMgr; }
    PostProcess&    postProcess(){ return m_postProcess; }

    void setCamera(const Camera& cam) { m_camera = cam; }
    const Camera& camera() const      { return m_camera; }

    // Resolve a PBR material's texture *paths* into GPU views. baseColor loads
    // as sRGB, normal as linear. Empty/failed paths fall back to white /
    // flat-normal. No-op for SpecialEffect materials. Textures are cached by
    // path for the renderer's lifetime. Game-mode renderers call this in
    // onInit after resolveMaterial().
    void resolvePbrTextures(Material& m);

    VkCommandBuffer   currentCmd()   const { return m_currentCmd; }
    VulkanContext&    context()            { return m_ctx; }
    DescriptorManager& descriptors()      { return m_descMgr; }

    // White 1x1 fallback texture
    VkImageView whiteView()    const { return m_whiteTexture.view; }
    VkSampler   whiteSampler() const { return m_whiteTexture.sampler; }

    // Flat tangent-space-normal 1x1 fallback — bound at Set 1 binding 1 for
    // PBR materials that carry no normal map. Game-mode renderers use this as
    // the fallback when a material's normalTexPath is empty.
    VkImageView flatNormalView()    const { return m_flatNormalTexture.view; }
    VkSampler   flatNormalSampler() const { return m_flatNormalTexture.sampler; }

    uint32_t width()  const { return m_swapchain.extent().width; }
    uint32_t height() const { return m_swapchain.extent().height; }

    // Constrain rendering to a centered sub-rect (letterbox). Used by the
    // desktop test-game to render at the author's chosen aspect ratio; the
    // black scene clear outside the scissor becomes the letterbox bars.
    void setViewportOverride(int x, int y, int w, int h) {
        m_vpOverride = true;
        m_vpX = x; m_vpY = y; m_vpW = w; m_vpH = h;
    }
    void clearViewportOverride() { m_vpOverride = false; }

    VkRenderPass swapchainRenderPass() const { return m_renderPass.handle(); }
    VkImageView sceneImageView() const { return m_postProcess.sceneView(); }

    // Draw the UI particle instance into the still-open swapchain render pass
    // (call AFTER ImGui has been rendered, BEFORE finishFrame()). Uses a
    // screen-space ortho projection so emit positions are window pixels.
    void flushUiParticles();

private:
    void recordFrame(uint32_t imageIndex);
    void setViewportScissor(VkCommandBuffer cmd);

    bool m_vpOverride = false;
    int  m_vpX = 0, m_vpY = 0, m_vpW = 0, m_vpH = 0;

    VulkanContext    m_ctx;
    Swapchain        m_swapchain;
    RenderPass       m_renderPass;
    BufferManager    m_bufMgr;
    TextureManager   m_texMgr;
    DescriptorManager m_descMgr;
    CommandManager   m_cmdMgr;
    SyncObjects      m_sync;

    QuadBatch      m_quads;
    LineBatch      m_lines;
    MeshRenderer   m_meshes;
    ParticleSystem m_particles;
    ParticleSystem m_uiParticles;  // swapchain-pass instance (UI overlay)

    PostProcess m_postProcess;

    Camera   m_camera;
    Texture  m_whiteTexture;
    Texture  m_flatNormalTexture;
    VkDescriptorSet m_whiteTexSet = VK_NULL_HANDLE;

    // Preinstalled scene light (no UI). lightDir is the direction the light
    // travels (world space); shaders use L = -lightDir.
    glm::vec3 m_lightDir     = glm::normalize(glm::vec3(-0.3f, -0.6f, -0.5f));
    glm::vec3 m_lightColor   = glm::vec3(1.f, 1.f, 1.f);
    float     m_lightInten   = 3.0f;
    glm::vec3 m_ambientColor = glm::vec3(1.f, 1.f, 1.f);
    float     m_ambientInten = 0.25f;

    // PBR texture cache keyed by absolute path ("<path>" = sRGB baseColor,
    // "<path>|n" = linear normal-map variant). Owned for the renderer's
    // lifetime; freed in shutdown().
    std::unordered_map<std::string, Texture> m_pbrTexCache;

    VkCommandBuffer m_currentCmd    = VK_NULL_HANDLE;
    uint32_t        m_imageIndex    = 0;
    bool            m_framebufferResized = false;
    bool            m_vsync         = true;
    std::string     m_shaderDir;
    float           m_time = 0.f;
};
