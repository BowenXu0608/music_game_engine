#include "Renderer.h"
#include <stdexcept>
#include <array>

void Renderer::resolvePbrTextures(Material& m) {
    if (m.cls != MaterialClass::Pbr) return;

    auto loadCached = [&](const std::string& path, bool srgb,
                          VkImageView& outView, VkSampler& outSamp,
                          VkImageView fbView, VkSampler fbSamp) {
        if (path.empty()) { outView = fbView; outSamp = fbSamp; return; }
        std::string key = srgb ? path : (path + "|n");
        auto it = m_pbrTexCache.find(key);
        if (it == m_pbrTexCache.end()) {
            try {
                Texture t = m_texMgr.loadFromFile(m_ctx, m_bufMgr, path, srgb);
                it = m_pbrTexCache.emplace(key, t).first;
            } catch (...) {
                // Missing/unreadable file → fall back, don't crash gameplay.
                outView = fbView; outSamp = fbSamp; return;
            }
        }
        outView = it->second.view;
        outSamp = it->second.sampler;
    };

    loadCached(m.baseColorTexPath, /*srgb=*/true,
               m.baseColorTex, m.baseColorSamp,
               m_whiteTexture.view, m_whiteTexture.sampler);
    loadCached(m.normalTexPath, /*srgb=*/false,
               m.normalTex, m.normalSamp,
               m_flatNormalTexture.view, m_flatNormalTexture.sampler);
}

void Renderer::init(GLFWwindow* window, const std::string& shaderDir, bool validation, bool vsync) {
    m_shaderDir = shaderDir;
    m_vsync     = vsync;

    m_ctx.init(window, validation);
    m_swapchain.init(m_ctx, window, vsync);
    m_renderPass.init(m_ctx, m_swapchain.imageFormat());
    m_swapchain.createFramebuffers(m_ctx, m_renderPass.handle());
    m_bufMgr.init(m_ctx);
    m_texMgr.init(m_ctx, m_bufMgr);
    m_descMgr.init(m_ctx);
    m_cmdMgr.init(m_ctx, MAX_FRAMES_IN_FLIGHT);
    m_sync.init(m_ctx);

    m_whiteTexture      = m_texMgr.createWhite1x1(m_ctx, m_bufMgr);
    m_flatNormalTexture = m_texMgr.createFlatNormal1x1(m_ctx, m_bufMgr);
    m_whiteTexSet  = m_descMgr.allocateTextureSet(m_ctx, m_whiteTexture.view, m_whiteTexture.sampler);

    auto ext = m_swapchain.extent();
    m_postProcess.init(m_ctx, ext.width, ext.height,
                       VK_FORMAT_R16G16B16A16_SFLOAT, m_renderPass.handle(), shaderDir);

    // Batchers use the scene render pass, not the swapchain render pass
    m_quads.init(m_ctx, m_bufMgr, m_descMgr, m_postProcess.sceneRenderPass(), shaderDir,
                 m_whiteTexture.view, m_whiteTexture.sampler,
                 m_flatNormalTexture.view, m_flatNormalTexture.sampler);
    m_lines.init(m_ctx, m_bufMgr, m_descMgr, m_postProcess.sceneRenderPass(), shaderDir);
    m_meshes.init(m_ctx, m_bufMgr, m_descMgr, m_postProcess.sceneRenderPass(), shaderDir,
                  m_whiteTexture.view, m_whiteTexture.sampler,
                  m_flatNormalTexture.view, m_flatNormalTexture.sampler);
    m_particles.init(m_ctx, m_bufMgr, m_descMgr, m_postProcess.sceneRenderPass(), shaderDir);

    // Default ortho camera
    m_camera = Camera::makeOrtho(0.f, static_cast<float>(ext.width),
                                  static_cast<float>(ext.height), 0.f);
}

void Renderer::shutdown() {
    vkDeviceWaitIdle(m_ctx.device());
    m_texMgr.destroyTexture(m_ctx, m_whiteTexture);
    m_texMgr.destroyTexture(m_ctx, m_flatNormalTexture);
    for (auto& [_, t] : m_pbrTexCache) m_texMgr.destroyTexture(m_ctx, t);
    m_pbrTexCache.clear();
    m_quads.shutdown(m_ctx, m_bufMgr);
    m_lines.shutdown(m_ctx, m_bufMgr);
    m_meshes.shutdown(m_ctx, m_bufMgr);
    m_particles.shutdown(m_ctx, m_bufMgr);
    m_postProcess.shutdown(m_ctx);
    m_sync.shutdown(m_ctx);
    m_cmdMgr.shutdown(m_ctx);
    m_descMgr.shutdown(m_ctx);
    m_texMgr.shutdown(m_ctx);
    m_bufMgr.shutdown();
    m_swapchain.shutdown(m_ctx);
    m_renderPass.shutdown(m_ctx);
    m_ctx.shutdown();
}

