#pragma once
#include "engine/Engine.h"
#include <imgui.h>
#include <algorithm>
#include <vector>

// Shared aspect-ratio controls for editor preview boxes (Start Screen +
// Music Selection). The preset list spans desktop 16:9 all the way down to
// modern tall phones (19.5:9 iPhone-class) and a few 4:3 / 16:10 tablets,
// so authors can see how the scene will frame on any mainstream device.

namespace previewAspect {

struct Preset { const char* label; int w; int h; };

inline const Preset* presets(int& count) {
    // Landscape-only — the game targets horizontal play, so portrait ratios
    // are deliberately omitted. Authors who need a square preview can pick
    // 1:1 or enter a custom ratio (still clamped to w >= h below).
    static const Preset kList[] = {
        {"16:9 (1920x1080)",    16, 9},     // Desktop / TV
        {"16:10",               16, 10},    // Classic tablet landscape
        {"4:3",                  4, 3},     // iPad landscape (pre-2024)
        {"3:2",                  3, 2},     // Surface / Pixel Tablet landscape
        {"18:9",                 2, 1},     // Older Android "18:9"
        {"19.5:9",              39, 18},    // iPhone X-class landscape
        {"20:9",                20, 9},     // Samsung / OnePlus / Xiaomi
        {"21:9 Ultrawide",      21, 9},
        {"1:1 Square",           1, 1},
        {"Custom",               0, 0},     // Sentinel — uses Engine-stored w/h
    };
    count = (int)(sizeof(kList) / sizeof(kList[0]));
    return kList;
}

// Enforce landscape: width must be >= height, otherwise the engine would be
// rendering the gameplay scene rotated. Called after any user edit.
inline void enforceLandscape(Engine::PreviewAspect& a) {
    if (a.w < 1) a.w = 1;
    if (a.h < 1) a.h = 1;
    if (a.h > a.w) a.h = a.w;
}

// Render the aspect-ratio controls inline (preset combo + two input boxes).
// Mutates `a` in place. Should be called from inside a BeginChild that ends
// up hosting the preview rect.
inline void renderControls(Engine::PreviewAspect& a) {
    int presetCount = 0;
    const Preset* list = presets(presetCount);

    // Build label array for Combo.
    std::vector<const char*> labels;
    labels.reserve(presetCount);
    for (int i = 0; i < presetCount; ++i) labels.push_back(list[i].label);

    ImGui::PushItemWidth(180);
    if (ImGui::Combo("##preset", &a.presetIdx, labels.data(), presetCount)) {
        if (a.presetIdx >= 0 && a.presetIdx < presetCount - 1) {
            a.w = list[a.presetIdx].w;
            a.h = list[a.presetIdx].h;
        }
        enforceLandscape(a);
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::TextUnformatted("Aspect");

    ImGui::SameLine();
    ImGui::PushItemWidth(70);
    int w = a.w;
    if (ImGui::InputInt("##w", &w, 0, 0)) {
        if (w > 0) { a.w = w; a.presetIdx = presetCount - 1; } // snap to Custom
        enforceLandscape(a);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(":");
    ImGui::SameLine();
    int h = a.h;
    if (ImGui::InputInt("##h", &h, 0, 0)) {
        if (h > 0) { a.h = h; a.presetIdx = presetCount - 1; }
        enforceLandscape(a);
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::TextDisabled("(landscape only)");
}

// Fit an aspect-w:aspect-h rectangle inside `avail` (at cursor pos), centered
// and letterboxed/pillarboxed. Returns the fitted rect as {origin, size}.
// Also paints the surrounding letterbox bars with the given color.
struct FitResult { ImVec2 origin; ImVec2 size; };

inline FitResult fitAndLetterbox(const Engine::PreviewAspect& a,
                                 ImVec2 avail,
                                 ImU32 letterboxCol = IM_COL32(10, 10, 14, 255))
{
    ImVec2 cur = ImGui::GetCursorScreenPos();
    // Clamp at read-time too, so stale portrait state from a previous session
    // (or a direct member poke) can never leak into the rendered rect.
    float aw = (float)std::max(1, a.w);
    float ah = (float)std::max(1, std::min(a.w, a.h));
    float ratio = aw / ah;

    float fitW = avail.x, fitH = avail.x / ratio;
    if (fitH > avail.y) { fitH = avail.y; fitW = avail.y * ratio; }

    ImVec2 origin(cur.x + (avail.x - fitW) * 0.5f,
                  cur.y + (avail.y - fitH) * 0.5f);
    ImVec2 size(fitW, fitH);

    // Paint bars around the fitted rect.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 outMax(cur.x + avail.x, cur.y + avail.y);
    // Top bar
    if (origin.y > cur.y)
        dl->AddRectFilled(cur, ImVec2(outMax.x, origin.y), letterboxCol);
    // Bottom bar
    if (origin.y + size.y < outMax.y)
        dl->AddRectFilled(ImVec2(cur.x, origin.y + size.y), outMax, letterboxCol);
    // Left bar
    if (origin.x > cur.x)
        dl->AddRectFilled(ImVec2(cur.x, origin.y),
                          ImVec2(origin.x, origin.y + size.y), letterboxCol);
    // Right bar
    if (origin.x + size.x < outMax.x)
        dl->AddRectFilled(ImVec2(origin.x + size.x, origin.y),
                          ImVec2(outMax.x, origin.y + size.y), letterboxCol);

    return {origin, size};
}

} // namespace previewAspect
