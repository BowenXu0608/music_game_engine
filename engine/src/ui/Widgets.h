#pragma once
#include "StyleTokens.h"
#include <imgui.h>
#include <functional>
#include <initializer_list>

// Reusable editor widgets that match the new design language.
// Wrap ImGui primitives only — no rendering invariants change.
namespace ui {

// Mono font for timecodes / paths / numbers per MIGRATION §2. Loaded by
// ImGuiLayer::init from third_party/imgui/misc/fonts/Cousine-Regular.ttf.
// May be nullptr if the file is missing — the Push/Pop helpers no-op.
extern ImFont* s_monoFont;
inline void PushMono() { if (s_monoFont) ImGui::PushFont(s_monoFont); }
inline void PopMono()  { if (s_monoFont) ImGui::PopFont(); }

// Multi-weight / multi-size font registry. Populated by ImGuiLayer::init.
// All pointers fall back to ImGui's default font if the .ttf is missing.
struct Fonts {
    ImFont* body      = nullptr;  // Inter 16/400 (default body)
    ImFont* bodyMed   = nullptr;  // Inter 16/500 (project names, nav)
    ImFont* label     = nullptr;  // Inter 14/600 (section headers, pills)
    ImFont* heading   = nullptr;  // Inter 20/600 (panel titles)
    ImFont* title     = nullptr;  // Inter 24/600 (Settings title)
    ImFont* mono      = nullptr;  // JetBrains Mono 16
    ImFont* monoSm    = nullptr;  // JetBrains Mono 14 (field values)
    ImFont* monoLg    = nullptr;  // JetBrains Mono 24 (timecode)
};
extern Fonts fonts;

// Draw text with per-character extra spacing (ImGui has no native letter-spacing).
void DrawSpacedText(ImDrawList* dl, ImFont* font, float fontSize,
                    ImVec2 pos, ImU32 col, const char* text, float extraSpacing);

// Measure the width of spaced text.
float CalcSpacedTextWidth(ImFont* font, float fontSize, const char* text, float extraSpacing);

// Primary action button: solid accent fill, dark text.
bool PrimaryButton(const char* label, const ImVec4& accent, ImVec2 size = {0,0});

// Ghost button: transparent bg, BorderHi outline, TextMid text.
bool GhostButton(const char* label, ImVec2 size = {0,0});

// Icon-only button via DrawList callback.
bool IconButton(const char* id, ImVec2 size,
                std::function<void(ImDrawList*, ImVec2 center, float sz, ImU32 col)> drawIcon);


// Uppercase 10px section label with a bullet chevron and optional right-side
// slot (e.g. a Default reset pill). Replaces inline CollapsingHeader styling
// in property panels.
//
//   if (ui::SectionHeader("Background", [&]{ ui::DefaultPill("##bg"); })) { ... }
//
// Returns true when the section is expanded. State is stored in ImGui storage
// keyed by the label so each header remembers its open/closed across frames.
bool SectionHeader(const char* label, std::function<void()> rightSlot = {});

// Small clickable "Default" reset pill rendered with the cyan accent. Returns
// true on click. Caller decides what "default" means for the surrounding
// section.
bool DefaultPill(const char* id);

// Solid or outlined pill / chip used for mode badges, difficulty markers,
// achievement letters. Non-interactive.
void Pill(const char* label, const ImVec4& color, bool solid = false);

// Note-type chip used by the SongEditor note toolbar: colored swatch + word
// + key hint (1..N). Returns true when clicked.
//
//   if (ui::LabeledChip("Click", 1, ui::tokens::Cyan, &active)) { ... }
//
// keyHint of 0 hides the hint badge.
bool LabeledChip(const char* label, int keyHint, const ImVec4& accent, bool active);

// Segmented control. Items are { id, label } pairs; current is the selected
// id. Returns true when the selection changes (and writes to *current).
//
//   const char* logoTypes[][2] = {{"text","Text"},{"image","Image"}};
//   ui::SegBar("##logoType", logoTypes, 2, &logoTypeStr);
bool SegBar(const char* id,
            const char* const items[][2],
            int itemCount,
            const char** current);

// Same as above but for an int-indexed segmented control.
bool SegBar(const char* id,
            std::initializer_list<const char*> labels,
            int* current);

// Dropdown with chevron — same surface treatment as SegBar, but only one item
// is visible at a time. Click to open the popup. Items are { id, label }
// pairs; current is the selected id (writable).
//
//   const char* fxKinds[][2] = {{"fade","Fade to Black"},{"slide","Slide Left"}};
//   ui::Dropdown("##fx", fxKinds, 2, &m_fx);
bool Dropdown(const char* id,
              const char* const items[][2],
              int itemCount,
              const char** current);

// Float slider with the accent color filled into ImGuiCol_SliderGrab. Wraps
// ImGui::SliderFloat — the value is written through `*v`. `suffix` is
// appended to the displayed value (e.g. " ms", " s", "%").
//
//   ui::Slider("Audio offset", &m_offsetMs, -200.f, 200.f, " ms",
//              ui::tokens::Magenta);
bool Slider(const char* label,
            float* v,
            float vMin,
            float vMax,
            const char* suffix,
            const ImVec4& accent);

// Renders the editor-wide 44 px top toolbar matching MIGRATION §3 mock:
// gradient cyan→magenta `M` tile + crumbs (joined with `>`). The right slot
// (lambda) is rendered right-aligned. Returns the y-coordinate of the line
// directly below the bar.
//
//   ui::TopBar({"BandoriSandbox", "Start Screen"}, [&]{
//       ui::TopNavBack ("Hub",             []{ ... });
//       ImGui::SameLine(0.f, 6.f);
//       ui::TopNavForward("Music Selection",[]{ ... });
//       ImGui::SameLine(0.f, 12.f);
//       ui::TopTestGame([]{ ... });
//   });
float TopBar(std::initializer_list<const char*> crumbs,
             std::function<void()> rightSlot = {});

// Top-bar nav back-button: "<  Label" ghost. Returns true on click.
bool TopNavBack(const char* label);

// Top-bar nav forward-button: "Label  >" ghost. Returns true on click.
bool TopNavForward(const char* label);

// Top-bar primary action: lime fill + play triangle + "Test Game" label.
// Returns true on click.
bool TopTestGame();

// Pre-measured rendered width of a DefaultPill in the current ImGui style.
// Use this from layout code that needs to right-align the pill — measuring
// from the font + style avoids hardcoded pixel reserves that break at
// non-100% DPI scales.
float DefaultPillWidth();

// Top-bar toggle pill (e.g. "Copilot"). Filled cyan when active, hollow
// otherwise. Returns true on click; caller flips the bound state.
bool TopToggle(const char* label, bool active);

// Pill-shaped toggle switch (36x18 px). Returns true when the value changes.
bool Toggle(const char* label, bool* v, const ImVec4& accent = tokens::Cyan);

// Styled input field with label above, BgPanel3 fill, optional mono font.
bool Field(const char* label, char* buf, int bufSize, bool mono = false);

// Float variant of Field for numeric properties.
bool FieldFloat(const char* label, float* v, const char* format = "%.2f",
                bool mono = true);

// Striped placeholder drop-zone for drag-drop targets.
void Placeholder(const char* label, float height,
                 const ImVec4& accent = tokens::Cyan);

} // namespace ui
