#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>

class VulkanContext;

static constexpr int MAX_FRAMES_IN_FLIGHT = 3;

class SyncObjects {
public:
    void init(VulkanContext& ctx);
    void shutdown(VulkanContext& ctx);

    VkSemaphore& imageAvailable(int frame) { return m_imageAvailable[frame]; }
    VkSemaphore& renderFinished(int frame) { return m_renderFinished[frame]; }
    VkFence&     inFlight(int frame)       { return m_inFlight[frame]; }

    int  currentFrame() const { return m_currentFrame; }
    void advance()            { m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT; }

private:
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence>     m_inFlight;
    int m_currentFrame = 0;
};
