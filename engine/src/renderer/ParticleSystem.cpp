#include "ParticleSystem.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/SyncObjects.h"
#include "RenderTypes.h"
#include <cstring>
#include <array>
#include <cmath>
#include <cstdlib>

static constexpr float TWO_PI = 6.28318530717958f;

void ParticleSystem::init(VulkanContext& ctx, BufferManager& bufMgr,
                          DescriptorManager& descMgr, VkRenderPass renderPass,
                          const std::string& shaderDir) {
    m_vertexBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_ubos.resize(MAX_FRAMES_IN_FLIGHT);
    m_frameSets.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_vertexBuffers[i] = bufMgr.createDynamicBuffer(
            sizeof(QuadVertex) * MAX_PARTICLES * 6, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        m_ubos[i] = bufMgr.createDynamicBuffer(sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_frameSets[i] = descMgr.allocateFrameSet(ctx, m_ubos[i].handle, sizeof(FrameUBO));
    }

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
        throw std::runtime_error("Failed to create particle pipeline layout");

    auto binding    = QuadVertex::binding();
    auto attributes = QuadVertex::attributes();

    PipelineConfig cfg{};
    cfg.renderPass       = renderPass;
    cfg.layout           = m_pipelineLayout;
    cfg.vertShaderPath   = shaderDir + "/quad.vert.spv";
    cfg.fragShaderPath   = shaderDir + "/quad.frag.spv";
    cfg.vertexBinding    = binding;
    cfg.vertexAttributes = {attributes.begin(), attributes.end()};
    cfg.topology         = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    cfg.blend            = PipelineConfig::Blend::Additive;
    m_pipeline.init(ctx, cfg);

    for (auto& p : m_pool) p.life = 0.f;
}

void ParticleSystem::emit(glm::vec2 pos, glm::vec2 vel, glm::vec4 color,
                          float size, float lifetime) {
    Particle& p = m_pool[m_head % MAX_PARTICLES];
    p.pos     = pos;
    p.vel     = vel;
    p.color   = color;
    p.size    = size;
    p.life    = lifetime;
    p.maxLife = lifetime;
    ++m_head;
}

void ParticleSystem::emitBurst(glm::vec2 pos, glm::vec4 color, int count,
                                float speed, float size, float lifetime) {
    for (int i = 0; i < count; ++i) {
        float angle = TWO_PI * i / count + ((rand() % 100) / 100.f - 0.5f) * 0.5f;
        float spd   = speed * (0.5f + (rand() % 100) / 100.f);
        glm::vec2 vel = {cosf(angle) * spd, sinf(angle) * spd};
        float sz = size * (0.5f + (rand() % 100) / 100.f);
        emit(pos, vel, color, sz, lifetime * (0.7f + (rand() % 100) / 300.f));
    }
}

void ParticleSystem::update(float dt) {
    for (auto& p : m_pool) {
        if (p.life <= 0.f) continue;
        p.life -= dt;
        p.pos  += p.vel * dt;
        p.vel  *= 0.92f;  // drag
        float t   = p.life / p.maxLife;
        p.color.a = t * t;  // quadratic fade
    }
}

void ParticleSystem::updateFrameUBO(const glm::mat4& viewProj, float time, int frameIndex) {
    FrameUBO ubo{};
    ubo.viewProj = viewProj;
    ubo.time     = time;
    memcpy(m_ubos[frameIndex].mapped, &ubo, sizeof(FrameUBO));
}

void ParticleSystem::flush(VkCommandBuffer cmd, int frameIndex,
                            VkDescriptorSet whiteTexSet) {
    std::vector<QuadVertex> verts;
    verts.reserve(MAX_PARTICLES * 6);

    for (auto& p : m_pool) {
        if (p.life <= 0.f) continue;
        float h = p.size * 0.5f;
        glm::vec2 tl = {p.pos.x - h, p.pos.y - h};
        glm::vec2 tr = {p.pos.x + h, p.pos.y - h};
        glm::vec2 br = {p.pos.x + h, p.pos.y + h};
        glm::vec2 bl = {p.pos.x - h, p.pos.y + h};

        auto v = [&](glm::vec2 pos, glm::vec2 uv) {
            QuadVertex qv{};
            qv.pos   = pos;
            qv.uv    = uv;
            qv.color = p.color;
            return qv;
        };
        // Triangle 1
        verts.push_back(v(tl, {0,0}));
        verts.push_back(v(tr, {1,0}));
        verts.push_back(v(bl, {0,1}));
        // Triangle 2
        verts.push_back(v(tr, {1,0}));
        verts.push_back(v(br, {1,1}));
        verts.push_back(v(bl, {0,1}));
    }

    if (verts.empty()) return;

    memcpy(m_vertexBuffers[frameIndex].mapped,
           verts.data(), sizeof(QuadVertex) * verts.size());

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle());

    VkBuffer vb = m_vertexBuffers[frameIndex].handle;
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_frameSets[frameIndex], 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 1, 1, &whiteTexSet, 0, nullptr);

    QuadPushConstants pc{};
    pc.tint        = {1.f, 1.f, 1.f, 1.f};
    pc.uvTransform = {0.f, 0.f, 1.f, 1.f};
    pc.model       = glm::mat4(1.f);
    vkCmdPushConstants(cmd, m_pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(QuadPushConstants), &pc);

    vkCmdDraw(cmd, static_cast<uint32_t>(verts.size()), 1, 0, 0);
}

void ParticleSystem::shutdown(VulkanContext& ctx, BufferManager& bufMgr) {
    m_pipeline.shutdown(ctx);
    vkDestroyPipelineLayout(ctx.device(), m_pipelineLayout, nullptr);
    for (auto& b : m_vertexBuffers) bufMgr.destroyBuffer(b);
    for (auto& b : m_ubos)         bufMgr.destroyBuffer(b);
}
