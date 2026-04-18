#include "MeshRenderer.h"
#include "ShaderCompiler.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/SyncObjects.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace {

constexpr std::array<MaterialKind, 5> kAllKinds = {
    MaterialKind::Unlit,
    MaterialKind::Glow,
    MaterialKind::Scroll,
    MaterialKind::Pulse,
    MaterialKind::Gradient,
};

const char* shaderNameForKind(MaterialKind k) {
    switch (k) {
        case MaterialKind::Unlit:    return "mesh_unlit";
        case MaterialKind::Glow:     return "mesh_glow";
        case MaterialKind::Scroll:   return "mesh_scroll";
        case MaterialKind::Pulse:    return "mesh_pulse";
        case MaterialKind::Gradient: return "mesh_gradient";
        default:                     return "mesh_unlit";
    }
}

} // namespace

void MeshRenderer::init(VulkanContext& ctx, BufferManager& bufMgr,
                        DescriptorManager& descMgr, VkRenderPass renderPass,
                        const std::string& shaderDir,
                        VkImageView whiteView, VkSampler whiteSampler) {
    m_whiteView    = whiteView;
    m_whiteSampler = whiteSampler;
    m_renderPass   = renderPass;
    m_shaderDir    = shaderDir;

    m_ubos.resize(MAX_FRAMES_IN_FLIGHT);
    m_frameSets.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_ubos[i] = bufMgr.createDynamicBuffer(sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        m_frameSets[i] = descMgr.allocateFrameSet(ctx, m_ubos[i].handle, sizeof(FrameUBO));
    }

    std::array<VkDescriptorSetLayout, 2> layouts = {
        descMgr.frameUBOLayout(), descMgr.textureLayout()
    };
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(MeshPushConstants);

    VkPipelineLayoutCreateInfo lci{};
    lci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount         = static_cast<uint32_t>(layouts.size());
    lci.pSetLayouts            = layouts.data();
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(ctx.device(), &lci, nullptr, &m_pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create mesh pipeline layout");

    auto binding    = MeshVertex::binding();
    auto attributes = MeshVertex::attributes();

    for (MaterialKind k : kAllKinds) {
        PipelineConfig cfg{};
        cfg.renderPass       = renderPass;
        cfg.layout           = m_pipelineLayout;
        cfg.vertShaderPath   = shaderDir + "/mesh.vert.spv";
        cfg.fragShaderPath   = shaderDir + "/" + shaderNameForKind(k) + ".frag.spv";
        cfg.vertexBinding    = binding;
        cfg.vertexAttributes = {attributes.begin(), attributes.end()};
        cfg.depthTest        = true;
        cfg.depthWrite       = true;
        cfg.blend            = PipelineConfig::Blend::Alpha;
        m_pipelines[(size_t)k].init(ctx, cfg);
    }

    // Pre-register the white fallback so legacy drawMesh() (no texture) and any
    // Material whose texture resolved to null share a single descriptor set.
    m_texSetCache[m_whiteView] = descMgr.allocateTextureSet(ctx, m_whiteView, m_whiteSampler);
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

void MeshRenderer::updateMesh(VulkanContext& ctx, BufferManager& bufMgr, Mesh& mesh,
                               const std::vector<MeshVertex>& verts,
                               const std::vector<uint32_t>& indices) {
    mesh.indexCount = static_cast<uint32_t>(indices.size());
    if (!verts.empty())
        bufMgr.uploadToBuffer(ctx, mesh.vertexBuffer, verts.data(), sizeof(MeshVertex) * verts.size());
    if (!indices.empty())
        bufMgr.uploadToBuffer(ctx, mesh.indexBuffer, indices.data(), sizeof(uint32_t) * indices.size());
}

VkDescriptorSet MeshRenderer::resolveTexSet(VkImageView view, VkSampler sampler,
                                            VulkanContext& ctx, DescriptorManager& descMgr) {
    VkImageView v = view    ? view    : m_whiteView;
    VkSampler   s = sampler ? sampler : m_whiteSampler;
    auto it = m_texSetCache.find(v);
    if (it != m_texSetCache.end()) return it->second;
    VkDescriptorSet set = descMgr.allocateTextureSet(ctx, v, s);
    m_texSetCache[v] = set;
    return set;
}

void MeshRenderer::drawMesh(const Mesh& mesh, const glm::mat4& model, const Material& mat,
                            VulkanContext& ctx, DescriptorManager& descMgr) {
    DrawEntry e;
    e.mesh        = &mesh;
    e.model       = model;
    e.kind        = mat.kind;
    // Resolve custom pipeline up-front so compilation failures don't hit
    // the middle of a flush. Null handle means "use the built-in pipeline
    // for this kind" (Unlit fallback when Custom fails).
    if (mat.kind == MaterialKind::Custom && !mat.customShaderPath.empty())
        e.customPipe = getOrBuildCustomPipeline(ctx, mat.customShaderPath);
    e.tint        = mat.tint;
    e.params      = mat.params;
    e.uvTransform = glm::vec4(0.f, 0.f, 1.f, 1.f);
    e.texSet      = resolveTexSet(mat.texture, mat.sampler, ctx, descMgr);
    m_queue.push_back(e);
}

void MeshRenderer::drawMesh(const Mesh& mesh, const glm::mat4& model, glm::vec4 tint) {
    // Legacy entry: tint-only, Unlit kind, white texture. Resolved lazily at
    // flush so we don't need a ctx/descMgr reference here. The cache is
    // pre-warmed by any Material-aware call, but if *only* legacy calls run we
    // populate it on first flush.
    DrawEntry e;
    e.mesh        = &mesh;
    e.model       = model;
    e.kind        = MaterialKind::Unlit;
    e.tint        = tint;
    e.params      = glm::vec4(0.f);
    e.uvTransform = glm::vec4(0.f, 0.f, 1.f, 1.f);
    e.texSet      = VK_NULL_HANDLE;  // flush() substitutes whiteView's set
    m_queue.push_back(e);
}

void MeshRenderer::flush(VkCommandBuffer cmd, int frameIndex) {
    if (m_queue.empty()) return;

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_frameSets[frameIndex], 0, nullptr);

    // If any legacy drawMesh deferred its texture, we need a set for the white
    // fallback up front. Callers that used the Material overload already
    // populated m_texSetCache via resolveTexSet(); legacy calls stored null.
    VkDescriptorSet whiteSet = VK_NULL_HANDLE;
    auto whiteIt = m_texSetCache.find(m_whiteView);
    if (whiteIt != m_texSetCache.end()) whiteSet = whiteIt->second;

    VkPipeline      lastPipe   = VK_NULL_HANDLE;
    VkDescriptorSet lastTexSet = VK_NULL_HANDLE;

    for (auto& entry : m_queue) {
        VkPipeline pipe = entry.customPipe;
        if (pipe == VK_NULL_HANDLE) {
            pipe = m_pipelines[(size_t)entry.kind].handle();
            if (pipe == VK_NULL_HANDLE)
                pipe = m_pipelines[(size_t)MaterialKind::Unlit].handle();
        }
        if (pipe != lastPipe) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            lastPipe = pipe;
        }

        VkDescriptorSet texSet = entry.texSet ? entry.texSet : whiteSet;
        if (texSet != lastTexSet) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_pipelineLayout, 1, 1, &texSet, 0, nullptr);
            lastTexSet = texSet;
        }

        MeshPushConstants pc{};
        pc.model       = entry.model;
        pc.tint        = entry.tint;
        pc.uvTransform = entry.uvTransform;
        pc.params      = entry.params;
        pc.kind        = (uint32_t)entry.kind;
        vkCmdPushConstants(cmd, m_pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(MeshPushConstants), &pc);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &entry.mesh->vertexBuffer.handle, &offset);
        vkCmdBindIndexBuffer(cmd, entry.mesh->indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
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

VkPipeline MeshRenderer::getOrBuildCustomPipeline(VulkanContext& ctx,
                                                   const std::string& fragPath,
                                                   std::string* errorOut) {
    auto it = m_customPipelines.find(fragPath);
    if (it != m_customPipelines.end()) return it->second.handle();

    ShaderCompileResult compile = compileFragmentToSpv(fragPath);
    if (!compile.ok) {
        if (errorOut) *errorOut = compile.errorLog;
        return VK_NULL_HANDLE;
    }

    auto binding    = MeshVertex::binding();
    auto attributes = MeshVertex::attributes();

    PipelineConfig cfg{};
    cfg.renderPass       = m_renderPass;
    cfg.layout           = m_pipelineLayout;
    cfg.vertShaderPath   = m_shaderDir + "/mesh.vert.spv";
    cfg.fragShaderPath   = compile.spvPath;
    cfg.vertexBinding    = binding;
    cfg.vertexAttributes = {attributes.begin(), attributes.end()};
    cfg.depthTest        = true;
    cfg.depthWrite       = true;
    cfg.blend            = PipelineConfig::Blend::Alpha;

    Pipeline pipe;
    try {
        pipe.init(ctx, cfg);
    } catch (const std::exception& e) {
        if (errorOut) *errorOut = e.what();
        return VK_NULL_HANDLE;
    }
    auto [ins, _] = m_customPipelines.emplace(fragPath, std::move(pipe));
    if (errorOut) *errorOut = compile.errorLog;
    return ins->second.handle();
}

void MeshRenderer::shutdown(VulkanContext& ctx, BufferManager& bufMgr) {
    for (auto& p : m_pipelines) p.shutdown(ctx);
    for (auto& [_, p] : m_customPipelines) p.shutdown(ctx);
    m_customPipelines.clear();
    vkDestroyPipelineLayout(ctx.device(), m_pipelineLayout, nullptr);
    for (auto& b : m_ubos) bufMgr.destroyBuffer(b);
    m_texSetCache.clear();
}
