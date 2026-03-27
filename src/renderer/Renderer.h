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
#include <GLFW/glfw3.h>
#include <string>

class Renderer {
public:
    void init(GLFWwindow* window, const std::string& shaderDir,
              bool validation = false, bool vsync = true);
    void shutdown();

    // Called at start of frame — acquires swapchain image
    bool beginFrame();
    // Called at end of frame — submits and presents
    void endFrame();

    // Render ImGui (call before endFrame, after game mode render)
    void renderImGui(VkCommandBuffer cmd);

    // Resize — call from GLFW framebuffer size callback
    void onResize(GLFWwindow* window);

    // Batchers — game modes write to these
    QuadBatch&      quads()     { return m_quads; }
    LineBatch&      lines()     { return m_lines; }
    MeshRenderer&   meshes()    { return m_meshes; }
    ParticleSystem& particles() { return m_particles; }
    TextureManager& textures()   { return m_texMgr; }
    BufferManager&  buffers()    { return m_bufMgr; }
    PostProcess&    postProcess(){ return m_postProcess; }

    void setCamera(const Camera& cam) { m_camera = cam; }
    const Camera& camera() const      { return m_camera; }

    VkCommandBuffer   currentCmd()   const { return m_currentCmd; }
    VulkanContext&    context()            { return m_ctx; }
    DescriptorManager& descriptors()      { return m_descMgr; }

    // White 1x1 fallback texture
    VkImageView whiteView()    const { return m_whiteTexture.view; }
    VkSampler   whiteSampler() const { return m_whiteTexture.sampler; }

    uint32_t width()  const { return m_swapchain.extent().width; }
    uint32_t height() const { return m_swapchain.extent().height; }

    VkRenderPass swapchainRenderPass() const { return m_renderPass.handle(); }
    VkImageView sceneImageView() const { return m_postProcess.sceneView(); }

private:
    void recordFrame(uint32_t imageIndex);
    void setViewportScissor(VkCommandBuffer cmd);

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

    PostProcess m_postProcess;

    Camera   m_camera;
    Texture  m_whiteTexture;
    VkDescriptorSet m_whiteTexSet = VK_NULL_HANDLE;

    VkCommandBuffer m_currentCmd    = VK_NULL_HANDLE;
    uint32_t        m_imageIndex    = 0;
    bool            m_framebufferResized = false;
    bool            m_vsync         = true;
    std::string     m_shaderDir;
    float           m_time = 0.f;
};
