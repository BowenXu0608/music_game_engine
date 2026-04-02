#pragma once
#include "RenderTypes.h"
#include "vulkan/BufferManager.h"
#include "vulkan/DescriptorManager.h"
#include "vulkan/Pipeline.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

class VulkanContext;

static constexpr uint32_t MAX_LINES = 4096;

class LineBatch {
public:
    void init(VulkanContext& ctx, BufferManager& bufMgr,
              DescriptorManager& descMgr, VkRenderPass renderPass,
              const std::string& shaderDir);
    void shutdown(VulkanContext& ctx, BufferManager& bufMgr);

    // Draw a line segment — CPU-expanded to a screen-aligned quad
    void drawLine(glm::vec2 a, glm::vec2 b, float width,
                  glm::vec4 color);

    // Draw a polyline (e.g. Lanota ring approximation)
    void drawPolyline(const std::vector<glm::vec2>& points, float width,
                      glm::vec4 color, bool closed = false);

    // frameIndex must match the current frame-in-flight index
    void flush(VkCommandBuffer cmd, int frameIndex);

    void updateFrameUBO(const glm::mat4& viewProj, float time, int frameIndex);

private:
    void expandLine(glm::vec2 a, glm::vec2 b, float width, glm::vec4 color);

    Pipeline         m_pipeline;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    std::vector<Buffer>          m_vertexBuffers;
    std::vector<Buffer>          m_ubos;
    std::vector<VkDescriptorSet> m_frameSets;

    std::vector<QuadVertex> m_vertices;
    int m_currentFrame = 0;
};
