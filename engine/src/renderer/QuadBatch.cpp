#include "QuadBatch.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/SyncObjects.h"
#include "RenderTypes.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>
#include <stdexcept>
#include <array>

namespace {

// All material kinds that have a backing fragment shader.
constexpr std::array<MaterialKind, 5> kAllKinds = {
    MaterialKind::Unlit,
    MaterialKind::Glow,
    MaterialKind::Scroll,
    MaterialKind::Pulse,
    MaterialKind::Gradient,
};

const char* shaderNameForKind(MaterialKind k) {
    switch (k) {
        case MaterialKind::Unlit:    return "quad_unlit";
        case MaterialKind::Glow:     return "quad_glow";
        case MaterialKind::Scroll:   return "quad_scroll";
        case MaterialKind::Pulse:    return "quad_pulse";
        case MaterialKind::Gradient: return "quad_gradient";
        default:                     return "quad_unlit";
    }
}

} // namespace

void QuadBatch::init(VulkanContext& ctx, BufferManager& bufMgr,
                     DescriptorManager& descMgr, VkRenderPass renderPass,
                     const std::string& shaderDir) {
    m_vertexBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_ubos.resize(MAX_FRAMES_IN_FLIGHT);
    m_frameSets.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_vertexBuffers[i] = bufMgr.createDynamicBuffer(
            sizeof(QuadVertex) * QUAD_VERTS, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        m_ubos[i] = bufMgr.createDynamicBuffer(
            sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_frameSets[i] = descMgr.allocateFrameSet(ctx, m_ubos[i].handle, sizeof(FrameUBO));
    }

    buildIndexBuffer(ctx, bufMgr);

    // Shared pipeline layout across all material kinds.
    std::array<VkDescriptorSetLayout, 2> layouts = {
        descMgr.frameUBOLayout(), descMgr.textureLayout()
    };
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(QuadPushConstants);

    VkPipelineLayoutCreateInfo lci{};
    lci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount         = static_cast<uint32_t>(layouts.size());
    lci.pSetLayouts            = layouts.data();
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(ctx.device(), &lci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create quad pipeline layout");

    auto binding    = QuadVertex::binding();
    auto attributes = QuadVertex::attributes();

    // Create one pipeline per kind. Shared vertex shader, per-kind frag.
    for (MaterialKind k : kAllKinds) {
        PipelineConfig cfg{};
        cfg.renderPass       = renderPass;
        cfg.layout           = m_pipelineLayout;
        cfg.vertShaderPath   = shaderDir + "/quad.vert.spv";
        cfg.fragShaderPath   = shaderDir + "/" + shaderNameForKind(k) + ".frag.spv";
        cfg.vertexBinding    = binding;
        cfg.vertexAttributes = {attributes.begin(), attributes.end()};
        cfg.blend            = PipelineConfig::Blend::Alpha;
        m_pipelines[(size_t)k].init(ctx, cfg);
    }
}

void QuadBatch::buildIndexBuffer(VulkanContext& ctx, BufferManager& bufMgr) {
    std::vector<uint32_t> indices(QUAD_INDICES);
    for (uint32_t i = 0; i < MAX_QUADS; ++i) {
        uint32_t v = i * 4;
        uint32_t idx = i * 6;
        indices[idx+0] = v+0; indices[idx+1] = v+1; indices[idx+2] = v+2;
        indices[idx+3] = v+2; indices[idx+4] = v+3; indices[idx+5] = v+0;
    }
    m_indexBuffer = bufMgr.createDeviceBuffer(
        sizeof(uint32_t) * QUAD_INDICES, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    bufMgr.uploadToBuffer(ctx, m_indexBuffer, indices.data(),
                          sizeof(uint32_t) * QUAD_INDICES);
}

void QuadBatch::pushBatch(const Material& mat, glm::vec4 uvTransform,
                          uint32_t quadIdx,
                          VulkanContext& ctx, DescriptorManager& descMgr) {
    // Per-vertex tint + uvTransform are baked into vertices, so batching only
    // breaks on (kind, texture, params) change. This matches the pre-material
    // batching efficiency.
    (void)uvTransform;
    bool canCoalesce =
        !m_batches.empty() &&
        m_batches.back().kind    == mat.kind &&
        m_batches.back().texture == mat.texture &&
        m_batches.back().sampler == mat.sampler &&
        m_batches.back().params  == mat.params;

    if (canCoalesce) {
        m_batches.back().indexCount += 6;
        return;
    }

    Batch b{};
    b.kind        = mat.kind;
    b.texture     = mat.texture;
    b.sampler     = mat.sampler;
    b.tint        = glm::vec4(1.f);        // unused — per-vertex tint is live
    b.params      = mat.params;
    b.uvTransform = glm::vec4(0,0,1,1);    // unused — per-vertex UV is live
    b.indexStart  = quadIdx * 6;
    b.indexCount  = 6;

    auto it = m_texSetCache.find(mat.texture);
    if (it == m_texSetCache.end()) {
        b.texSet = descMgr.allocateTextureSet(ctx, mat.texture, mat.sampler);
        m_texSetCache[mat.texture] = b.texSet;
    } else {
        b.texSet = it->second;
    }
    m_batches.push_back(b);
}

void QuadBatch::drawQuad(glm::vec2 pos, glm::vec2 size, float rotation,
                         const Material& mat, glm::vec4 uvTransform,
                         VulkanContext& ctx, DescriptorManager& descMgr) {
    if (m_vertices.size() / 4 >= MAX_QUADS) return;

    float hw = size.x * 0.5f, hh = size.y * 0.5f;
    glm::vec2 corners[4] = {{-hw,-hh},{hw,-hh},{hw,hh},{-hw,hh}};
    glm::vec2 uvs[4] = {{0,0},{1,0},{1,1},{0,1}};

    float c = cosf(rotation), s = sinf(rotation);
    for (int i = 0; i < 4; ++i) {
        glm::vec2 r = {corners[i].x*c - corners[i].y*s,
                       corners[i].x*s + corners[i].y*c};
        QuadVertex v{};
        v.pos   = pos + r;
        v.uv    = uvs[i] * glm::vec2(uvTransform.z, uvTransform.w)
                         + glm::vec2(uvTransform.x, uvTransform.y);
        v.color = mat.tint;
        m_vertices.push_back(v);
    }

    uint32_t quadIdx = static_cast<uint32_t>(m_vertices.size() / 4) - 1;
    pushBatch(mat, uvTransform, quadIdx, ctx, descMgr);
}

void QuadBatch::drawQuadCorners(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                                const Material& mat, glm::vec4 uvTransform,
                                VulkanContext& ctx, DescriptorManager& descMgr) {
    if (m_vertices.size() / 4 >= MAX_QUADS) return;

    glm::vec2 corners[4] = {p0, p1, p2, p3};
    glm::vec2 uvs[4]     = {{0,0},{1,0},{1,1},{0,1}};

    for (int i = 0; i < 4; ++i) {
        QuadVertex v{};
        v.pos   = corners[i];
        v.uv    = uvs[i] * glm::vec2(uvTransform.z, uvTransform.w)
                         + glm::vec2(uvTransform.x, uvTransform.y);
        v.color = mat.tint;
        m_vertices.push_back(v);
    }

    uint32_t quadIdx = static_cast<uint32_t>(m_vertices.size() / 4) - 1;
    pushBatch(mat, uvTransform, quadIdx, ctx, descMgr);
}

// ── Legacy overloads — forward as Unlit ─────────────────────────────────────

void QuadBatch::drawQuad(glm::vec2 pos, glm::vec2 size, float rotation,
                         glm::vec4 color, glm::vec4 uvTransform,
                         VkImageView texture, VkSampler sampler,
                         VulkanContext& ctx, DescriptorManager& descMgr) {
    Material mat;
    mat.kind    = MaterialKind::Unlit;
    mat.tint    = color;
    mat.texture = texture;
    mat.sampler = sampler;
    drawQuad(pos, size, rotation, mat, uvTransform, ctx, descMgr);
}

void QuadBatch::drawQuadCorners(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                                glm::vec4 color, glm::vec4 uvTransform,
                                VkImageView texture, VkSampler sampler,
                                VulkanContext& ctx, DescriptorManager& descMgr) {
    Material mat;
    mat.kind    = MaterialKind::Unlit;
    mat.tint    = color;
    mat.texture = texture;
    mat.sampler = sampler;
    drawQuadCorners(p0, p1, p2, p3, mat, uvTransform, ctx, descMgr);
}

// ────────────────────────────────────────────────────────────────────────────

void QuadBatch::flush(VkCommandBuffer cmd, VulkanContext& ctx, DescriptorManager& descMgr) {
    if (m_vertices.empty()) return;

    memcpy(m_vertexBuffers[m_currentFrame].mapped,
           m_vertices.data(), sizeof(QuadVertex) * m_vertices.size());

    VkBuffer vb = m_vertexBuffers[m_currentFrame].handle;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_frameSets[m_currentFrame], 0, nullptr);

    MaterialKind    lastKind   = MaterialKind::Count;   // force first bind
    VkDescriptorSet lastTexSet = VK_NULL_HANDLE;

    for (auto& batch : m_batches) {
        if (batch.kind != lastKind) {
            VkPipeline pipe = m_pipelines[(size_t)batch.kind].handle();
            // Fall back to Unlit if a kind isn't initialized yet (Phase 1).
            if (pipe == VK_NULL_HANDLE)
                pipe = m_pipelines[(size_t)MaterialKind::Unlit].handle();
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            lastKind = batch.kind;
        }

        QuadPushConstants pc{};
        pc.model       = glm::mat4(1.f);
        pc.tint        = glm::vec4(1.f);           // per-vertex tint is live
        pc.uvTransform = glm::vec4(0.f, 0.f, 1.f, 1.f);
        pc.params      = batch.params;
        pc.kind        = (uint32_t)batch.kind;
        vkCmdPushConstants(cmd, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(QuadPushConstants), &pc);

        if (batch.texSet != lastTexSet) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 1, 1, &batch.texSet, 0, nullptr);
            lastTexSet = batch.texSet;
        }
        vkCmdDrawIndexed(cmd, batch.indexCount, 1, batch.indexStart, 0, 0);
    }

    m_vertices.clear();
    m_batches.clear();
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void QuadBatch::updateFrameUBO(const glm::mat4& viewProj, float time, int frameIndex) {
    FrameUBO ubo{};
    ubo.viewProj = viewProj;
    ubo.time     = time;
    memcpy(m_ubos[frameIndex].mapped, &ubo, sizeof(FrameUBO));
}

void QuadBatch::shutdown(VulkanContext& ctx, BufferManager& bufMgr) {
    for (auto& p : m_pipelines) p.shutdown(ctx);
    vkDestroyPipelineLayout(ctx.device(), m_pipelineLayout, nullptr);
    for (auto& b : m_vertexBuffers) bufMgr.destroyBuffer(b);
    for (auto& b : m_ubos)         bufMgr.destroyBuffer(b);
    bufMgr.destroyBuffer(m_indexBuffer);
}
