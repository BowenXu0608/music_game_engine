#pragma once
#include <imgui.h>

class IPlayerEngine;

// Player-facing in-game HUD: score + combo overlay drawn on top of the
// active mode renderer's scene. Owns no state — reads everything from
// IPlayerEngine each frame.
class GameplayHudView {
public:
    void render(ImVec2 displaySize, IPlayerEngine& engine);

    // Multiplied into fontSize and panel padding so the HUD scales with
    // device DPI on Android (1.0 on desktop, ~2.6 on a 420dpi phone).
    void setUiScale(float s) { m_uiScale = (s > 0.f) ? s : 1.f; }

private:
    float m_uiScale = 1.f;
};
