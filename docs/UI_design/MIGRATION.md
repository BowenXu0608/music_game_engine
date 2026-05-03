# Music Game Engine — UI Migration Guide

This package contains a high-fidelity React mockup of the editor UI. It is a **visual spec**, not production code. The target is the existing C++/Vulkan engine that uses **Dear ImGui** (`SongEditor.cpp`, `MusicSelectionEditor.cpp`, `StartScreenEditor.cpp`, `ProjectHub.cpp`, `SettingsPageUI.cpp`).

The goal: keep all current functions and code paths intact; replicate the **layout, hierarchy, density, and visual language** shown in the mock.

---

## 1. Files in this package

```
Music Game Engine.html        — entry point; loads everything in a DesignCanvas
tokens.js                     — CSS custom-properties for colors, spacing, type
components.jsx                — shared primitives (Btn, IconBtn, Panel, Tabs,
                                Field, Slider, Toggle, Pill, SidebarItem,
                                SectionHeader, Surface, Placeholder, TopBar,
                                Icon, I (icon paths))
design-canvas.jsx             — pan/zoom canvas wrapper (mockup only — drop)
tweaks-panel.jsx              — runtime knobs (mockup only — drop)
screens/
  project-hub.jsx             — Project Hub
  start-screen.jsx            — Start Screen Editor
  music-selection.jsx         — Music Selection Editor
  song-editor.jsx             — Song Editor (the DAW; per-mode config)
  player-settings.jsx         — Player Settings
MIGRATION.md                  — this file
```

When porting to ImGui, the only files that matter are the five `screens/*.jsx`.
`design-canvas.jsx` and `tweaks-panel.jsx` are presentation chrome and should
be discarded.

---

## 2. Design system (tokens.js)

| Token group | Values | Where it maps in ImGui |
|---|---|---|
| **Surfaces** | `--bg-base #000`, `--bg-panel #0a0a0e`, `--bg-panel-2 #0e0e14`, `--bg-panel-3 #14141c`, `--bg-elev #1a1a24` | `ImGuiCol_WindowBg`, `_ChildBg`, `_FrameBg`, `_FrameBgHovered`, `_PopupBg` |
| **Borders** | `--border #1f1f2e`, `--border-hi #2a2a3e`, `--divider #14141c` | `ImGuiCol_Border`, `_Separator` |
| **Text** | `--text-hi #fff`, `--text-mid rgba(255,255,255,0.72)`, `--text-low rgba(255,255,255,0.42)` | `ImGuiCol_Text`, `_TextDisabled` |
| **Accents** | `--cyan #22e6ff`, `--magenta #ff3df0`, `--lime #7dff5a`, `--amber #ffb547`, `--red #ff4d6b`, `--violet #a070ff` | Highlights, sliders' grab/active, button glow |
| **Type** | UI: `Inter` 11–13px; Mono: `JetBrains Mono` 9–11px (timecodes, paths, numbers) | Push two `ImFont*`s: `g_FontUI`, `g_FontMono` |
| **Spacing** | 4 / 6 / 8 / 10 / 12 / 14 / 18 / 20 px scale | `ImGuiStyle::ItemSpacing`, `WindowPadding`, `FramePadding` |
| **Radii** | 3 / 4 / 6 / 8 px | `FrameRounding`, `GrabRounding`, `WindowRounding` |

**Apply the tokens once** in a `style.cpp` helper (`ApplyMusicGameStyle()`),
called after `ImGui::CreateContext()`. Do not sprinkle colors inline.

---

## 3. Per-screen migration notes

Each section follows the same template:

> **Source file** — what the screen does today
> **Mock file** — where the new layout lives
> **Layout** — frame regions
> **Required behaviour** — keep / change / add

### 3.1 Project Hub  (`ProjectHub.cpp`)

**Source:** scans `Projects/`, builds project list, opens Create/Add dialogs, runs APK build.
**Mock:** `screens/project-hub.jsx`.

**Layout** (single ImGui window, no docking required):

