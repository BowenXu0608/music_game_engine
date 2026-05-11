#pragma once
#include "game/PlayerSettings.h"
#include <string>

class Engine;

// A dedicated editor layer that renders the player-facing Settings page as
// its own full-panel view (same pattern as StartScreenEditor / MusicSelectionEditor).
// Reached from MusicSelectionEditor's "Next: Settings >" button.
class SettingsEditor {
public:
    void render(Engine* engine);

private:
    // Local preview struct so sliders bind to something — not persisted,
    // since real settings live in the shipped game's player_settings.json.
    PlayerSettings m_previewSettings;
};
