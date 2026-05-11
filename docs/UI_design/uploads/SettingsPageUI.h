#pragma once
#include "game/PlayerSettings.h"
#include <imgui.h>
#include <functional>

class AudioEngine;

// Shared ImGui renderer for the player-facing Settings page.
// Called in two places:
//   * AndroidEngine (runtime) — live, `host.audio` set, readOnly=false.
//   * GameFlowPreview (editor) — preview, host.audio=nullptr, readOnly=true.
namespace SettingsPageUI {

struct Host {
    AudioEngine*          audio = nullptr;    // null in editor preview
    std::function<void()> onBack;             // invoked by the Back button
    std::function<void()> onSave;             // invoked to persist to disk
};

// Draws the whole settings page filling the given on-screen rectangle.
void render(ImVec2          origin,
            ImVec2          size,
            PlayerSettings& settings,
            const Host&     host,
            bool            readOnly);

} // namespace SettingsPageUI
