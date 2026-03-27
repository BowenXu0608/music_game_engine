#include "SceneViewer.h"
#include "engine/Engine.h"
#include <imgui.h>

void SceneViewer::render(Engine& engine) {
    // Scene window
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);

    ImGui::Begin("Scene");
    ImVec2 sceneSize = ImGui::GetContentRegionAvail();
    ImGui::Text("Rendering: %dx%d", (int)sceneSize.x, (int)sceneSize.y);
    ImGui::Text("Game Mode: BanG Dream");
    ImGui::End();

    // Stats window
    ImGui::SetNextWindowPos(ImVec2(1220, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 300), ImGuiCond_FirstUseEver);

    ImGui::Begin("Stats");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Separator();
    ImGui::Text("Song Time: %.2f s", m_songTime);
    ImGui::End();
}
