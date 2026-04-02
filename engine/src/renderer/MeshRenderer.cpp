#include "MeshRenderer.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/SyncObjects.h"
#include <array>
#include <cstring>

void MeshRenderer::init(VulkanContext& ctx, BufferManager& bufMgr,
                        DescriptorManager& descMgr, VkRenderPass renderPass,
                        const std::string& shaderDir) {
    m_ubos.resize(MAX_FRAMES_IN_FLIGHT);
    m_frameSets.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_ubos[i] = bufMgr.createDynamicBuffer(sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_frameSets[i] = descMgr.allocateFrameSet(ctx, m_ubos[i].handle, sizeof(FrameUBO));
    }

    // Push constants: model matrix (64) + tint (16) = 80 bytes
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.size       = sizeof(glm::mat4) + sizeof(glm::vec4);

    std::array<VkDescriptorSetLayout, 1> layouts = { descMgr.frameUBOLayout() };
    VkPipelineLayoutCreateInfo lci{};
    lci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount         = 1;
    lci.pSetLayouts            = layouts.data();
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(ctx.device(), &lci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create mesh pipeline layout");

    auto binding    = MeshVertex::binding();
    auto attributes = MeshVertex::attributes();

    PipelineConfig cfg{};
    cfg.renderPass       = renderPass;
    cfg.layout           = m_pipelineLayout;
    cfg.vertShaderPath   = shaderDir + "/mesh.vert.spv";
    cfg.fragShaderPath   = shaderDir + "/mesh.frag.spv";
    cfg.vertexBinding    = binding;
    cfg.vertexAttributes = {attributes.begin(), attributes.end()};
    cfg.depthTest        = true;
    cfg.depthWrite       = true;
    cfg.blend            = PipelineConfig::Blend::Alpha;
    m_pipeline.init(ctx, cfg);
}

Mesh MeshRenderer::createMesh(VulkanContext& ctx, BufferManager& bufMgr,
                               const std::vector<MeshVertex>& verts,
                               const std::vector<uint32_t>& indices) {
    Mesh mesh;
    mesh.indexCount  = static_cast<uint32_t>(indices.size());
    mesh.vertexBuffer = bufMgr.createDeviceBuffer(
        sizeof(MeshVertex) * verts.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    mesh.indexBuffer  = bufMgr.createDeviceBuffer(
        sizeof(uint32_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    bufMgr.uploadToBuffer(ctx, mesh.vertexBuffer, verts.data(), sizeof(MeshVertex) * verts.size());
    bufMgr.uploadToBuffer(ctx, mesh.indexBuffer,  indices.data(), sizeof(uint32_t) * indices.size());
    return mesh;
}

void MeshRenderer::destroyMesh(BufferManager& bufMgr, Mesh& mesh) {
    bufMgr.destroyBuffer(mesh.vertexBuffer);
    bufMgr.destroyBuffer(mesh.indexBuffer);
    mesh.indexCount = 0;
}

void MeshRenderer::drawMesh(const Mesh& mesh, const glm::mat4& model, glm::vec4 tint) {
    m_queue.push_back({&mesh, model, tint});
}

void MeshRenderer::flush(VkCommandBuffer cmd, int frameIndex) {
    if (m_queue.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_frameSets[frameIndex], 0, nullptr);

    for (auto& entry : m_queue) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &entry.mesh->vertexBuffer.handle, &offset);
        vkCmdBindIndexBuffer(cmd, entry.mesh->indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);

        // Push model + tint
        struct PC { glm::mat4 model; glm::vec4 tint; } pc{entry.model, entry.tint};
        vkCmdPushConstants(cmd, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(PC), &pc);

        vkCmdDrawIndexed(cmd, entry.mesh->indexCount, 1, 0, 0, 0);
    }

    m_queue.clear();
}

void MeshRenderer::updateFrameUBO(const glm::mat4& viewProj, float time,
                                   int frameIndex, BufferManager&) {
    FrameUBO ubo{};
    ubo.viewProj = viewProj;
    ubo.time     = time;
    memcpy(m_ubos[frameIndex].mapped, &ubo, sizeof(FrameUBO));
}

void MeshRenderer::shutdown(VulkanContext& ctx, BufferManager& bufMgr) {
    m_pipeline.shutdown(ctx);
    vkDestroyPipelineLayout(ctx.device(), m_pipelineLayout, nullptr);
    for (auto& b : m_ubos) bufMgr.destroyBuffer(b);
}
