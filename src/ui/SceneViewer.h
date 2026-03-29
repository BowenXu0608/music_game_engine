#pragma once
#include <string>
#include <vulkan/vulkan.h>

class Engine;

class SceneViewer {
public:
    void render(Engine& engine);

    bool isPlaying() const { return m_playing; }
    void setPlaying(bool playing) { m_playing = playing; }

    void setSceneTexture(VkDescriptorSet texSet) { m_sceneTexSet = texSet; }

private:
    bool m_playing = true;
    bool m_showStats = true;
    float m_songTime = 0.0f;
    VkDescriptorSet m_sceneTexSet = VK_NULL_HANDLE;
};
