#include "SettingsEditor.h"
#include "SettingsPageUI.h"
#include "engine/Engine.h"
#include <imgui.h>

void SettingsEditor::render(Engine* engine) {
    ImVec2 displaySz = ImGui::GetIO().DisplaySize;

    // Bind the settings page to the live engine PlayerSettings so changes
    // propagate to the audio engine / renderer immediately.
    PlayerSettings& settings = engine ? engine->playerSettings() : m_previewSettings;
    SettingsPageUI::Host host;
    host.audio  = engine ? &engine->audio() : nullptr;
    host.onSave = [engine]() { if (engine) engine->applyPlayerSettings(); };
    host.onBack = [engine]() {
        if (engine) {
            engine->applyPlayerSettings();
            engine->switchLayer(EditorLayer::MusicSelection);
        }
    };
    SettingsPageUI::render(ImVec2(0, 0), displaySz, settings, host,
                           /*readOnly=*/false);
}
