#include "PostProcess.h"
#include "vulkan/VulkanContext.h"
#include <fstream>
#include <stdexcept>
#include <cmath>
#include <array>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeBits,
                                VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("PostProcess: no suitable memory type");
}

VkShaderModule PostProcess::loadShader(VulkanContext& ctx, const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("PostProcess: cannot open shader: " + path);
    size_t sz = f.tellg(); f.seekg(0);
    std::vector<char> buf(sz);
    f.read(buf.data(), sz);
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = sz;
    ci.pCode    = reinterpret_cast<const uint32_t*>(buf.data());
    VkShaderModule mod;
    vkCreateShaderModule(ctx.device(), &ci, nullptr, &mod);
    return mod;
}

// ---------------------------------------------------------------------------
// BloomImage helpers
// ---------------------------------------------------------------------------

BloomImage PostProcess::createBloomImage(VulkanContext& ctx, uint32_t w, uint32_t h) {
    BloomImage img;
    img.width = w; img.height = h;

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
    ici.extent        = {w, h, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(ctx.device(), &ici, nullptr, &img.image);

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(ctx.device(), img.image, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = findMemoryType(ctx.physicalDevice(), mr.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(ctx.device(), &mai, nullptr, &img.memory);
    vkBindImageMemory(ctx.device(), img.image, img.memory, 0);

    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = img.image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = VK_FORMAT_R16G16B16A16_SFLOAT;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.layerCount     = 1;
    vkCreateImageView(ctx.device(), &vci, nullptr, &img.view);

    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_LINEAR;
    sci.minFilter    = VK_FILTER_LINEAR;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(ctx.device(), &sci, nullptr, &img.sampler);

    return img;
}

void PostProcess::destroyBloomImage(VulkanContext& ctx, BloomImage& img) {
    if (img.sampler) vkDestroySampler(ctx.device(), img.sampler, nullptr);
    if (img.view)    vkDestroyImageView(ctx.device(), img.view, nullptr);
    if (img.image)   vkDestroyImage(ctx.device(), img.image, nullptr);
    if (img.memory)  vkFreeMemory(ctx.device(), img.memory, nullptr);
    img = {};
}

// ---------------------------------------------------------------------------
// Scene target
// ---------------------------------------------------------------------------

void PostProcess::createSceneTarget(VulkanContext& ctx, uint32_t w, uint32_t h, VkFormat fmt) {
    m_sceneFormat = fmt;

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = fmt;
    ici.extent        = {w, h, 1};
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT |
                        VK_IMAGE_USAGE_STORAGE_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(ctx.device(), &ici, nullptr, &m_sceneImage);

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(ctx.device(), m_sceneImage, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = mr.size;
    mai.memoryTypeIndex = findMemoryType(ctx.physicalDevice(), mr.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(ctx.device(), &mai, nullptr, &m_sceneMemory);
    vkBindImageMemory(ctx.device(), m_sceneImage, m_sceneMemory, 0);

    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = m_sceneImage;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = fmt;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.layerCount     = 1;
    vkCreateImageView(ctx.device(), &vci, nullptr, &m_sceneImageView);

    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_LINEAR;
    sci.minFilter    = VK_FILTER_LINEAR;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(ctx.device(), &sci, nullptr, &m_sceneSampler);

    // Render pass for scene
    VkAttachmentDescription att{};
    att.format         = fmt;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &ref;

    // EXTERNAL→0: wait for prior frame's composite to finish before clearing+writing
    VkSubpassDependency dep0{};
    dep0.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep0.dstSubpass    = 0;
    dep0.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep0.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep0.srcAccessMask = 0;
    dep0.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // 0→EXTERNAL: make scene writes visible to bloom compute + composite fragment
    VkSubpassDependency dep1{};
    dep1.srcSubpass    = 0;
    dep1.dstSubpass    = VK_SUBPASS_EXTERNAL;
    dep1.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep1.dstStageMask  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep1.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::array<VkSubpassDependency, 2> deps{dep0, dep1};

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &att;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &sub;
    rpci.dependencyCount = static_cast<uint32_t>(deps.size());
    rpci.pDependencies   = deps.data();
    vkCreateRenderPass(ctx.device(), &rpci, nullptr, &m_sceneRenderPass);

    VkFramebufferCreateInfo fbci{};
    fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass      = m_sceneRenderPass;
    fbci.attachmentCount = 1;
    fbci.pAttachments    = &m_sceneImageView;
    fbci.width           = w;
    fbci.height          = h;
    fbci.layers          = 1;
    vkCreateFramebuffer(ctx.device(), &fbci, nullptr, &m_sceneFramebuffer);
}

void PostProcess::destroySceneTarget(VulkanContext& ctx) {
    if (m_sceneFramebuffer) vkDestroyFramebuffer(ctx.device(), m_sceneFramebuffer, nullptr);
    if (m_sceneRenderPass)  vkDestroyRenderPass(ctx.device(), m_sceneRenderPass, nullptr);
    if (m_sceneSampler)     vkDestroySampler(ctx.device(), m_sceneSampler, nullptr);
    if (m_sceneImageView)   vkDestroyImageView(ctx.device(), m_sceneImageView, nullptr);
    if (m_sceneImage)       vkDestroyImage(ctx.device(), m_sceneImage, nullptr);
    if (m_sceneMemory)      vkFreeMemory(ctx.device(), m_sceneMemory, nullptr);
    m_sceneFramebuffer = VK_NULL_HANDLE;
    m_sceneRenderPass  = VK_NULL_HANDLE;
    m_sceneSampler     = VK_NULL_HANDLE;
    m_sceneImageView   = VK_NULL_HANDLE;
    m_sceneImage       = VK_NULL_HANDLE;
    m_sceneMemory      = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Bloom mips
// ---------------------------------------------------------------------------

void PostProcess::createBloomMips(VulkanContext& ctx, uint32_t w, uint32_t h) {
    uint32_t mw = w, mh = h;
    for (int i = 0; i < BLOOM_MIPS; ++i) {
        mw = std::max(1u, mw / 2);
        mh = std::max(1u, mh / 2);
        m_bloomMips[i] = createBloomImage(ctx, mw, mh);
    }
}

void PostProcess::destroyBloomMips(VulkanContext& ctx) {
    for (int i = 0; i < BLOOM_MIPS; ++i)
        destroyBloomImage(ctx, m_bloomMips[i]);
}

// ---------------------------------------------------------------------------
// Descriptor pool + layouts
// ---------------------------------------------------------------------------

void PostProcess::createDescriptorPool(VulkanContext& ctx) {
    // compute sets: BLOOM_MIPS downsample + (BLOOM_MIPS-1) upsample = 9 sets
    // composite set: 1
    // total: 10 sets, each has 2 bindings (sampler + storage or sampler + sampler)
    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[0].descriptorCount = 20;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    sizes[1].descriptorCount = 10;

    VkDescriptorPoolCreateInfo pci{};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets       = 12;
    pci.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pci.pPoolSizes    = sizes.data();
    vkCreateDescriptorPool(ctx.device(), &pci, nullptr, &m_descPool);

    // Compute layout: binding0 = sampler2D, binding1 = storage image
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        VkDescriptorSetLayoutCreateInfo lci{};
        lci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        lci.bindingCount = 2;
        lci.pBindings    = bindings.data();
        vkCreateDescriptorSetLayout(ctx.device(), &lci, nullptr, &m_computeSetLayout);
    }

    // Composite layout: binding0 = sampler2D scene, binding1 = sampler2D bloom
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo lci{};
        lci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        lci.bindingCount = 2;
        lci.pBindings    = bindings.data();
        vkCreateDescriptorSetLayout(ctx.device(), &lci, nullptr, &m_compositeSetLayout);
    }
}

// ---------------------------------------------------------------------------
// Update descriptors
// ---------------------------------------------------------------------------

void PostProcess::updateDescriptors(VulkanContext& ctx) {
    // All bloom mips stay in GENERAL permanently.
    // Scene stays SHADER_READ_ONLY_OPTIMAL (render pass finalLayout).

    m_downsampleSets.resize(BLOOM_MIPS);
    for (int i = 0; i < BLOOM_MIPS; ++i) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_computeSetLayout;
        vkAllocateDescriptorSets(ctx.device(), &ai, &m_downsampleSets[i]);

        VkImageView   srcView   = (i == 0) ? m_sceneImageView : m_bloomMips[i-1].view;
        VkSampler     srcSampler= (i == 0) ? m_sceneSampler   : m_bloomMips[i-1].sampler;
        VkImageLayout srcLayout = (i == 0) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                           : VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo srcInfo{};
        srcInfo.sampler     = srcSampler;
        srcInfo.imageView   = srcView;
        srcInfo.imageLayout = srcLayout;

        VkDescriptorImageInfo dstInfo{};
        dstInfo.imageView   = m_bloomMips[i].view;
        dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_downsampleSets[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo      = &srcInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_downsampleSets[i];
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo      = &dstInfo;
        vkUpdateDescriptorSets(ctx.device(), 2, writes.data(), 0, nullptr);
    }

    m_upsampleSets.resize(BLOOM_MIPS - 1);
    for (int i = 0; i < BLOOM_MIPS - 1; ++i) {
        int src = BLOOM_MIPS - 1 - i;
        int dst = src - 1;

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_computeSetLayout;
        vkAllocateDescriptorSets(ctx.device(), &ai, &m_upsampleSets[i]);

        VkDescriptorImageInfo srcInfo{};
        srcInfo.sampler     = m_bloomMips[src].sampler;
        srcInfo.imageView   = m_bloomMips[src].view;
        srcInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo dstInfo{};
        dstInfo.imageView   = m_bloomMips[dst].view;
        dstInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_upsampleSets[i];
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo      = &srcInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_upsampleSets[i];
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo      = &dstInfo;
        vkUpdateDescriptorSets(ctx.device(), 2, writes.data(), 0, nullptr);
    }

    // Composite set
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts        = &m_compositeSetLayout;
        vkAllocateDescriptorSets(ctx.device(), &ai, &m_compositeSet);

        VkDescriptorImageInfo sceneInfo{};
        sceneInfo.sampler     = m_sceneSampler;
        sceneInfo.imageView   = m_sceneImageView;
        sceneInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo bloomInfo{};
        bloomInfo.sampler     = m_bloomMips[0].sampler;
        bloomInfo.imageView   = m_bloomMips[0].view;
        bloomInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet          = m_compositeSet;
        writes[0].dstBinding      = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo      = &sceneInfo;
        writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet          = m_compositeSet;
        writes[1].dstBinding      = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo      = &bloomInfo;
        vkUpdateDescriptorSets(ctx.device(), 2, writes.data(), 0, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Compute pipelines
// ---------------------------------------------------------------------------

void PostProcess::createComputePipelines(VulkanContext& ctx, const std::string& shaderDir) {
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(float) * 2;

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &m_computeSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    vkCreatePipelineLayout(ctx.device(), &plci, nullptr, &m_computeLayout);

    auto makeCompute = [&](const std::string& spv) -> VkPipeline {
        VkShaderModule mod = loadShader(ctx, spv);
        VkComputePipelineCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.layout       = m_computeLayout;
        ci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = mod;
        ci.stage.pName  = "main";
        VkPipeline pipe;
        vkCreateComputePipelines(ctx.device(), VK_NULL_HANDLE, 1, &ci, nullptr, &pipe);
        vkDestroyShaderModule(ctx.device(), mod, nullptr);
        return pipe;
    };

    m_downsamplePipeline = makeCompute(shaderDir + "/bloom_downsample.comp.spv");
    m_upsamplePipeline   = makeCompute(shaderDir + "/bloom_upsample.comp.spv");
}

// ---------------------------------------------------------------------------
// Composite pipeline
// ---------------------------------------------------------------------------

void PostProcess::createCompositePipeline(VulkanContext& ctx, VkRenderPass swapRenderPass,
                                           const std::string& shaderDir) {
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(float);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &m_compositeSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    vkCreatePipelineLayout(ctx.device(), &plci, nullptr, &m_compositePipelineLayout);

    VkShaderModule vertMod = loadShader(ctx, shaderDir + "/composite.vert.spv");
    VkShaderModule fragMod = loadShader(ctx, shaderDir + "/composite.frag.spv");

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vps{};
    vps.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vps.viewportCount = 1;
    vps.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dyn.pDynamicStates    = dynStates.data();

    VkGraphicsPipelineCreateInfo gci{};
    gci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gci.stageCount          = 2;
    gci.pStages             = stages.data();
    gci.pVertexInputState   = &vi;
    gci.pInputAssemblyState = &ia;
    gci.pViewportState      = &vps;
    gci.pRasterizationState = &rs;
    gci.pMultisampleState   = &ms;
    gci.pColorBlendState    = &cb;
    gci.pDynamicState       = &dyn;
    gci.layout              = m_compositePipelineLayout;
    gci.renderPass          = swapRenderPass;
    gci.subpass             = 0;
    vkCreateGraphicsPipelines(ctx.device(), VK_NULL_HANDLE, 1, &gci, nullptr, &m_compositePipeline);

    vkDestroyShaderModule(ctx.device(), vertMod, nullptr);
    vkDestroyShaderModule(ctx.device(), fragMod, nullptr);
}

// ---------------------------------------------------------------------------
// init / shutdown / resize
// ---------------------------------------------------------------------------

void PostProcess::init(VulkanContext& ctx, uint32_t width, uint32_t height,
                       VkFormat sceneFormat, VkRenderPass swapRenderPass, const std::string& shaderDir) {
    m_width = width; m_height = height;
    m_shaderDir = shaderDir;
    createDescriptorPool(ctx);
    createSceneTarget(ctx, width, height, sceneFormat);
    createBloomMips(ctx, width, height);
    updateDescriptors(ctx);
    createComputePipelines(ctx, shaderDir);
    createCompositePipeline(ctx, swapRenderPass, shaderDir);

    // Transition all bloom mips UNDEFINED -> GENERAL (one-time, stays GENERAL forever)
    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();
    for (int i = 0; i < BLOOM_MIPS; ++i) {
        VkImageMemoryBarrier b{};
        b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
        b.srcAccessMask    = 0;
        b.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
        b.image            = m_bloomMips[i].image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    }
    ctx.endSingleTimeCommands(cmd);
}

void PostProcess::shutdown(VulkanContext& ctx) {
    if (m_compositePipeline)       vkDestroyPipeline(ctx.device(), m_compositePipeline, nullptr);
    if (m_compositePipelineLayout) vkDestroyPipelineLayout(ctx.device(), m_compositePipelineLayout, nullptr);
    if (m_downsamplePipeline)      vkDestroyPipeline(ctx.device(), m_downsamplePipeline, nullptr);
    if (m_upsamplePipeline)        vkDestroyPipeline(ctx.device(), m_upsamplePipeline, nullptr);
    if (m_computeLayout)           vkDestroyPipelineLayout(ctx.device(), m_computeLayout, nullptr);
    if (m_descPool) {
        vkDestroyDescriptorPool(ctx.device(), m_descPool, nullptr);
        m_descPool = VK_NULL_HANDLE;
    }
    if (m_computeSetLayout)   vkDestroyDescriptorSetLayout(ctx.device(), m_computeSetLayout, nullptr);
    if (m_compositeSetLayout) vkDestroyDescriptorSetLayout(ctx.device(), m_compositeSetLayout, nullptr);
    destroyBloomMips(ctx);
    destroySceneTarget(ctx);
    m_downsampleSets.clear();
    m_upsampleSets.clear();
    m_compositeSet             = VK_NULL_HANDLE;
    m_compositePipeline        = VK_NULL_HANDLE;
    m_compositePipelineLayout  = VK_NULL_HANDLE;
    m_downsamplePipeline       = VK_NULL_HANDLE;
    m_upsamplePipeline         = VK_NULL_HANDLE;
    m_computeLayout            = VK_NULL_HANDLE;
    m_computeSetLayout         = VK_NULL_HANDLE;
    m_compositeSetLayout       = VK_NULL_HANDLE;
}

void PostProcess::resize(VulkanContext& ctx, uint32_t width, uint32_t height) {
    m_width = width; m_height = height;
    vkResetDescriptorPool(ctx.device(), m_descPool, 0);
    m_downsampleSets.clear();
    m_upsampleSets.clear();
    m_compositeSet = VK_NULL_HANDLE;

    destroySceneTarget(ctx);
    destroyBloomMips(ctx);
    createSceneTarget(ctx, width, height, m_sceneFormat);
    createBloomMips(ctx, width, height);
    updateDescriptors(ctx);

    // Re-transition new bloom mips UNDEFINED -> GENERAL
    VkCommandBuffer cmd = ctx.beginSingleTimeCommands();
    for (int i = 0; i < BLOOM_MIPS; ++i) {
        VkImageMemoryBarrier b{};
        b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
        b.srcAccessMask    = 0;
        b.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
        b.image            = m_bloomMips[i].image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    }
    ctx.endSingleTimeCommands(cmd);
}

// ---------------------------------------------------------------------------
// render() — bloom compute passes
// ---------------------------------------------------------------------------

void PostProcess::render(VkCommandBuffer cmd, VulkanContext& ctx,
                         VkImageView /*sceneView*/, VkSampler /*sceneSampler*/) {
    // All bloom mips stay in GENERAL permanently.
    // Use memory barriers (no layout transitions) to order write->read between dispatches.

    auto memBarrier = [&](VkImage img, VkPipelineStageFlags dst = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) {
        VkImageMemoryBarrier b{};
        b.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
        b.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
        b.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
        b.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        b.image            = img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dst,
            0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // Downsample: scene(SHADER_READ_ONLY) -> mip0 -> mip1 -> mip2 -> mip3 -> mip4
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_downsamplePipeline);
    for (int i = 0; i < BLOOM_MIPS; ++i) {
        if (i > 0) memBarrier(m_bloomMips[i-1].image);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_computeLayout, 0, 1, &m_downsampleSets[i], 0, nullptr);

        uint32_t srcW = (i == 0) ? m_width  : m_bloomMips[i-1].width;
        uint32_t srcH = (i == 0) ? m_height : m_bloomMips[i-1].height;
        float pc[2] = { 1.f / static_cast<float>(srcW), 1.f / static_cast<float>(srcH) };
        vkCmdPushConstants(cmd, m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);

        uint32_t dstW = m_bloomMips[i].width;
        uint32_t dstH = m_bloomMips[i].height;
        vkCmdDispatch(cmd, (dstW + 7) / 8, (dstH + 7) / 8, 1);
    }

    // Upsample: mip4 -> mip3 -> mip2 -> mip1 -> mip0
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_upsamplePipeline);
    for (int i = 0; i < BLOOM_MIPS - 1; ++i) {
        int src = BLOOM_MIPS - 1 - i;
        int dst = src - 1;

        memBarrier(m_bloomMips[src].image);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_computeLayout, 0, 1, &m_upsampleSets[i], 0, nullptr);

        float pc[2] = { 1.f / static_cast<float>(m_bloomMips[src].width),
                        1.f / static_cast<float>(m_bloomMips[src].height) };
        vkCmdPushConstants(cmd, m_computeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);

        uint32_t dstW = m_bloomMips[dst].width;
        uint32_t dstH = m_bloomMips[dst].height;
        vkCmdDispatch(cmd, (dstW + 7) / 8, (dstH + 7) / 8, 1);
    }

    // mip0 compute write -> fragment shader read (layout stays GENERAL)
    memBarrier(m_bloomMips[0].image, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}
