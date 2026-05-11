#pragma once
#include <imgui.h>

// New editor design tokens. Source: Downloads/UI_design/tokens.js.
// Apply once via ApplyMusicGameStyle(); never sprinkle inline ImVec4s.
namespace ui::tokens {

// Surfaces — true-black canvas, lifted with subtle neutrals.
inline constexpr ImVec4 BgVoid    {0.000f, 0.000f, 0.000f, 1.f};   // outermost
inline constexpr ImVec4 BgBase    {0.027f, 0.027f, 0.031f, 1.f};   // window
inline constexpr ImVec4 BgPanel   {0.051f, 0.051f, 0.063f, 1.f};   // panel
inline constexpr ImVec4 BgPanel2  {0.075f, 0.075f, 0.094f, 1.f};   // raised / row hover
inline constexpr ImVec4 BgPanel3  {0.102f, 0.102f, 0.129f, 1.f};   // input field / pressed

// Borders / dividers (alpha so they sit on any surface).
inline constexpr ImVec4 Border    {1.f, 1.f, 1.f, 0.06f};
inline constexpr ImVec4 BorderHi  {1.f, 1.f, 1.f, 0.10f};
inline constexpr ImVec4 Divider   {1.f, 1.f, 1.f, 0.04f};

// Text.
inline constexpr ImVec4 TextHi    {0.953f, 0.957f, 0.969f, 1.f};
inline constexpr ImVec4 TextMid   {0.659f, 0.675f, 0.714f, 1.f};
inline constexpr ImVec4 TextLow   {0.369f, 0.388f, 0.431f, 1.f};
inline constexpr ImVec4 TextDim   {0.227f, 0.243f, 0.278f, 1.f};

// Accents — muted neon, oklch-derived, shared chroma (from tokens.js).
inline constexpr ImVec4 Cyan       {0.373f, 0.722f, 0.788f, 1.f};   // primary — desaturated teal
inline constexpr ImVec4 CyanDim    {0.227f, 0.471f, 0.518f, 1.f};
inline constexpr ImVec4 Magenta    {0.784f, 0.478f, 0.722f, 1.f};   // secondary — dusty rose
inline constexpr ImVec4 MagentaDim {0.541f, 0.310f, 0.494f, 1.f};
inline constexpr ImVec4 Lime       {0.561f, 0.722f, 0.478f, 1.f};   // success / FC — sage
inline constexpr ImVec4 LimeDim   {0.361f, 0.471f, 0.310f, 1.f};
inline constexpr ImVec4 Amber      {0.831f, 0.647f, 0.455f, 1.f};   // warning / good — muted gold
inline constexpr ImVec4 AmberDim  {0.541f, 0.420f, 0.295f, 1.f};
inline constexpr ImVec4 Red        {0.788f, 0.439f, 0.502f, 1.f};   // error / miss — muted coral
inline constexpr ImVec4 Violet     {0.541f, 0.498f, 0.690f, 1.f};   // copilot / AI — dusty violet

// Original neon accents preserved for game/scene preview content.
// Editor chrome uses the muted tokens above; game-facing UI (difficulty
// pills, START button, song cards, scene overlays) uses these so the
// player-visible appearance stays vibrant.
inline constexpr ImVec4 NCyan       {0.133f, 0.902f, 1.000f, 1.f};
inline constexpr ImVec4 NCyanDim    {0.059f, 0.659f, 0.761f, 1.f};
inline constexpr ImVec4 NMagenta    {1.000f, 0.239f, 0.941f, 1.f};
inline constexpr ImVec4 NMagentaDim {0.722f, 0.122f, 0.659f, 1.f};
inline constexpr ImVec4 NLime       {0.490f, 1.000f, 0.353f, 1.f};
inline constexpr ImVec4 NAmber      {1.000f, 0.710f, 0.278f, 1.f};
inline constexpr ImVec4 NRed        {1.000f, 0.302f, 0.420f, 1.f};
inline constexpr ImVec4 NViolet     {0.627f, 0.439f, 1.000f, 1.f};

// Helpers — ImGui packed colors and alpha tweaks.
inline ImU32 ToU32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
inline ImVec4 WithAlpha(const ImVec4& c, float a) { return {c.x, c.y, c.z, a}; }

} // namespace ui::tokens

// Applies the new editor design system to the given ImGuiStyle.
// Call once after ImGui::CreateContext() / StyleColorsDark().
void ApplyMusicGameStyle(ImGuiStyle& style);
