#include "ResultsView.h"
#include "engine/IPlayerEngine.h"
#include "gameplay/ScoreTracker.h"
#include "gameplay/JudgmentSystem.h"
#include <imgui.h>

void ResultsView::render(ImVec2 displaySize, IPlayerEngine& engine) {
    // Semi-transparent backdrop
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::Begin("##results_bg", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(0, 0), displaySize, IM_COL32(0, 0, 0, 180));
    ImGui::End();

    // Centered results panel
    ImVec2 panelSz(280, 300);
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - panelSz.x) * 0.5f,
                                    (displaySize.y - panelSz.y) * 0.5f));
    ImGui::SetNextWindowSize(panelSz);
    ImGui::Begin("Results", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse);

    ScoreTracker& score = engine.score();
    JudgmentSystem& judgment = engine.judgment();

    ImGui::Text("Score:     %07d", score.getScore());
    ImGui::Text("Max Combo: %d", score.getMaxCombo());
    ImGui::Separator();

    const auto& stats = judgment.getStats();
    ImGui::TextColored(ImVec4(1.f, 0.95f, 0.2f, 1.f), "Perfect: %d", stats.perfect);
    ImGui::TextColored(ImVec4(0.2f, 1.f, 0.4f, 1.f),  "Good:    %d", stats.good);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 1.f, 1.f),  "Bad:     %d", stats.bad);
    ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f),  "Miss:    %d", stats.miss);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Back", ImVec2(-1, 40))) {
        engine.exitGameplay();
    }

    ImGui::End();
}
