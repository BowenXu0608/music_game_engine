#include "SyncObjects.h"
#include "VulkanContext.h"

void SyncObjects::init(VulkanContext& ctx) {
    m_imageAvailable.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinished.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlight.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(ctx.device(), &si, nullptr, &m_imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(ctx.device(), &si, nullptr, &m_renderFinished[i]) != VK_SUCCESS ||
            vkCreateFence(ctx.device(), &fi, nullptr, &m_inFlight[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create sync objects");
    }
}

void SyncObjects::shutdown(VulkanContext& ctx) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(ctx.device(), m_imageAvailable[i], nullptr);
        vkDestroySemaphore(ctx.device(), m_renderFinished[i], nullptr);
        vkDestroyFence(ctx.device(), m_inFlight[i], nullptr);
    }
}
