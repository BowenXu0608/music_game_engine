#include "LineBatch.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/SyncObjects.h"
#include <glm/glm.hpp>
#include <cstring>
#include <array>

void LineBatch::init(VulkanContext& ctx, BufferManager& bufMgr,
                     DescriptorManager& descMgr, VkRenderPass renderPass,
                     const std::string& shaderDir) {
    m_vertexBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_ubos.resize(MAX_FRAMES_IN_FLIGHT);
    m_frameSets.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_vertexBuffers[i] = bufMgr.createDynamicBuffer(
            sizeof(QuadVertex) * MAX_LINES * 4, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        m_ubos[i] = bufMgr.createDynamicBuffer(
            sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_frameSets[i] = descMgr.allocateFrameSet(ctx, m_ubos[i].handle, sizeof(FrameUBO));
    }

    std::array<VkDescriptorSetLayout, 1> layouts = { descMgr.frameUBOLayout() };
    VkPipelineLayoutCreateInfo lci{};
    lci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount = 1;
    lci.pSetLayouts    = layouts.data();
    if (vkCreatePipelineLayout(ctx.device(), &lci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create line pipeline layout");

    auto binding    = QuadVertex::binding();
    auto attributes = QuadVertex::attributes();

    PipelineConfig cfg{};
    cfg.renderPass       = renderPass;
    cfg.layout           = m_pipelineLayout;
    cfg.vertShaderPath   = shaderDir + "/line.vert.spv";
    cfg.fragShaderPath   = shaderDir + "/line.frag.spv";
    cfg.vertexBinding    = binding;
    cfg.vertexAttributes = {attributes.begin(), attributes.end()};
    cfg.topology         = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    cfg.blend            = PipelineConfig::Blend::Alpha;
    m_pipeline.init(ctx, cfg);
}

void LineBatch::expandLine(glm::vec2 a, glm::vec2 b, float width, glm::vec4 color) {
    glm::vec2 dir = b - a;
    float len = glm::length(dir);
    if (len < 1e-6f) return;
    glm::vec2 perp = glm::vec2(-dir.y, dir.x) / len * (width * 0.5f);

    // 4 corners: top-left, top-right, bottom-right, bottom-left
    glm::vec2 tl = a - perp, tr = a + perp;
    glm::vec2 br = b + perp, bl = b - perp;

    // Emit 6 vertices (2 triangles) — no index buffer needed
    auto emit = [&](glm::vec2 p) {
        QuadVertex v{};
        v.pos   = p;
        v.uv    = {0.f, 0.f};
        v.color = color;
        m_vertices.push_back(v);
    };
    emit(tl); emit(tr); emit(bl);  // triangle 1
    emit(tr); emit(br); emit(bl);  // triangle 2
}

void LineBatch::drawLine(glm::vec2 a, glm::vec2 b, float width, glm::vec4 color) {
    expandLine(a, b, width, color);
}

void LineBatch::drawPolyline(const std::vector<glm::vec2>& points, float width,
                              glm::vec4 color, bool closed) {
    if (points.size() < 2) return;
    for (size_t i = 0; i + 1 < points.size(); ++i)
        expandLine(points[i], points[i+1], width, color);
    if (closed && points.size() > 2)
        expandLine(points.back(), points.front(), width, color);
}

void LineBatch::flush(VkCommandBuffer cmd, int frameIndex) {
    if (m_vertices.empty()) return;

    memcpy(m_vertexBuffers[frameIndex].mapped,
           m_vertices.data(), sizeof(QuadVertex) * m_vertices.size());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle());

    VkBuffer vb = m_vertexBuffers[frameIndex].handle;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_frameSets[frameIndex], 0, nullptr);

    vkCmdDraw(cmd, static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);

    m_vertices.clear();
}

void LineBatch::updateFrameUBO(const glm::mat4& viewProj, float time, int frameIndex) {
    FrameUBO ubo{};
    ubo.viewProj = viewProj;
    ubo.time     = time;
    memcpy(m_ubos[frameIndex].mapped, &ubo, sizeof(FrameUBO));
}

void LineBatch::shutdown(VulkanContext& ctx, BufferManager& bufMgr) {
    m_pipeline.shutdown(ctx);
    vkDestroyPipelineLayout(ctx.device(), m_pipelineLayout, nullptr);
    for (auto& b : m_vertexBuffers) bufMgr.destroyBuffer(b);
    for (auto& b : m_ubos)         bufMgr.destroyBuffer(b);
}