bool Renderer::beginFrame() {
    int frame = m_sync.currentFrame();
    vkWaitForFences(m_ctx.device(), 1, &m_sync.inFlight(frame), VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        m_ctx.device(), m_swapchain.handle(), UINT64_MAX,
        m_sync.imageAvailable(frame), VK_NULL_HANDLE, &m_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    vkResetFences(m_ctx.device(), 1, &m_sync.inFlight(frame));

    m_currentCmd = m_cmdMgr.begin(frame);

    // Begin scene render pass (offscreen, owned by PostProcess)
    VkClearValue clearVal{};
    clearVal.color = {{0.f, 0.f, 0.f, 1.f}};
    VkRenderPassBeginInfo rpbi{};
    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass      = m_postProcess.sceneRenderPass();
    rpbi.framebuffer     = m_postProcess.sceneFramebuffer();
    rpbi.renderArea      = {{0,0}, m_swapchain.extent()};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues    = &clearVal;
    vkCmdBeginRenderPass(m_currentCmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    setViewportScissor(m_currentCmd);
    return true;
}

void Renderer::endFrame() {
    int frame = m_sync.currentFrame();

    // Build the shared per-frame UBO now — after the game mode called
    // setCamera(). All batchers get the same struct (the preinstalled light
    // travels with it so PBR materials are lit without self-illumination).
    FrameUBO ubo{};
    ubo.viewProj   = m_camera.viewProjection();
    ubo.cameraPos  = glm::vec4(m_camera.eyePosition(), m_time);
    ubo.lightDir   = glm::vec4(m_lightDir, 0.f);
    ubo.lightColor = glm::vec4(m_lightColor, m_lightInten);
    ubo.ambient    = glm::vec4(m_ambientColor, m_ambientInten);

    m_quads.updateFrameUBO(ubo, frame);
    m_lines.updateFrameUBO(ubo, frame);
    m_meshes.updateFrameUBO(ubo, frame, m_bufMgr);
    m_particles.updateFrameUBO(ubo, frame);

    // Flush all batchers
    m_quads.flush(m_currentCmd, m_ctx, m_descMgr);
    m_lines.flush(m_currentCmd, frame);
    m_meshes.flush(m_currentCmd, frame);
    m_particles.flush(m_currentCmd, frame, m_whiteTexSet);

    vkCmdEndRenderPass(m_currentCmd);

    // Bloom compute (scene finalLayout already SHADER_READ_ONLY_OPTIMAL)
    m_postProcess.render(m_currentCmd, m_ctx,
                         m_postProcess.sceneView(), VK_NULL_HANDLE);

    // Begin swapchain render pass with clear (no composite - ImGui will show scene texture)
    VkClearValue clearVal{};
    clearVal.color = {{0.1f, 0.1f, 0.1f, 1.f}};
    VkRenderPassBeginInfo rpbi{};
    rpbi.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass      = m_renderPass.handle();
    rpbi.framebuffer     = m_swapchain.framebuffer(m_imageIndex);
    rpbi.renderArea      = {{0,0}, m_swapchain.extent()};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues    = &clearVal;
    vkCmdBeginRenderPass(m_currentCmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    setViewportScissor(m_currentCmd);

    // Swapchain render pass stays open for ImGui (no composite drawn)
}

void Renderer::finishFrame() {
    int frame = m_sync.currentFrame();

    m_renderPass.end(m_currentCmd);
    m_cmdMgr.end(frame);

    // Submit
    VkSemaphore waitSems[]   = { m_sync.imageAvailable(frame) };
    VkSemaphore signalSems[] = { m_sync.renderFinished(frame) };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = waitSems;
    si.pWaitDstStageMask    = waitStages;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &m_currentCmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = signalSems;

    vkQueueSubmit(m_ctx.graphicsQueue(), 1, &si, m_sync.inFlight(frame));

    // Present
    VkSwapchainKHR swapchains[] = { m_swapchain.handle() };
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = signalSems;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = swapchains;
    pi.pImageIndices      = &m_imageIndex;

    VkResult result = vkQueuePresentKHR(m_ctx.presentQueue(), &pi);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        m_framebufferResized = true;

    m_sync.advance();
    m_time += 0.016f;
}

void Renderer::onResize(GLFWwindow* window) {
    vkDeviceWaitIdle(m_ctx.device());
    m_swapchain.recreate(m_ctx, window, m_vsync);
    m_swapchain.createFramebuffers(m_ctx, m_renderPass.handle());
    auto ext = m_swapchain.extent();
    m_postProcess.resize(m_ctx, ext.width, ext.height);
    m_camera = Camera::makeOrtho(0.f, static_cast<float>(ext.width),
                                  static_cast<float>(ext.height), 0.f);
}

void Renderer::setViewportScissor(VkCommandBuffer cmd) {
    auto ext = m_swapchain.extent();
    VkViewport vp{};
    vp.x        = 0.f;
    vp.y        = 0.f;
    vp.width    = static_cast<float>(ext.width);
    vp.height   = static_cast<float>(ext.height);
    vp.minDepth = 0.f;
    vp.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{{0,0}, ext};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}
