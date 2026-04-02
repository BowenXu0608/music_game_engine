#include "SceneViewer.h"
#include "engine/Engine.h"
#include <imgui.h>

void SceneViewer::render(Engine& engine) {
    // Scene window with game viewport
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);

    ImGui::Begin("Scene");

    // Play/Stop controls
    if (m_playing) {
        if (ImGui::Button("Stop")) m_playing = false;
    } else {
        if (ImGui::Button("Play")) m_playing = true;
    }
    ImGui::SameLine();
    ImGui::Text("Game Mode: BanG Dream");

    // Render game scene as texture
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    if (m_sceneTexSet != VK_NULL_HANDLE) {
        ImGui::Image((ImTextureID)m_sceneTexSet, viewportSize);
    } else {
        ImGui::Text("Scene: %dx%d", (int)viewportSize.x, (int)viewportSize.y);
    }

    ImGui::End();

    // Stats window
    ImGui::SetNextWindowPos(ImVec2(1220, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 300), ImGuiCond_FirstUseEver);

    ImGui::Begin("Stats");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Separator();
    ImGui::Text("Song Time: %.2f s", m_songTime);
    ImGui::Text("Status: %s", m_playing ? "Playing" : "Stopped");
    ImGui::End();
}
