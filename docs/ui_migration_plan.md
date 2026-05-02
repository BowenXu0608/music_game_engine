# UI Migration Plan — `Downloads/UI_design/MIGRATION.md`

> Verbatim copy of the implementation plan that drove the 2026-05-02 UI
> migration session. Original lived at
> `C:\Users\wense\.claude\plans\swift-stirring-stearns.md`.

## Context

The user produced a high-fidelity React mockup of the editor UI
(`C:\Users\wense\Downloads\UI_design\`) and a porting guide
(`MIGRATION.md`). The goal: keep all current functions, persistence, and
dialogs intact; replicate the mock's layout, hierarchy, density, and
visual language in the existing C++/Vulkan/ImGui editor.

The `ui-test` branch already carries about 70% of the migration:

- `engine/src/ui/StyleTokens.{h,cpp}` defines the full token table from
  MIGRATION §2 and applies it via `ApplyMusicGameStyle()` (called from
  `engine/src/ui/ImGuiLayer.cpp:33`).
- `engine/src/ui/Widgets.{h,cpp}` already provides `SectionHeader`,
  `DefaultPill`, `Pill`, `LabeledChip`, `SegBar` (two overloads).
- `SongEditor::renderNoteToolbar()`
  (`engine/src/ui/SongEditor.cpp:6230`) already uses `ui::LabeledChip`
  for Marker/Click/Hold/Flick/Arc/ArcTap with key hints, and the toolbar
  sequence already covers Analyze · Clr Mrk · Thin · Undo · Place · AI
  per MIGRATION §3.4.
- The SongEditor center column already places DiskFx/ScanPages strips
  above the scene per §3.4 (`SongEditor.cpp:780,788`), and the
  right-side Copilot+Audit tab bar is wired (`SongEditor.cpp:996`).
- `SettingsPageUI.cpp` already has the four sections
  (Audio/Gameplay/Visual/Misc) with the calibration wizard state
  machine, just using a local `sectionHeader()` helper instead of the
  new `ui::SectionHeader`.

What's left is mostly **wiring the new widgets through the existing
render functions**, plus a few small additions (a new SongEditor top
toolbar, a Dropdown widget, ProjectHub filter pills, an Apply button).
No new Vulkan code, no persistence changes.

## Scope ground rules (MIGRATION §5)

- Keep every existing function/method/data struct as is.
- Keep every existing dialog (Create / Add Set / Add Song / Add File /
  APK Build).
- Keep every existing JSON format (`project.json`,
  `music_selection.json`, `start_screen.json`, charts).
- Keep every existing keyboard shortcut.
- Only the visual layer changes.

## Phase A — Widget polish (`Widgets.{h,cpp}`)

Add the two widgets MIGRATION §4.3 calls out that are still missing:

1. **`Dropdown(id, items, current)`** — chevron control matching `SegBar`
   styling. ImGui `BeginCombo` with `BgPanel` background, `BorderHi`
   border, `TextHi` label. Reuses existing token palette. Used by Start
   Screen Transition Effect, Materials Target mode/slot/kind.
2. **`Slider(label, *v, min, max, suffix, accent)`** — thin wrapper over
   `ImGui::SliderFloat` that pushes accent into `ImGuiCol_SliderGrab` +
   `ImGuiCol_SliderGrabActive` and a 22 px `FrameMinHeight`. Used
   everywhere sliders need accent fill (note speed, BG dim, audio
   offset, mode-specific knobs).

**No** changes to existing widgets — `LabeledChip`, `SectionHeader`,
`Pill`, `SegBar`, `DefaultPill` already match the mock.

## Phase B — Mono-font push/pop (`ImGuiLayer.cpp`)

Skip the full Inter/JetBrains atlas swap (the licenses aren't in-tree
and Roboto is already loaded). Instead:

- Add a single mono fallback: load `Cousine-Regular` or `ProggyClean` at
  11 px as `g_FontMono` alongside the existing Roboto setup at
  `ImGuiLayer.cpp:36-40`.
- Add helper `void PushMono()` / `void PopMono()` in `Widgets.h`. Call
  sites push around timecodes (`01:42.318`), paths (`Projects/X/`), and
  numbers (BPM, FOV, ms windows).

Acceptable to defer entirely if Cousine isn't bundled; the loss is just
look-and-feel, not function. **Mark as P2** — do not block on this.

## Phase C — ProjectHub (`ProjectHub.{h,cpp}`)

Anchor: `ProjectHub.cpp:416-561`. Two-column layout (list + 300 px
detail) is already there; the top header and filter row aren't.

1. **Top header row** — render before `BeginChild("##hub_list")`:
   - Big title `Projects` (font scale 1.4× via
     `ImGui::SetWindowFontScale` then reset).
   - Subtitle `<N> projects · last opened <name>` in `TextLow`.
   - Right-aligned 240 px ⌘K search box (already exists as
     `m_searchBuf`; restyle).
   - `Add file` outline button + `Create game` primary-glow button
     (already exist; restyle).
2. **Filter pills row** — All / Drop 2D / 3D / Scan Line / Circle.
   Render via `ui::Pill(label, color, solid=isActive)`. New state field
   `m_modeFilter` (enum
   `ProjectMode { All, Drop2D, Drop3D, ScanLine, Circle }`). Filter
   `m_projects` by `proj.gameMode` (verify field exists on
   `ProjectInfo` in `ProjectHub.h`; if missing, add it and set during
   scan from the project's `music_selection.json`).
3. **Sort + Filter ghost buttons** to the right of the pill row (no
   functionality required for now beyond reading mock — render as
   visual stubs with tooltip "Coming soon" if necessary, or wire to an
   existing sort).
4. **Optional 220 px left rail** (All projects / Recent / Starred) —
   defer unless trivial; the mock shows it but MIGRATION §3.1 doesn't
   make it mandatory. **Mark as P2.**
5. **Detail panel** — keep as-is (it already follows §3.1: metadata +
   Open Project + inline APK Build state machine `idle/running/done`).
   Replace the `TextDisabled("PROJECT")` / `TextDisabled("Path")` etc.
   with `ui::SectionHeader("Metadata")` wrapping the field list, and
   `ui::SectionHeader("Package APK")` wrapping the build block.

## Phase D — Start Screen (`StartScreenEditor.cpp`)

Anchor: existing render function (not yet read in Phase 1; implementation
lives in `StartScreenEditor.cpp` past line 300). Logic for Background /
Logo / Tap Text / Transition / Audio is already in place per the audit.

1. Wrap each of the 5 logical groups in
   `ui::SectionHeader("Background", [&]{ ui::DefaultPill("##bg"); })` —
   captures the section state and gives each a Default reset pill
   (right-slot lambda already supported). Sections: **Background**,
   **Logo**, **Tap Text**, **Transition Effect**, **Audio**.
2. Logo Type picker → `ui::SegBar("##logoType", { {"text","Text"},
   {"image","Image"} }, 2, &m_logoTypeStr)` (string-indexed overload).
3. Transition Effect dropdown → `ui::Dropdown` (5 options: Fade to Black /
   Slide Left / Zoom In / Ripple / Custom). When current==Custom, render
   a path Field labeled `Script Path` with a hint
   `Lua receives: progress, tap_x, tap_y`.
4. Audio: render `BGM` uppercase mini-label, `FileRow` + Volume `Slider`
   + Loop `Toggle`; horizontal divider; `Tap SFX` mini-label, `FileRow`
   + Volume `Slider`. Use existing `m_bgMusic*` and `m_tapSfx*` fields
   per audit.
5. Materials tab is already wired through the existing Materials list/
   builder; verify it now uses `ui::Dropdown` for Target Mode/Slot/Kind.

## Phase E — Music Selection (`MusicSelectionEditor.cpp`)

Anchor: existing render function (not yet read). The data layer
(`gameMode`, `scoreHud`, `comboHud`, `noteAssets`) is already in place.

1. **Difficulty pills row** in the preview area: `EZ` lime / `NM` cyan /
   `HD` magenta. Active = solid pill via
   `ui::Pill(label, color, solid=true)`; inactive = outline.
2. **Per-difficulty chart blocks** (3 rows: Easy/Medium/Hard). Each row
   is a `BgPanel2` panel containing: difficulty `ui::Pill(solid)` +
   rating mono + filename + best score mono + achievement pill. Wire to
   existing `Song::charts[]` per difficulty.
3. **Game Mode SegBar** —
   `ui::SegBar({"2D","3D","Circle","Scan"}, &m_modeIdx)` — int-indexed
   overload — that maps to `GameModeType` + `DropDimension`.
4. **HUD block** — Score (X mono / Y mono / Size slider) + horizontal
   divider + Combo (X mono / Y mono / Size slider). Wire to
   `m_song->scoreHud` and `m_song->comboHud`.
5. **Auto-Play toggle** in the top bar — already a field on the editor;
   expose as `ui::Toggle`-style label + checkbox in the top-right of the
   existing header.

## Phase F — Song Editor (`SongEditor.{h,cpp}`) — biggest delta

### F.1 Top toolbar (new)

Add a 44 px chrome row above the existing main split, matching React
mock `song-editor.jsx:87-117`. Insert at `SongEditor::render` just after
`ImGui::Begin("Song Editor", ...)` and before computing `contentSize` /
sidebar widths.

Contents (left to right):

- 22 px gradient `M` logo tile (cyan→magenta gradient with cyan glow).
  Pure `ImDrawList` rect + text.
- Crumbs: `<projectName>` `>` `<songName>` `>` `<difficulty Pill>`
  `<mode Pill>`.
  - Difficulty Pill via `ui::Pill(diffLabel, diffColor, solid=true)`.
    Color: Easy=Lime, Medium=Cyan, Hard=Magenta.
  - Mode Pill via `ui::Pill(modeLabel, modeAccent, solid=false)`.
    Accent: Drop2D=Cyan, Drop3D=Magenta, Circle=Lime, ScanLine=Amber.
- Spacer (`flex: 1`).
- Auto-save indicator: 6 px lime dot + mono `auto-save - <Ns> ago` text,
  sourced from `Engine::autoSaveLastTimestamp()` (already exists per
  memory; if not exposed, add a getter).
- `Save` button: outline kind, calls `flushChartsForAutoSave()` +
  `MusicSelectionEditor::save()`.
- `Test Game` button: success kind + glow box-shadow, switches to
  Gameplay layer.

Reduce the existing `bodyH` calculation (`SongEditor.cpp:701`) by the
new top toolbar height (44 + 4 padding).

Remove the in-sidebar `Editing: <song name>` / `by <artist>` header at
`SongEditor.cpp:710-713` — now redundant with the top toolbar.

### F.2 Sidebar SectionHeader integration

`renderProperties()` (line 1508) and `renderGameModeConfig()` (line
2055) currently use `ImGui::CollapsingHeader`. Replace with
`ui::SectionHeader(label)` and gate body rendering on its return value.

`renderNotePage()` (line 1788) already groups per-note-type sections —
wrap each in `ui::SectionHeader`.

`renderMaterialBuilderPage()` (line 2039) — wrap material list and
builder in section headers; switch dropdowns to `ui::Dropdown`.

### F.3 Game Mode picker via SegBar

Inside `renderGameModeConfig(structureOnly=true)`, replace the existing
radio/combo for game mode with
`ui::SegBar({"Drop 2D","Drop 3D","Circle","Scan Line"}, &modeIdx)`.
Mode picker stays only in the Basic sidebar per MIGRATION §3.4 (no
top-bar duplication).

### F.4 Note toolbar polish

`renderNoteToolbar()` (line 6230) already does the right thing. Minor
tweaks:

- Verify the order is `Marker | <chips> | Analyze · Clr Mrk · Thin ·
  Undo · Place · AI · Clr Note | Audit` per MIGRATION §3.4. Audit
  button should be right-aligned via `SameLine` after a `Dummy(spacer)`.
- If `Clr Note` is missing, add it before the Audit-side spacer.

### F.5 Right sidebar — Copilot prompts only

Audit confirms `renderCopilotPanel()` (line 8238) and
`renderAuditPanel()` (line 8581) are tabbed correctly. No structural
change needed. Just verify Copilot doesn't render mode/property knobs
(it shouldn't, per audit).

## Phase G — Settings page (`SettingsPageUI.cpp`)

1. Replace local `sectionHeader()` helper (lines 38-54) with calls to
   `ui::SectionHeader`. To keep section-color labels (Audio cyan /
   Gameplay magenta / Visual lime / Misc amber per MIGRATION §3.5),
   wrap the call:

   ```cpp
   ImGui::PushStyleColor(ImGuiCol_Text, sectionAccent);
   ui::SectionHeader("Audio");
   ImGui::PopStyleColor();
   ```

2. Footer (around line 184): change single `Back` button to `Cancel`
   (ghost — calls `onBack()` without save) + `Apply` (primary-glow —
   calls `onSave()` then `onBack()`).

3. Confirm calibration wizard already matches mock — audit said yes; no
   change.

## Phase H — Verification

1. **Build**: `cmake --build build --config Debug` (existing build
   setup).
2. **Run**: launch the editor in background per the `Run After Update`
   memory rule.
3. **Walk MIGRATION §6 acceptance checklist**, marking PASS/FAIL per
   item:
   - [ ] Tokens applied via `ApplyMusicGameStyle()`; no inline colors
     anywhere → grep for `IM_COL32` / `ImVec4{` outside
     `StyleTokens.cpp` to spot leaks.
   - [ ] Project Hub: APK build inline (already done); metadata
     visible.
   - [ ] Start Screen: 5 SectionHeaders, Logo Type segmented, Transition
     Custom→Script, BGM + Tap SFX split.
   - [ ] Music Selection: per-difficulty chart files, Game Mode + HUD
     blocks.
   - [ ] Song Editor: labeled chips with key hints, mode picker only in
     Basic sidebar, Copilot prompts-only, right tabs Copilot+Audit,
     center order matches §3.4 table.
   - [ ] Player Settings: 4 sections, calibration wizard with 3 phases.
   - [ ] No emojis, no glyph-only buttons for note types, no AI-slop
     tropes.
4. **Regression smoke**: open BandoriSandbox project → switch through
   Start Screen / Music Selection / Song Editor / Settings → place a
   few notes → save → reopen, confirm chart data round-trips. Critical
   because per memory `User-data writes fail loud` — autosave must
   still write valid `music_selection.json`.

## Files modified

| File | Phase | Estimated LOC |
|---|---|---|
| `engine/src/ui/Widgets.h` | A | +12 |
| `engine/src/ui/Widgets.cpp` | A | +90 |
| `engine/src/ui/ImGuiLayer.cpp` | B | +20 (P2) |
| `engine/src/ui/ProjectHub.h` | C | +6 |
| `engine/src/ui/ProjectHub.cpp` | C | +180 |
| `engine/src/ui/StartScreenEditor.cpp` | D | +40 (mostly replacements) |
| `engine/src/ui/MusicSelectionEditor.cpp` | E | +120 |
| `engine/src/ui/SongEditor.h` | F | +5 |
| `engine/src/ui/SongEditor.cpp` | F | +180 (mostly top toolbar; sidebar replacements net ~0) |
| `engine/src/ui/SettingsPageUI.cpp` | G | +25 |

Total: ~10 files, ~680 LOC of net change. No file deletions, no struct
migrations, no JSON format changes.

## Risks

- **Section state**: `ui::SectionHeader` stores open/closed in
  `ImGuiStorage` keyed by label hash. Switching from
  `ImGui::CollapsingHeader` (which keys differently) means previously-
  collapsed sections will reset to "open" once. Acceptable — one-time
  visual flicker, no data loss.
- **Top toolbar geometry**: changes `bodyH`. If the user has a saved
  `m_sidebarW` / `m_copilotBarW` close to the bounds, they could clip.
  Already clamped via `std::clamp`, so safe.
- **ProjectHub mode filter**: requires `ProjectInfo::gameMode` to exist.
  If absent, add it to `ProjectHub.h` and populate during scan by
  reading the project's `music_selection.json` first song's
  `gameMode.type`. If neither lookup works, default to `All` and
  silently disable the filter.
- **Editor restart**: per memory `Run After Update`, every code change
  should kick off a background editor launch — schedule that into the
  verification loop.

## What I will NOT touch

- Vulkan backends (`engine/src/render/`).
- Chart serialization (`ChartLoader`,
  `MusicSelectionEditor::save` transcoder).
- Game-mode renderers (`engine/src/modes/*`).
- Audio path / `AudioEngine`.
- Copilot / Audit / Style Transfer / Shader Gen logic — only their UI
  containers if needed.

## Out-of-scope (defer to follow-up)

- Inter + JetBrains Mono font atlas swap (license + asset bundling).
- Recent / Starred filters in Project Hub left rail.
- Animated `Tap to Start` pulse and hero gradients in Start Screen
  preview (visual-only; the existing live render serves).
- Splitter handles styled to 4 px draggable bars (current
  InvisibleButton splitters work).