```
┌─────────────────────────────────────────────────────────────────┐
│ TopBar: "Project Hub"                              [⚙ Settings] │
├─────────────────────────────────────────────────────────────────┤
│ ┌─ Left rail (220) ─┐ ┌─ Header row ──────────────────────────┐ │
│ │ All projects   6  │ │ Projects                  [search ⌘K] │ │
│ │ Recent         3  │ │ 6 projects · last opened …            │ │
│ │ Starred        2  │ │                  [Add file] [+ Create]│ │
│ └───────────────────┘ ├──────────────────────────────────────┤ │
│                       │ Filter pills: All · Drop 2D · 3D ·   │ │
│                       │ Scan Line · Circle  ·····  Sort ▾    │ │
│                       ├──────────┬───────────────────────────┤ │
│                       │ Project  │ Detail / APK build (280)  │ │
│                       │ table    │  · Metadata               │ │
│                       │  · icon  │  · [Open Project]         │ │
│                       │  · name  │  · Package APK panel      │ │
│                       │  · mode  │    (idle / running / done)│ │
│                       │  · songs │                           │ │
│                       │  · mtime │                           │ │
│                       └──────────┴───────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

**Required behaviour:**
- **Project table**: bind to `m_projects`. Selected row → fills detail pane (`m_selectedProject`).
- **Detail metadata**: pull from project JSON (`name`, `version`, `defaultChart`, `shaderPath`, `lastModified`, on-disk path).
- **Create game**: keep current name dialog (`m_showCreateDialog`).
- **Add file**: keep current file picker (`m_showAddFileDialog`).
- **Package APK**: keep `BuildAPK()` future-based flow. Surface progress + log inline in the right panel (mock shows `idle → running → done` states with a streaming log). Replace any blocking modal with this inline panel.
- Filter pills filter by `gameMode`. Search box filters by name + path.

### 3.2 Start Screen Editor  (`StartScreenEditor.cpp`)

**Source:** edits the start-screen layer (background, logo, tap text, transition, audio, materials).
**Mock:** `screens/start-screen.jsx`.

**Layout** (3 columns, splitter between mid + right):

```
┌──────────────┬─────────────────────────────────┬───────────────────┐
│ Hierarchy    │ Preview (16:9 / 16:10 / 21:9)   │ Properties / Mat. │
│   Background │  · live render of start screen  │ ── Background     │
│   Logo*      │  · selection box + handles      │ ── Logo (Type:    │
│   Tap Text   │  · zoom %                       │      Text/Image)  │
│   Trans FX   │                                 │ ── Tap Text       │
│   Audio      │                                 │ ── Transition     │
│              │                                 │ ── Audio (BGM+SFX)│
│              │                                 │  Materials tab    │
└──────────────┴─────────────────────────────────┴───────────────────┘
```

**Required behaviour — Properties tab** (5 collapsing headers, each with a `Default` reset pill):
1. **Background** — image/video file picker.
2. **Logo** — Type segmented (Text / Image). When Text: text content, font size (12–300 px), Bold toggle, color (gradient or solid), X/Y position (0..1), Scale (0.1–5), Glow toggle → glow color + radius (1–32). When Image: image picker + same X/Y/Scale/Glow.
3. **Tap Text** — text content + size.
4. **Transition Effect** — dropdown: Fade to Black / Slide Left / Zoom In / Ripple / **Custom**. Duration slider (0.1–2 s). When `Custom`: shows `Script Path` field (Lua receives `progress, tap_x, tap_y`).
5. **Audio** — BGM (file + volume + loop toggle) and Tap SFX (file + volume).

**Required behaviour — Materials tab:**
- List + builder. List shows all materials in the project with their target slot.
- Edit form: target mode dropdown (`start / 2d / 3d / circle / lanota / phigros`), target slot dropdown, kind (`Unlit / Glow / Scroll / Pulse / Gradient / Custom`), texture path, intensity, speed.
- AI Shader Generator block (per existing source) — natural-language prompt → `.frag` → `glslc` → retry on errors.

### 3.3 Music Selection Editor  (`MusicSelectionEditor.cpp`)

**Source:** sets / songs / charts hierarchy + per-song metadata + game-mode config + HUD config + page background and FC/AP badge slots.
**Mock:** `screens/music-selection.jsx`.

**Layout:**

```
┌──────────────┬─────────────────────────────────┬───────────────────┐
│ Hierarchy    │ Preview (16:9 / 21:9)           │ Properties        │
│  [+ Set]     │  · cover art + 3 difficulty pads│  Song (name,      │
│  [+ Song]    │  · song wheel cards (right)     │       artist,     │
│  search 🔍   │  · START button                 │       cover,      │
│ ── Sets      │                                 │       audio)      │
│  NEON +6     │                                 │  Preview Clip     │
│   Forest…◀   │                                 │   (start, length, │
│   Mana…      │                                 │    Auto)          │
│   …          │                                 │  Charts (Easy /   │
│ ── Page      │                                 │   Medium / Hard:  │
│  Background  │                                 │   file, score,    │
│  FC Badge    │                                 │   achievement)    │
│  AP Badge    │                                 │  Game Mode (mode  │
│              │                                 │   + tracks + sky  │
│              │                                 │   + judgment ms)  │
│              │                                 │  HUD (Score+Combo │
│              │                                 │   pos / size)     │
└──────────────┴─────────────────────────────────┴───────────────────┘
```

**Required behaviour:**
- **+Set / +Song**: keep `m_showAddSetDialog`, `m_showAddSongDialog`.
- **Preview clip**: `previewStart`, `previewLength`, plus an `Auto` button (existing auto-detect).
- **Charts**: 3 entries (Easy / Medium / Hard). Each entry shows file path + best `score` + achievement letter (— / D / C / B / A / S / S+).
- **Game Mode**: per-song `GameModeConfig` — mode (Drop2D/Drop3D/Circle/ScanLine), tracks, sky height, disk inner / ring spacing (Circle), judgment windows (perfect / great / good ms).
- **HUD**: `scoreHud` and `comboHud` — pos (x/y in 0..1), size (px). Existing source has color/glow too; surface in this panel.
- **Test Game** in top bar carries an `Auto-Play` toggle that flows into existing `m_autoPlay`.

### 3.4 Song Editor  (`SongEditor.cpp`) — the DAW

**Source:** the chart editor. Shared chrome + mode-specific scene + chart timeline.
**Mock:** `screens/song-editor.jsx` (single component, **parameterized by `mode`**).

**Layout:**

```
┌─────────────────────────────────────────────────────────────────────┐
│ TopBar: project · song · diff · BPM                  [Test Game ▶] │
├──────────────┬───────────────────────────────────┬──────────────────┤
│ Left sidebar │ Center column                     │ Right sidebar    │
│              │                                   │                  │
│ Tabs:        │  ┌─ Note toolbar ─────────────┐   │ Tabs:            │
│ Basic        │  │ Marker · [Click 1][Hold 2] │   │ Copilot · Audit  │
│ Note         │  │ [Flick 3] · Analyze · …    │   │                  │
│ Material     │  │ Audit                      │   │ Copilot:         │
│              │  └────────────────────────────┘   │  · NL prompt     │
│ Basic tab:   │                                   │    "double-time  │
│  Game Mode   │  ┌─ Scene preview ─────────────┐  │     the chorus"  │
│   ◉ Drop 2D  │  │  per-mode rendering          │  │  · accept/reject │
│   ○ Drop 3D  │  │  Drop 2D: 7 lanes vertical   │  │                  │
│   ○ Circle   │  │  Drop 3D: dual sky/ground    │  │ Audit:           │
│   ○ Scan Ln  │  │  Circle:  rings + disk       │  │  · rule list     │
│  (mode cfg)  │  │  Scan:    page sweep         │  │  · severity      │
│  Audio       │  └────────────────────────────┘   │  · click → focus │
│  Judgment    │                                   │                  │
│  HUD         │  *(Drop 3D only)* ArcCurve strip  │                  │
│  Camera      │  *(Circle only)*  Disk-FX strip   │                  │
│  Background  │  *(Scan Line only)* Pages strip   │                  │
│              │                                   │                  │
│ Note tab:    │  ┌─ Chart timeline ─────────────┐ │                  │
│  selected    │  │  per-mode lane layout         │ │                  │
│  type, fields│  │  beat / bar gridlines         │ │                  │
│  arc fields  │  │  playhead                     │ │                  │
│  material    │  └─────────────────────────────┘  │                  │
│  slots, snap │                                   │                  │
│              │  ┌─ Waveform + transport ───────┐ │                  │
│ Material tab:│  │ wave · beat marks · clip · ⏯ │ │                  │
│  shared with │  └─────────────────────────────┘ │                  │
│  start scrn  │                                   │                  │
└──────────────┴───────────────────────────────────┴──────────────────┘
```

**Required behaviour:**

**Center column order — fixed, mode-dependent:**
| Mode | Order (top → bottom) |
|---|---|
| Drop 2D | toolbar · scene · timeline · waveform · transport |
| Drop 3D | toolbar · scene · timeline · **ArcCurve strip (120 px)** · waveform · transport |
| Circle | toolbar · **Disk FX strip (120 px)** · scene · timeline · waveform · transport |
| Scan Line | toolbar · **Pages strip (96 px)** · scene · waveform · transport · *(no traditional chart-timeline; the scene IS the page)* |

**Note types per mode** (drives the toolbar chips, the Note tab, and the picker):

| Mode | Note types |
|---|---|
| Drop 2D | Click · Hold · Flick |
| Drop 3D | Click · Hold · Flick · Arc · ArcTap |
| Circle  | Click · Hold · Flick |
| Scan Line | Click · Hold · Flick · Slide |

Render note toolbar buttons **as labeled chips**: `[● Click 1] [● Hold 2] [● Flick 3]` — colored swatch + word + keyboard hint (1..N). Active chip lights its accent (cyan / lime / magenta / amber / violet). **No glyph-only buttons for note types.**

**Left sidebar tabs:**
- **Basic** — `renderProperties()`. Game Mode (mode + dimension + tracks), Audio (file + Preview Clip group), Judgment + Scoring (perfect / good / bad ms + scores), HUD (score + combo positions/sizes), Camera, Background, AI gear. **Game-mode picker lives only in the Basic tab; do not duplicate it in the top bar.**
- **Note** — selected-note inspector. Type-specific fields. For Arc: `start_track`, `end_track`, height curve preview, easing dropdown. Material slots (any note can override its material). Snap (1/4 · 1/8 · 1/12 · 1/16 · 1/24 · 1/32).
- **Material** — same content/layout as the Materials tab in StartScreenEditor (deduplicate: extract into one widget).

**Right sidebar tabs:**
- **Copilot** — natural-language → chart-edit prompt. Shows model output as a diff with Accept / Reject buttons. **Do not put properties here**; copilot is for prompts only.
- **Audit** — list of rule violations (overlap, hand cross, density spike). Severity icon + line + jump-to-time button.

**Top bar — keep simple:**
project crumbs · song name · difficulty pill · BPM · `[Test Game ▶]`.
**Do not** put a mode picker here — that lives in the Basic sidebar.

### 3.5 Player Settings  (`SettingsPageUI.cpp`, `SettingsEditor.h`)

**Source:** four sections (Audio / Gameplay / Visual / Misc). Used both by the standalone editor layer and by the in-game pause overlay.
**Mock:** `screens/player-settings.jsx`.

**Layout:** centered card on a dimmed/blurred backdrop.

**Required behaviour:**
- **Audio** — Music volume, Hit-sound volume, Hit-sounds enabled toggle, Audio offset slider (−200..+200 ms), **Tap Calibration wizard** (idle → 4 beats with progress bar + tap zone → average offset → Accept/Retry/Cancel).
- **Gameplay** — Note speed (1..10, default 5; show "Scan Line ignores this" hint).
- **Visual** — Background dim (0..1), Show FPS counter toggle.
- **Misc** — Language (en / zh / ja / ko).
- Footer: `Cancel` / `Apply`.

The mock's calibration wizard is animated; the engine's existing `drawCalibrationPanel()` already has the equivalent state machine — wire to that.

---

## 4. ImGui implementation hints

1. **Don't re-architect.** The mock matches the **layout and density** the C++ source already implements with ImGui. The work is style + reordering, not a rewrite.
2. **Style pass.** Implement `ApplyMusicGameStyle(ImGuiStyle&)`. Set every color from §2 plus rounding/spacing values.
3. **Custom widgets.**
   - `LabeledChip(label, key, color, active)` — replaces icon-only `IconButton` for note types.
   - `Slider(label, *v, min, max, suffix, accent)` — taller (22 px) with accent fill on the filled portion.
   - `SegBar(items[], *current)` — segmented control (used for Logo Type, mode pills, Visual/Audio toggles).
   - `Dropdown(items[], *current)` — dropdown with `▾` chevron.
   - `SectionHeader(label, right_widget)` — uppercase 10 px label + chevron + optional right slot (e.g. `Default` reset pill).
4. **Font setup.** Two atlases: `g_FontUI` (Inter at 11/13/16) and `g_FontMono` (JetBrains Mono at 10/11). Push/pop around timecodes, paths, numbers.
5. **Splitters.** The Song Editor center column has a horizontal splitter between scene and chart timeline (matches sys7). Use `ImGui::SplitterBehavior` (or a custom 4 px draggable bar).
6. **Drop targets.** File rows in Background/Audio/Cover use the existing `ImGui::SetDragDropTarget` plumbing — don't change.

---

## 5. What NOT to change

- All existing **functions / methods / data structures** stay. This is a UI port.
- All existing **dialogs** (Create, Add Set, Add Song, Add File, APK Build) stay; restyle their chrome only.
- All existing **commands / shortcuts** stay. The mock surfaces 1..N as note-type hints; bind these (1=Click, 2=Hold, 3=Flick, 4=Slide/Arc, 5=ArcTap).
- All existing **persistence formats** (project.json, music_selection.json, start_screen.json, charts) stay. Property fields map 1:1.

---

## 6. Acceptance checklist

- [ ] Tokens applied via `ApplyMusicGameStyle()`; no inline colors anywhere.
- [ ] Project Hub: APK build flows inline in detail panel (not a blocking modal); metadata visible.
- [ ] Start Screen: 5 collapsing headers (Background, Logo, Tap Text, Transition, Audio); Logo has Type=Text/Image; Transition has Custom + Script Path; Audio has BGM + Tap SFX.
- [ ] Music Selection: per-difficulty chart files visible; Game Mode + HUD blocks present.
- [ ] Song Editor: note toolbar uses labeled chips with key hints; Game Mode picker only in Basic sidebar; Copilot panel is for prompts only; right sidebar tabs are Copilot + Audit; center order matches §3.4 table.
- [ ] Player Settings: 4 sections (Audio / Gameplay / Visual / Misc); calibration wizard has all 3 phases.
- [ ] No emojis, no glyph-only buttons for note types, no AI slop tropes (gradients, filler stats).
