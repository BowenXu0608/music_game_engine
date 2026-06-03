#include "ParticleSystem.h"
#include "ShaderCompiler.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/SyncObjects.h"
#include "RenderTypes.h"
#include <cstring>
#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <algorithm>

static constexpr float TWO_PI = 6.28318530717958f;

static inline float frand() { return (rand() % 1000) / 1000.f; }            // 0..1
static inline float frange(float a, float b) { return a + (b - a) * frand(); }

static PipelineConfig particlePipelineConfig(VkRenderPass rp, VkPipelineLayout layout,
                                             const std::string& vertSpv,
                                             const std::string& fragSpv,
                                             const VkVertexInputBindingDescription& binding,
                                             const std::array<VkVertexInputAttributeDescription, 4>& attrs) {
    PipelineConfig cfg{};
    cfg.renderPass       = rp;
    cfg.layout           = layout;
    cfg.vertShaderPath   = vertSpv;
    cfg.fragShaderPath   = fragSpv;
    cfg.vertexBinding    = binding;
    cfg.vertexAttributes = {attrs.begin(), attrs.end()};
    cfg.topology         = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    cfg.blend            = PipelineConfig::Blend::Additive;
    return cfg;
}

void ParticleSystem::init(VulkanContext& ctx, BufferManager& bufMgr,
                          DescriptorManager& descMgr, VkRenderPass renderPass,
                          const std::string& shaderDir) {
    m_ctx        = &ctx;
    m_renderPass = renderPass;
    m_shaderDir  = shaderDir;

    m_vertexBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_ubos.resize(MAX_FRAMES_IN_FLIGHT);
    m_frameSets.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_vertexBuffers[i] = bufMgr.createDynamicBuffer(
            sizeof(ParticleVertex) * MAX_PARTICLES * 6, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
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

    auto binding = ParticleVertex::binding();
    auto attrs   = ParticleVertex::attributes();
    PipelineConfig cfg = particlePipelineConfig(
        renderPass, m_pipelineLayout,
        shaderDir + "/particle.vert.spv", shaderDir + "/particle.frag.spv",
        binding, attrs);
    m_builtinPipeline.init(ctx, cfg);

    for (auto& p : m_pool) p.life = 0.f;
}

void ParticleSystem::emit(glm::vec2 pos, glm::vec2 vel, glm::vec4 color,
                          float size, float lifetime) {
    Particle& p = m_pool[m_head % MAX_PARTICLES];
    p.pos       = pos;
    p.vel       = vel;
    p.accel     = {0.f, 0.f};
    p.color     = color;
    p.colorEnd  = {color.r, color.g, color.b, 0.f};   // fade to transparent
    p.sizeStart = size;
    p.sizeEnd   = size;
    p.rot       = 0.f;
    p.angVel    = 0.f;
    p.life      = lifetime;
    p.maxLife   = lifetime;
    p.seed      = frand();
    p.drag      = 0.92f;
    p.pipeKey   = 0;
    ++m_head;
}

void ParticleSystem::spawnOne(glm::vec2 pos, glm::vec2 vel, const ParticleEmit& fx, float t) {
    Particle& p = m_pool[m_head % MAX_PARTICLES];
    float life  = frange(fx.lifeMin, fx.lifeMax);
    p.pos       = pos;
    p.vel       = vel;
    p.accel     = fx.gravity;
    p.color     = fx.color;
    p.colorEnd  = fx.colorEnd;
    p.sizeStart = fx.sizeStart;
    p.sizeEnd   = fx.sizeEnd;
    p.rot       = frand() * TWO_PI;
    p.angVel    = (frand() - 0.5f) * 6.f;
    p.life      = life;
    p.maxLife   = life;
    p.seed      = frand();
    p.drag      = fx.drag;
    p.pipeKey   = fx.pipeKey;
    ++m_head;
}

void ParticleSystem::emitBurst(glm::vec2 pos, glm::vec4 color, int count,
                                float speed, float size, float lifetime) {
    ParticleEmit fx{};
    fx.pattern   = ParticleEmit::Pattern::Radial;
    fx.count     = count;
    fx.speedMin  = speed * 0.5f;
    fx.speedMax  = speed * 1.5f;
    fx.sizeStart = size;
    fx.sizeEnd   = size;                       // legacy: no size animation
    fx.lifeMin   = lifetime * 0.7f;
    fx.lifeMax   = lifetime;
    fx.color     = color;
    fx.colorEnd  = {color.r, color.g, color.b, 0.f};
    emitEffect(fx, pos);
}

void ParticleSystem::emitEffect(const ParticleEmit& fx, glm::vec2 pos, glm::vec2 dir) {
    int n = std::max(1, fx.count);
    float baseAngle = std::atan2(dir.y, dir.x);
    for (int i = 0; i < n; ++i) {
        float angle;
        switch (fx.pattern) {
            case ParticleEmit::Pattern::Radial:
            case ParticleEmit::Pattern::Ring:
                angle = TWO_PI * i / n + (frand() - 0.5f) * 0.5f;
                break;
            case ParticleEmit::Pattern::Directional:
                angle = baseAngle + (frand() - 0.5f) * fx.spread;
                break;
            case ParticleEmit::Pattern::Aura:
                angle = baseAngle + (frand() - 0.5f) * fx.spread;
                break;
        }
        float spd = (fx.pattern == ParticleEmit::Pattern::Ring)
                        ? fx.speedMax                          // uniform expanding ring
                        : frange(fx.speedMin, fx.speedMax);
        glm::vec2 vel{std::cos(angle) * spd, std::sin(angle) * spd};
        spawnOne(pos, vel, fx, 0.f);
    }
}

void ParticleSystem::emitSustained(uint32_t id, const ParticleEmit& fx,
                                   glm::vec2 pos, float dt) {
    float& acc = m_sustainAccum[id];
    acc += fx.rateHz * dt;
    int spawns = static_cast<int>(acc);
    if (spawns <= 0) return;
    acc -= static_cast<float>(spawns);
    spawns = std::min(spawns, 16);   // clamp burst after a long frame
    float baseAngle = std::atan2(-1.f, 0.f);   // upward in screen space
    for (int i = 0; i < spawns; ++i) {
        float angle = baseAngle + (frand() - 0.5f) * fx.spread;
        float spd   = frange(fx.speedMin, fx.speedMax);
        glm::vec2 jitter{(frand() - 0.5f) * 18.f, (frand() - 0.5f) * 18.f};
        glm::vec2 vel{std::cos(angle) * spd, std::sin(angle) * spd};
        spawnOne(pos + jitter, vel, fx, 0.f);
    }
}

uint16_t ParticleSystem::registerCustomPipeline(const std::string& fragPath,
                                                std::string* errorOut) {
    auto it = m_customPipeKeys.find(fragPath);
    if (it != m_customPipeKeys.end()) return it->second;
    if (!m_ctx) { if (errorOut) *errorOut = "particle system not initialized"; return 0; }

    ShaderCompileResult compile = compileFragmentToSpv(fragPath);
    if (!compile.ok) {
        if (errorOut) *errorOut = compile.errorLog;
        return 0;
    }

    auto binding = ParticleVertex::binding();
    auto attrs   = ParticleVertex::attributes();
    PipelineConfig cfg = particlePipelineConfig(
        m_renderPass, m_pipelineLayout,
        m_shaderDir + "/particle.vert.spv", compile.spvPath, binding, attrs);

    Pipeline pipe;
    try {
        pipe.init(*m_ctx, cfg);
    } catch (const std::exception& e) {
        if (errorOut) *errorOut = e.what();
        return 0;
    }
    uint16_t key = m_nextPipeKey++;
    m_customPipelines.emplace(key, std::move(pipe));
    m_customPipeKeys.emplace(fragPath, key);
    if (errorOut) *errorOut = compile.errorLog;   // may carry glslc warnings
    return key;
}

void ParticleSystem::update(float dt) {
    for (auto& p : m_pool) {
        if (p.life <= 0.f) continue;
        p.life -= dt;
        if (p.life <= 0.f) continue;
        p.vel  += p.accel * dt;
        p.pos  += p.vel * dt;
        p.vel  *= p.drag;
        p.rot  += p.angVel * dt;
    }
}

void ParticleSystem::updateFrameUBO(const FrameUBO& ubo, int frameIndex) {
    memcpy(m_ubos[frameIndex].mapped, &ubo, sizeof(FrameUBO));
}

void ParticleSystem::flush(VkCommandBuffer cmd, int frameIndex,
                            VkDescriptorSet whiteTexSet) {
    // Bucket particles by pipeKey so each pipeline draws a contiguous range of
    // the single dynamic vertex buffer. Built-in (key 0) first.
    struct Range { uint16_t pipeKey; uint32_t first; uint32_t count; };
    std::vector<ParticleVertex> verts;
    verts.reserve(MAX_PARTICLES * 6);
    std::vector<Range> ranges;

    auto appendBucket = [&](uint16_t key) {
        uint32_t first = static_cast<uint32_t>(verts.size());
        for (auto& p : m_pool) {
            if (p.life <= 0.f || p.pipeKey != key) continue;
            float t01     = p.life / p.maxLife;            // 1 -> 0
            glm::vec4 col = glm::mix(p.colorEnd, p.color, t01);
            float curSize = glm::mix(p.sizeEnd, p.sizeStart, t01);
            float h = curSize * 0.5f;
            float c = std::cos(p.rot), s = std::sin(p.rot);
            auto rotc = [&](float x, float y) {
                return glm::vec2(p.pos.x + x * c - y * s, p.pos.y + x * s + y * c);
            };
            glm::vec2 tl = rotc(-h, -h), tr = rotc(h, -h),
                      br = rotc(h,  h), bl = rotc(-h, h);
            glm::vec2 data{t01, p.seed};
            auto v = [&](glm::vec2 pos, glm::vec2 uv) {
                ParticleVertex qv{};
                qv.pos = pos; qv.uv = uv; qv.color = col; qv.data = data;
                return qv;
            };
            verts.push_back(v(tl, {0, 0}));
            verts.push_back(v(tr, {1, 0}));
            verts.push_back(v(bl, {0, 1}));
            verts.push_back(v(tr, {1, 0}));
            verts.push_back(v(br, {1, 1}));
            verts.push_back(v(bl, {0, 1}));
        }
        uint32_t count = static_cast<uint32_t>(verts.size()) - first;
        if (count > 0) ranges.push_back({key, first, count});
    };

    appendBucket(0);
    for (auto& [key, _] : m_customPipelines) appendBucket(key);

    if (verts.empty()) return;
    if (verts.size() > MAX_PARTICLES * 6) verts.resize(MAX_PARTICLES * 6);

    memcpy(m_vertexBuffers[frameIndex].mapped,
           verts.data(), sizeof(ParticleVertex) * verts.size());

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

    for (const Range& r : ranges) {
        VkPipeline pipe = (r.pipeKey == 0)
            ? m_builtinPipeline.handle()
            : m_customPipelines.at(r.pipeKey).handle();
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        vkCmdDraw(cmd, r.count, 1, r.first, 0);
    }
}

void ParticleSystem::shutdown(VulkanContext& ctx, BufferManager& bufMgr) {
    m_builtinPipeline.shutdown(ctx);
    for (auto& [_, p] : m_customPipelines) p.shutdown(ctx);
    m_customPipelines.clear();
    m_customPipeKeys.clear();
    vkDestroyPipelineLayout(ctx.device(), m_pipelineLayout, nullptr);
    for (auto& b : m_vertexBuffers) bufMgr.destroyBuffer(b);
    for (auto& b : m_ubos)          bufMgr.destroyBuffer(b);
}
