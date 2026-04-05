# System 7 — Editor UI System

**Last updated:** 2026-04-05  
**Status:** ✅ Complete (ProjectHub → StartScreen → MusicSelection → SongEditor [DAW-style layout + beat analysis + per-difficulty chart editing + live scene preview + waveform + HUD/camera/score config + chart persistence + separate-process test game] + GameFlowPreview)

The engine ships with a Unity Hub-style editor built on ImGui + Vulkan. It lets you create and manage game projects, configure each project's pages, and preview gameplay — all without leaving the application.

---

## Architecture

```
MusicGameEngineTest.exe
        ↓
    Engine::runHub()
        ↓
┌──────────────────────────────────────────────────────────┐
│                    EditorLayer enum                       │
│  ProjectHub → StartScreen → MusicSelection → SongEditor  │
│                                                  ↓        │
│                                             GamePlay      │
└──────────────────────────────────────────────────────────┘
```

Layer switching: `Engine::switchLayer(EditorLayer)`. Each layer is a self-contained ImGui panel rendered inside the main window.

**Files:**
```
engine/src/ui/
├── ImGuiLayer.h/.cpp            — ImGui init, Vulkan backend, texture management
├── ProjectHub.h/.cpp            — Project list + Create Game dialog
├── StartScreenEditor.h/.cpp     — Start screen config + live preview
├── MusicSelectionEditor.h/.cpp  — Arcaea-style card stack wheels, hierarchy, cover
├── SongEditor.h/.cpp            — Per-song chart/audio/score properties
├── SceneViewer.h/.cpp           — Gameplay viewport (Play/Stop, stats)
├── GameFlowPreview.h/.cpp       — Animated transition simulator
├── AssetBrowser.h               — Shared asset scanning + thumbnail strip
└── GifPlayer.h/.cpp             — Animated GIF playback via Vulkan textures
```

---

## Working Directory Fix

At startup, `main()` calls `GetModuleFileNameA` and sets the working directory to the exe's own directory (`build/Debug/`). This ensures all relative paths — most importantly `../../Projects` — resolve correctly regardless of how the exe is launched.

`ProjectHub::scanProjects()` stores discovered project paths as **absolute paths** (`fs::absolute(entry.path())`) so that drag-and-drop import, texture loading, and asset scanning always resolve correctly.

---

## Layer 1 — Project Hub

The first screen. Lists all projects found under `Projects/` and provides a button to create new ones.

### Project Scanning

On first render, `ProjectHub::scanProjects()` walks `../../Projects/` (absolute path), reads every `project.json`, and populates the project list. Rescanned whenever `m_scanned` resets to `false`.

### Create Game Dialog

Clicking `+ Create Game` opens a centered modal:
- **Project Name** — free text; sanitized to `[A-Za-z0-9_-]` (spaces → `_`)
- **Folder preview** — shows `Projects/<safe_name>` live as you type
- **Create** button — disabled while empty; also accepts Enter key
- **Cancel** — closes dialog, resets state
- Error in red if folder already exists or creation fails

On success, creates the following structure and switches to StartScreen:

```
Projects/<ProjectName>/
├── project.json          — name, version, window size, asset paths
├── start_screen.json     — start screen settings
├── music_selection.json  — music sets and songs
└── assets/
    ├── charts/
    │   └── demo.json     — empty UCF stub chart
    ├── audio/
    └── textures/
```

### project.json Format

```json
{
  "name": "MyGame",
  "version": "1.0.0",
  "engineVersion": "1.0.0",
  "window": { "width": 1280, "height": 720, "title": "MyGame" },
  "paths": {
    "charts": "assets/charts",
    "audio": "assets/audio",
    "shaders": "../../build/shaders"
  },
  "defaultChart": "assets/charts/demo.json"
}
```

---

## Layer 2 — Start Screen Editor

Design the opening screen: background, logo, tap text, audio, and transition effect — with live preview.

### Layout

```
┌──────────────────────┬────────────────────────┐
│                      │  Properties            │
│   Preview (live)     │  ▼ Background          │
│                      │  ▼ Logo                │
│                      │  ▼ Tap Text            │
│                      │  ▼ Transition Effect   │
│                      │  ▼ Audio               │
├──────────────────────┴────────────────────────┤
│  Asset Panel  [thumbnail strip]               │
├───────────────────────────────────────────────┤
│  < Back   Save   Load   Reset   Next: Music > │
└───────────────────────────────────────────────┘
```

Default split: 60/40 horizontal, 72/28 vertical. Drag splitter bars to resize.

### Preview Panel

Renders live via `ImDrawList`. Elements:
- **Background** — `dl->AddImage` (static image), per-frame Vulkan textures (GIF via GifPlayer), or video placeholder text
- **Logo** — Roboto-Medium TTF at configured size via `dl->AddText`. Bold: fake double-draw with 1px offset. Glow: 8 offset draws before main
- **Tap text** — `getLogoFont(tapTextSize)` → `dl->AddText`; size slider updates preview immediately

### Properties Panel

#### Background
Drag a thumbnail from the Asset Panel onto the drop zone (highlights blue on hover). Clear button removes it and unloads the Vulkan texture.

Supported: `.png`, `.jpg`, `.gif` (animated via GifPlayer), `.mp4`/`.webm` (placeholder text)

#### Logo
| Field | Type | Range |
|---|---|---|
| Logo Type | combo | Text / Image |
| Text | input | — |
| Font Size | slider | 12–96 px (Roboto-Medium) |
| Color | color picker | RGBA |
| Bold / Italic | checkboxes | — |
| Position | float2 slider | Normalized [0,1] |
| Scale | slider | 0.1–10.0 |
| Glow / Outline | checkbox | — |
| Glow Color | color picker | RGBA |
| Glow Radius | slider | 1–32 px |

Image mode: drag image thumbnail to logo drop zone.

#### Tap Text
| Field | Type | Range |
|---|---|---|
| Text | input | e.g. "Tap to Start" |
| Position | float2 slider | Normalized [0,1] |
| Size | int slider | 12–120 px |

#### Transition Effect
| Field | Type | Values |
|---|---|---|
| Effect | combo | Fade / SlideLeft / ZoomIn / Ripple / Custom |
| Duration | slider | 0.1–2.0 s |
| Script Path | input | Lua path (Custom only, stub) |

**Critical rule:** GameFlowPreview reads this effect + duration directly. Must not hardcode. (Prior bug: effects were hardcoded and didn't match the selector.)

#### Audio
Background Music and Tap SFX: drag audio tiles from Asset Panel onto their zones.

| Field | Details |
|---|---|
| Drop zone | Shows filename when set, "Drop audio here" when empty |
| Clear | Removes path |
| Volume | 0.0–1.0 |
| Loop | Checkbox (BGM only) |

Supported formats: `.mp3`, `.ogg`, `.wav`, `.flac`, `.aac`

### start_screen.json Format

```json
{
  "background": { "file": "assets/textures/bg.png", "type": "image" },
  "logo": {
    "type": "text",
    "text": "MyGame",
    "fontSize": 48.0,
    "color": [1.0, 1.0, 1.0, 1.0],
    "bold": false,
    "italic": false,
    "imageFile": "",
    "glow": false,
    "glowColor": [1.0, 0.8, 0.2, 0.8],
    "glowRadius": 8.0,
    "position": { "x": 0.5, "y": 0.3 },
    "scale": 1.0
  },
  "tapText": "Tap to Start",
  "tapTextPosition": { "x": 0.5, "y": 0.8 },
  "tapTextSize": 24,
  "transition": {
    "effect": "fade",
    "duration": 0.5,
    "customScript": ""
  },
  "audio": {
    "bgMusic": "assets/audio/bgm.mp3",
    "bgMusicVolume": 1.0,
    "bgMusicLoop": true,
    "tapSfx": "assets/audio/tap.wav",
    "tapSfxVolume": 1.0
  }
}
```

`transition.effect` values: `fade`, `slide_left`, `zoom_in`, `ripple`, `custom`.  
**Backward compatible:** `load()` accepts both old flat format and current nested format.

### Nav Bar (always visible)

| Button | Action |
|---|---|
| `< Back` | Return to Project Hub |
| `Save` | Write `start_screen.json`. Green "Saved!" for 2 s |
| `Load` | Reload from `start_screen.json` |
| `Reset` | Restore defaults (does not save) |
| `Next: Music Selection >` | Switch to MusicSelection layer |

---

## Layer 3 — Music Selection Editor

Arcaea-style music selection page with interactive card stack wheels and a full hierarchy panel.

### Card Stack Wheels

```
┌────────────┬───────────────────────────┬──────────────────────┐
│  Left Wheel│      Center Panel         │   Right Hierarchy    │
│  Music Sets│  [Cover 100×100]          │   ▼ Set A            │
│  [card]    │  [Easy] [Med] [Hard]      │     Song 1           │
│  [card]    │  [  START  ]              │     Song 2           │
│  [card]    │                           │   ▼ Set B            │
│            │                           │     Song 3           │
│  Right     │                           │                      │
│  Wheel     │                           │  [Add Set] [Add Song]│
│  Songs in  │                           │  [Delete]            │
│  selected  │                           │                      │
│  set       │                           │                      │
├────────────┴───────────────────────────┴──────────────────────┤
│  Asset Browser (thumbnail strip)                              │
└───────────────────────────────────────────────────────────────┘
```

- **Left wheel:** music sets. Perspective-skewed quads in painter's order (furthest first).
- **Right wheel:** songs within the selected set — name, score, achievement badge.
- Quads drawn with `AddQuadFilled` / `AddImageQuad` and manual skew transforms.

### Center Panel
- Cover photo: 100×100 thumbnail via `getCoverDesc()`
- Difficulty buttons: Easy / Medium / Hard (active difficulty highlighted)
- START button (future: launches gameplay)

### Cover Image Picker (also used in hierarchy inline editing)
1. Browse button → `OPENFILENAMEW` → copies to `assets/textures/`
2. Drag-drop target → accepts `"ASSET_PATH"` payload
3. Clear button → resets path + evicts thumbnail cache entry

### Right Panel — Hierarchy (70/30 split, always visible)
- Tree: Music Sets → Songs (collapsible)
- Add / Delete music sets and songs
- Inline property editing: name, artist, cover image picker
- Double-click a song → navigates to SongEditor layer

### Persistence

`music_selection.json` — stores all music sets, songs, cover paths, difficulty, score/achievement, and per-song game mode config.

---

## Layer 4 — Song Editor

Per-song properties + chart editor. Receives `SongInfo*` — edits in-place.

### DAW-Style Layout (Restructured 2026-04-04)

Old layout was tab-based (Scene OR Editor, not both visible simultaneously). New DAW-style layout shows scene preview and chart timeline at the same time:

```
┌─────────────┬───────────────────────────────────────────────────┐
│ Left Sidebar│ Preview (scene view)                              │
│ (scrollable)├── draggable splitter (m_sceneSplit) ──────────────┤
│             │ Chart Timeline                                    │
│ - Song Info │ [Toolbar: Analyze | Clear | Place All | Clear Nts]│
│ - Audio     │ [Difficulty: Easy | Medium | Hard]                │
│ - Game Mode │ [Track lanes + notes + markers]                   │
│ - Config    ├───────────────────────────────────────────────────┤
│ - Assets    │ Waveform Strip (120px)                            │
│             ├───────────────────────────────────────────────────┤
│             │ [< Back] [Save] [Test Game]  ▶ ⏸ ⏹  00:12.345   │
└─────────────┴───────────────────────────────────────────────────┘
```

Key layout changes:
- **Scene preview and chart timeline visible simultaneously** (no tab switching)
- **Left sidebar** (280px default, draggable via `m_sidebarW`) holds Properties + Config + Assets in one scrollable panel
- `m_sidebarW` (pixel width) replaces old `m_hSplit` ratio
- `m_sceneSplit` controls scene/timeline vertical ratio (draggable)
- **Compact nav bar** at the bottom with Play/Pause/Stop audio controls and current time display

### "Test Game" Button (Separate Process)

Green button always visible at the top-right corner of **all editor pages** (StartScreen, MusicSelection, SongEditor). Launches a **child process** (`MusicGameEngineTest.exe --test <project_path>`) that opens its own "Test Game" window (1280x720) running the full game flow: StartScreen → MusicSelection → Gameplay.

Key behaviors:
- The editor window is completely unaffected — no freezing, no state corruption
- ESC or the Exit button closes only the test window
- Multiple test runs work without crashes (each is an independent process)
- The child process reads the same project files, so Save before Test to pick up latest changes

### Toolbar Buttons (Added 2026-04-04)

The chart timeline area has a toolbar row with the following buttons:

| Button | Color | Action |
|---|---|---|
| **Analyze Beats** | Green | Triggers Madmom beat analysis for all 3 difficulties (see `AudioAnalyzer`). Shows "Analyzing..." while running. Results populate `m_diffMarkers`. Error popup on failure. |
| **Clear Markers** | — | Clears all markers for the current difficulty only |
| **Place All** | — | Places a Tap note on every marker in the current difficulty, distributing notes across tracks round-robin |
| **Clear Notes** | — | Clears all notes for the current difficulty |

### Audio Controls in Nav Bar

The bottom nav bar includes Play/Pause/Stop controls for audio playback:
- **Play (▶)** — starts audio playback from current position
- **Pause (⏸)** — pauses audio playback
- **Stop (⏹)** — stops audio and resets to beginning
- **Time display** — shows current playback position as `mm:ss.ms`

### Right-Click Delete Enhancement (2026-04-04)

Right-click to delete now works in **any mode** (not just when a note-placement tool is active). The delete logic tries to delete a note first (within 5px threshold), then falls back to deleting a marker if no note was found. This replaces the previous behavior where right-click only worked in note tool modes.

### Difficulty System

SongEditor supports **Easy / Medium / Hard** difficulty levels. The difficulty selector is shown in the chart timeline toolbar.

- Notes and markers are stored **per-difficulty** in `m_diffNotes` and `m_diffMarkers` maps keyed by `(int)Difficulty`
- Switching difficulty instantly swaps the visible notes and markers
- Each difficulty has its own independent chart that is saved and loaded separately

```cpp
enum class Difficulty { Easy = 0, Medium = 1, Hard = 2 };

std::map<int, std::vector<EditorNote>> m_diffNotes;    // per-difficulty notes
std::map<int, std::vector<float>>      m_diffMarkers;  // per-difficulty markers
```

### Scene Preview (Top Right)

Shows a **live game preview** at the current cursor time position. Renders the actual game mode (perspective highway for BanG Dream, concentric circles for Lanota, scan line for Cytus) with editor notes positioned relative to the cursor time. Includes a simulated **score and combo HUD** display using the configured HUD settings. When no chart data exists, displays an empty dark background with "No chart data — create a chart to see the preview". Always visible alongside the chart timeline (no tab switching required).

### Chart Timeline (Below Scene Preview)

#### Note Tool Toolbar

```
[Marker] [Click] [Press] [Slide]   "Click end point..."
```

- **Marker** (default): left-click places orange dashed marker lines (existing behavior)
- **Click**: single left-click places a Click note (diamond shape, blue)
- **Press**: first click sets start point, second click sets end point (bar with caps, green). Shows "Click end point..." while waiting for second click
- **Slide**: single left-click places a Slide note (arrow/triangle, purple). Hidden for Scan Line mode
- Active tool is highlighted with color; click again to toggle off
- 3D Sky restriction: Slide notes cannot be placed on sky lanes (hint shown in toolbar)
- All note placements **snap to the nearest marker** — markers act as guides with an adsorption feel

- **Top — Chart timeline** (all modes use `renderChartTimeline`):
  - **Drop Notes 2D / Circle**: horizontal track lanes (one per track, e.g. 7 tracks = 7 lanes), numbered labels on the left, subtle lane separators — no vertical grid lines
  - **Drop Notes 3D**: two stacked regions, each with `trackCount` lanes:
    - **Sky** (upper 40%, purple tint) — purple-tinted lane separators, "Sky" label
    - **Ground** (lower 60%, blue tint) — blue-tinted lane separators, "Ground" label
    - 4px gap between regions for visual separation
  - **Scan Line**: same Y-range view with zigzag scan path visualization (⚠️ incomplete — needs proper track lane layout)
  - **Notes on timeline**: each note is drawn with 3 concentric judgment bands (outermost to innermost):
    - **Red (Bad)** — faint red band at `+/- badMs`
    - **Yellow (Good)** — yellow band at `+/- goodMs`
    - **Perfect** — color depends on note type (blue-cyan for Click, green for Press, purple for Slide) at `+/- perfectMs`
    - Beyond Bad = Miss (no band drawn)
    - Band widths update live when judgment window sliders change
  - **Note shapes** (no text labels — perfect band color identifies type):
    - **Click** — diamond (quad), blue fill + light blue outline
    - **Press** — bar from start to end with caps, green fill + outline; judgment bands at both ends
    - **Slide** — right-pointing triangle/arrow, purple fill + outline
  - **Pending Press preview**: green highlight bar from start point to current mouse position while waiting for second click
  - **Judge line**: yellow vertical line that follows the mouse cursor position in real-time across the timeline (tracks mouse X directly). Shows time label at bottom.
  - **Playback cursor**: red vertical line at current audio playback position with mm:ss.ms timestamp
  - **Marker + note count**: yellow "Markers: N  Notes: N" debug indicator at top-left

### Waveform Strip (Always Visible)

The audio waveform strip was moved **out of the Editor tab** and is now always visible below the Scene/Editor tabs as a **fixed 120px strip**. Layout order: Top (Scene or Editor tab) → Waveform strip → splitter → Bottom (Assets | Config).

- Always visible regardless of which tab (Scene or Editor) is active
- Synchronized scroll/zoom with the chart timeline
- Shows markers, playback cursor, and hover line
- Decoded from loaded audio file (16384-bucket envelope)

### Interaction Controls

| Input | Action | Where |
|---|---|---|
| Mouse wheel | Scroll track forward/backward | Timeline + Waveform |
| Ctrl + mouse wheel | Zoom in/out (resolution 10–2000 px/s), zooms toward cursor | Timeline + Waveform |
| Mouse hover | White vertical line + time label (mm:ss.ms) follows cursor | Timeline + Waveform |
| Left-click (Marker tool) | Place an orange dashed marker line at that time | Timeline + Waveform |
| Left-click (Click/Slide tool) | Place a note snapped to nearest marker | Timeline |
| Left-click (Press tool) | First click = start point, second click = end point | Timeline |
| Right-click (Marker tool) | Remove nearest marker within 5px | Timeline + Waveform |
| Right-click (Note tool active) | Remove nearest note within 5px | Timeline |

Note: Marker placement uses raw `ImGuiIO::MouseClicked[]` input (bypasses ImGui widget system) to ensure clicks register at any zoom level. The parent child window (`SETop`) has `NoScrollWithMouse | NoScrollbar` flags to prevent ImGui from consuming wheel events.

### Markers

User-placed markers are stored as `std::vector<float> m_markers` (time in seconds). They appear as **orange dashed vertical lines** on both the chart timeline and the waveform, staying synchronized. Markers help the user visually mark positions where notes should be placed.

- Dashed effect: 4px drawn / 4px gap pattern
- Color: `IM_COL32(255, 140, 60, 180)` (orange)
- Markers persist across scroll/zoom operations
- Visible on both chart timeline and waveform panels simultaneously

### Note Placement

Notes are placed on the chart timeline using the note tool toolbar. Each note snaps to the nearest marker, so users place markers first to define time positions, then switch to a note tool and click to place notes.

#### Editor Note Types

```cpp
enum class EditorNoteType { Click, Press, Slide };

struct EditorNote {
    EditorNoteType type;
    float    time;       // start time (snapped to marker)
    float    endTime;    // end time for Press notes (0 for Click/Slide)
    int      track;      // 0-based track index (determined by mouse Y)
    bool     isSky;      // true = sky lane (3D mode only)
};

enum class NoteTool { None, Click, Press, Slide };
```

#### Placement Rules

| Mode | Available note types |
|---|---|
| 2D Drop Notes | Click, Press, Slide |
| Circle | Click, Press, Slide |
| 3D Drop Notes — Ground | Click, Press, Slide |
| 3D Drop Notes — Sky | Click, Press only (no Slide) |
| Scan Line | Click, Press only (no Slide button shown) |

- **Snap to marker**: `snapToMarker()` finds the nearest marker time to the mouse click position
- **Track from Y**: `trackFromY()` determines which track lane the mouse is in based on vertical position
- **Press two-click**: state tracked by `m_pressFirstClick`, `m_pressStartTime`, `m_pressStartTrack`, `m_pressStartSky`; auto-swaps if end < start
- **Right-click delete**: removes nearest note within 5px threshold (cancels any pending Press)

### Per-Song Game Mode Config

Each song carries a `GameModeConfig` struct (stored in `SongInfo`):

```cpp
enum class GameModeType { DropNotes, Circle, ScanLine };
enum class DropDimension { TwoD, ThreeD };

struct HudTextConfig {
    float position[2] = {0.5f, 0.05f};  // normalized screen position
    float fontSize     = 32.f;
    float scale        = 1.0f;
    float color[4]     = {1.f, 1.f, 1.f, 1.f};
    bool  bold         = false;
    bool  glow         = false;
};

struct GameModeConfig {
    GameModeType  type       = GameModeType::DropNotes;
    DropDimension dimension  = DropDimension::TwoD;   // only for DropNotes
    int           trackCount = 7;                      // 3..12, all modes

    // Judgment windows (milliseconds, +/- from note center)
    float perfectMs = 50.f;   // +/- 50ms
    float goodMs    = 100.f;  // +/- 100ms
    float badMs     = 150.f;  // +/- 150ms
    // Beyond badMs = Miss

    // Score per judgment
    int perfectScore = 1000;
    int goodScore    = 500;
    int badScore     = 100;

    // Achievement images (FC / AP)
    std::string fcImage;   // path to Full Combo badge image
    std::string apImage;   // path to All Perfect badge image

    // HUD config (score & combo display)
    HudTextConfig scoreHud;
    HudTextConfig comboHud;

    // Camera settings
    float cameraEye[3]    = {0.f, 5.f, 10.f};
    float cameraTarget[3] = {0.f, 0.f, 0.f};
    float cameraFov       = 45.f;
};
```

### Judgment Windows

Configurable per-song via sliders in the Game Mode Config panel. Each slider is clamped: Perfect ≤ Good ≤ Bad.

| Judgment | Default | Color | Visual |
|---|---|---|---|
| Perfect | +/- 50ms | Per-type (blue-cyan / green / purple) | Innermost band |
| Good | +/- 100ms | Yellow | Middle band |
| Bad | +/- 150ms | Red | Outermost band |
| Miss | Beyond Bad | — | No band |

The perfect band color distinguishes note types without text labels:
- **Click**: blue-cyan (`IM_COL32(40, 160, 220, 80)`)
- **Press**: green (`IM_COL32(40, 200, 80, 80)`)
- **Slide**: purple (`IM_COL32(180, 60, 200, 80)`)

#### Score Subsection

Below the judgment window sliders, a "Score" subsection with `InputInt` fields:

| Field | Default | Description |
|---|---|---|
| Perfect Score | 1000 | Points awarded per Perfect judgment |
| Good Score | 500 | Points awarded per Good judgment |
| Bad Score | 100 | Points awarded per Bad judgment |

#### Achievements Section

Below Score, an "Achievements" section with FC (Full Combo) and AP (All Perfect) image pickers. Each picker supports:
- **Browse** button — file dialog to select an image
- **Drag-drop** — accepts `"ASSET_PATH"` payload from asset panel
- **Thumbnail preview** — shows the selected badge image inline
- **Clear** button — removes the image path

### HUD Config (Score & Combo)

New collapsible "HUD — Score & Combo" section in the config panel. Configures how the score and combo counters appear during gameplay.

Each HUD element (`scoreHud`, `comboHud`) uses the `HudTextConfig` struct:

| Field | Type | Description |
|---|---|---|
| Position | float2 slider | Normalized screen position [0,1] |
| Font Size | slider | Text size in pixels |
| Scale | slider | Additional scale multiplier |
| Color | color picker | RGBA |
| Bold | checkbox | Fake-bold double-draw |
| Glow | checkbox | Glow effect behind text |

HUD renders in **both** the Scene tab preview and actual gameplay.

### Camera Config

New "Camera" section in the config panel. Controls the 3D camera used by game mode renderers (e.g., BandoriRenderer uses these settings for the perspective highway).

| Field | Widget | Description |
|---|---|---|
| Camera Eye | DragFloat3 | Camera position in world space |
| Camera Target | DragFloat3 | Look-at point in world space |
| Camera FOV | SliderFloat | Field of view in degrees |

### Hit Effects & Judgment Display

Judgment display squares were removed. Only **particle effects** remain:

| Judgment | Particles | Color |
|---|---|---|
| Perfect | 20 particles | Green |
| Good | 14 particles | Blue |
| Bad | 8 particles | Red |
| Miss | None | — |

JudgmentDisplay colors: Perfect = green, Good = blue, Bad = red. Miss detection properly dispatches Miss judgments (no visual effect).

### Game Modes

- **Basic Drop Notes**: perspective highway with converging lanes
  - **2D (Ground Only)**: single judge line at the bottom
  - **3D (Ground + Sky)**: two judge lines at different heights (like Arcaea — ground track + sky input)
- **Circle**: Lanota-style concentric circles with radial track sectors, trapezoid notes
- **Scan Line**: Cytus-style horizontal scan line, circle notes scattered across screen

Users can mix different styles within the same music set — each song is independent.

### Audio Waveform (Technical Details)

- AudioEngine extended with `static WaveformData decodeWaveform(path, bucketCount)` using `ma_decoder`
- Decodes audio to mono f32, computes min/max envelope per bucket
- Cached in SongEditor — re-decoded only when audio file path changes
- Rendered via ImDrawList as vertical lines (min→max per pixel)
- Playback cursor with mm:ss.ms timestamp, duration label
- Now rendered in the always-visible waveform strip (see "Waveform Strip" above)

### Timeline State Variables

```cpp
float m_timelineScrollX  = 0.f;    // horizontal scroll offset in seconds
float m_timelineZoom     = 100.f;  // pixels per second (10–2000)
float m_timelineVSplit   = 0.65f;  // timeline / waveform vertical split
float m_hoverTime        = -1.f;   // time under mouse cursor (-1 = none), reset each frame

// Per-difficulty storage (replaces flat m_markers and m_notes)
Difficulty m_difficulty = Difficulty::Easy;
std::map<int, std::vector<float>>      m_diffMarkers;  // keyed by (int)Difficulty
std::map<int, std::vector<EditorNote>> m_diffNotes;    // keyed by (int)Difficulty

// Note editing state
NoteTool  m_noteTool;              // active tool (None = marker mode)
bool      m_pressFirstClick;       // true = waiting for second click to finish Press
float     m_pressStartTime;        // start time of pending Press note
int       m_pressStartTrack;       // track of pending Press note
bool      m_pressStartSky;         // sky flag of pending Press note
```

### Technical Notes — Input Handling

All hover, click, and scroll handling in the chart timeline and waveform uses **raw mouse position checks** (`ImGuiIO::MousePos` bounds test) instead of `ImGui::IsMouseHoveringRect()`. This bypasses ImGui's internal window hover state tracking, which can become stale after scroll/zoom operations and prevent clicks from registering.

Marker placement specifically uses `ImGui::GetIO().MouseClicked[0]` (raw input flag) rather than `ImGui::IsMouseClicked()` (widget-aware API). The parent child window (`SETop`) is created with `ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar` to prevent ImGui from intercepting mouse wheel events intended for custom zoom/scroll.

### Persistence

Game mode config is saved per-song in `music_selection.json`:

```json
{
  "name": "Song Title",
  "artist": "Artist",
  "gameMode": {
    "type": "dropNotes",
    "dimension": "3D",
    "trackCount": 7,
    "perfectMs": 50,
    "goodMs": 100,
    "badMs": 150,
    "perfectScore": 1000,
    "goodScore": 500,
    "badScore": 100,
    "fcImage": "",
    "apImage": "",
    "scoreHud": { "position": [0.5, 0.05], "fontSize": 32, "scale": 1.0, "color": [1,1,1,1], "bold": false, "glow": false },
    "comboHud": { "position": [0.5, 0.15], "fontSize": 32, "scale": 1.0, "color": [1,1,1,1], "bold": false, "glow": false },
    "cameraEye": [0, 5, 10],
    "cameraTarget": [0, 0, 0],
    "cameraFov": 45
  }
}
```

### Chart Persistence

The Save button now calls `exportAllCharts()` which writes unified JSON chart files to `assets/charts/`. Charts are saved **per-difficulty** (e.g., `song_easy.json`, `song_medium.json`, `song_hard.json`). When a song is opened via `setSong()`, chart files are loaded back into editor notes. Charts survive across sessions.

Flow:
1. **Save** → `exportAllCharts()` → writes `assets/charts/<song>_<difficulty>.json` for each difficulty
2. **Open song** → `setSong()` → scans `assets/charts/` for matching files → loads into `m_diffNotes` and `m_diffMarkers`

### Unified Asset System

All editors use the shared `importAssetsToProject()` function from `AssetBrowser.h`. Same routing logic everywhere. All "Open File..." dialogs default to "All Files" filter. Assets imported on any page are visible on all pages.

### Other Properties

- Audio file: text input + Browse button + drag-drop from asset panel
- Chart paths removed from properties (handled in Editor tab)
- Score config: per-judgment score values (Perfect/Good/Bad) in config panel
- Achievement images: FC/AP badge image pickers in config panel
- Save: calls `engine->musicSelectionEditor().save()` → writes `music_selection.json` + `exportAllCharts()` → writes chart JSON files

---

## Game Flow Preview (via "Test Game" button)

The green **"Test Game"** button at top-right of all editor pages launches a **separate child process** (`MusicGameEngineTest.exe --test <project_path>`). The child process opens its own 1280x720 window running the full game flow:

**StartScreen → (tap) → MusicSelection → (START) → Gameplay**

### Transition Effects in Test Mode

- **StartScreen → MusicSelection**: fade-to-black transition
- **MusicSelection**: fades in from black

| Effect | Behavior |
|---|---|
| Fade | Cross-fade alpha blend between pages |
| SlideLeft | Outgoing slides left, incoming enters from right |
| ZoomIn | Scale + fade transition |
| Ripple | Circular reveal from click point |
| Custom | Falls back to Fade |

The test process reads transition settings from `start_screen.json`. ESC or Exit closes only the test window. The editor window is completely unaffected. Multiple test runs work without crashes.

**Must read `TransitionEffect` + `duration` from StartScreenEditor properties** — never hardcode. (See Critical Rules below.)

---

## Layer 5 — Scene Viewer (Gameplay Viewport)

`SceneViewer` renders the active game scene as an ImGui texture viewport.

- **Play/Stop** — controls game execution; paused = frozen scene
- **Stats** — FPS, frame time, song time, play status

### Render Flow
```
1. beginFrame()          — acquire swapchain image, begin scene render pass
2. Game renders to offscreen RGBA16F (only if playing)
3. End scene render pass
4. Bloom compute passes on scene texture
5. Begin swapchain render pass
6. ImGui renders UI; scene texture shown in SceneViewer viewport
7. End swapchain render pass
8. Submit & present
```

---

## ImGuiLayer

Owns the ImGui context and Vulkan descriptor pool (32 texture slots).

| Method | Details |
|---|---|
| `init(window, ctx, renderPass)` | Sets up GLFW + Vulkan backends; loads Roboto-Medium.ttf at 24/32/48/64 px |
| `addTexture(view, sampler)` | Registers Vulkan texture → `ImTextureID` for use in `ImGui::Image()` |
| `getLogoFont(targetSize)` | Returns Roboto font closest to `targetSize`; falls back to default |
| `beginFrame()` / `endFrame()` / `render(cmd)` | Per-frame lifecycle |

Font file: `third_party/imgui/misc/fonts/Roboto-Medium.ttf`

---

## Asset Browser Pattern (shared across all editors)

```
┌──────────────────────────────────────────────────────┐
│  Editor content (main area)                          │
├─── draggable splitter (m_vSplit) ────────────────────┤
│  Asset Panel strip                                   │
│  [img 80×80] [img 80×80] [MUS] [img 80×80] ...       │
│  [Open File...]                                      │
└──────────────────────────────────────────────────────┘
```

- `scanAssets()` uses `fs::absolute()` before `fs::relative()` (Windows path fix)
- Thumbnails: 80×80, lazy-loaded, cached in `m_thumbCache` (path → `ImTextureID`)
- Drag source: `"ASSET_PATH"` ImGui payload with relative path string
- Audio tiles: 80×80 blue tile labeled "MUS"
- Right-click → Delete: removes from disk, clears references, evicts cache, rescans
- Import routing:

| Extension | Destination |
|---|---|
| `.png` `.jpg` `.gif` | `assets/textures/` |
| `.mp3` `.ogg` `.wav` `.flac` `.aac` | `assets/audio/` |
| `.mp4` `.webm` | `assets/videos/` |

**Rule: ALL editor pages must have the asset browser strip.**

---

## Critical Rules

1. **GameFlowPreview must read TransitionEffect from StartScreenEditor.** Never hardcode effects. (Prior bug: options didn't match preview.)
2. **GLFW user pointer belongs to Engine.** `InputManager::init()` is a no-op — must not call `glfwSetWindowUserPointer`. (Prior bug: InputManager overwrote Engine's user pointer.)
3. **All file paths stored as absolute paths** in ProjectHub (`fs::absolute()`).
4. **Cover picker always has Browse + Drag-drop + Clear.** Never only one method.
5. **All editors share the asset browser panel.** User-confirmed requirement.

---

## Known Issues

**Window maximize — black borders**  
Clicking Windows maximize causes black borders. Root cause: Vulkan swapchain doesn't recreate immediately. Workaround: drag window edges manually.

**Validation warnings on rapid texture reload**  
Sampler-destroyed-while-referenced warnings when switching backgrounds fast. No crash in practice. Fix: deferred destruction or `vkDeviceWaitIdle` before unload.

**Scan Line editor timeline — incomplete**  
The Scan Line mode in the Editor tab currently shows a zigzag scan path visualization but does not have a proper track lane layout for note placement like the Drop Notes and Circle modes. Needs redesign for Cytus-style editing (notes placed in 2D screen space rather than discrete track lanes).

---

## Implementation Status

| Feature | Status |
|---|---|
| ImGui + Vulkan integration | ✅ done |
| ImGui descriptor pool (32 slots) | ✅ done |
| Roboto-Medium TTF at 24/32/48/64 px | ✅ done |
| Working directory anchored to exe location | ✅ done |
| Project Hub — list projects (absolute paths) | ✅ done |
| Project Hub — Create Game dialog + scaffolding | ✅ done |
| Start Screen Editor — resizable panels | ✅ done |
| Start Screen Editor — live preview (background, logo, tap text) | ✅ done |
| Start Screen Editor — logo text (Roboto, color, size, bold, glow) | ✅ done |
| Start Screen Editor — logo image drag-drop | ✅ done |
| Start Screen Editor — background PNG/JPG/GIF/video | ✅ done |
| Start Screen Editor — transition effect selector (4 presets + custom) | ✅ done |
| Start Screen Editor — audio section (BGM, SFX, volume, loop) | ✅ done |
| Start Screen Editor — asset panel (thumbnails, audio tiles, drag-drop) | ✅ done |
| Start Screen Editor — asset import (drag-drop + Open File multi-select) | ✅ done |
| Start Screen Editor — save/load JSON (backward compatible) | ✅ done |
| Music Selection Editor — Arcaea-style card stack wheels | ✅ done |
| Music Selection Editor — left wheel (music sets) | ✅ done |
| Music Selection Editor — right wheel (songs with score/badge) | ✅ done |
| Music Selection Editor — center panel (cover, difficulty, START) | ✅ done |
| Music Selection Editor — hierarchy panel (tree, add/delete, inline edit) | ✅ done |
| Music Selection Editor — cover image picker (Browse + drag-drop + Clear) | ✅ done |
| Music Selection Editor — asset panel | ✅ done |
| Music Selection Editor — persistence (music_selection.json) | ✅ done |
| Song Editor — per-song game mode config (DropNotes/Circle/ScanLine) | ✅ done |
| Song Editor — dimension toggle (2D Ground / 3D Ground+Sky) for DropNotes | ✅ done |
| Song Editor — track count slider (3-12) for all modes | ✅ done |
| Song Editor — difficulty selector (Easy/Medium/Hard) with per-difficulty notes/markers | ✅ done |
| Song Editor — Scene tab (live game mode preview at cursor time + score/combo HUD) | ✅ done |
| Song Editor — Editor tab (chart timeline + note tools) | ✅ done |
| Song Editor — chart timeline: 2D/Circle = horizontal track lanes (no grid) | ✅ done |
| Song Editor — chart timeline: 3D = Sky (purple) + Ground (blue) dual track regions | ✅ done |
| Song Editor — chart timeline: judge line follows mouse cursor | ✅ done |
| Song Editor — hover vertical line + time label on timeline and waveform | ✅ done |
| Song Editor — waveform strip (always visible, 120px, below Scene/Editor tabs) | ✅ done |
| Song Editor — scroll (mouse wheel) + zoom (Ctrl+wheel, 10-2000 px/s) | ✅ done |
| Song Editor — timestamps (cursor mm:ss.ms, zoom status, duration label) | ✅ done |
| Song Editor — marker placement (left-click on waveform = orange dashed line) | ✅ done |
| Song Editor — marker removal (right-click near marker on waveform) | ✅ done |
| Song Editor — markers visible on both chart timeline and waveform | ✅ done |
| Song Editor — raw IO input for markers (works at any zoom level) | ✅ done |
| Song Editor — note tool toolbar (Marker/Click/Press/Slide) | ✅ done |
| Song Editor — note placement with marker snapping (adsorption) | ✅ done |
| Song Editor — Click notes (single click, diamond shape, blue) | ✅ done |
| Song Editor — Press notes (two-click start/end, bar with caps, green) | ✅ done |
| Song Editor — Slide notes (single click, arrow shape, purple) | ✅ done |
| Song Editor — 3D Sky lane restriction (Click + Press only, no Slide) | ✅ done |
| Song Editor — judgment window settings (Perfect/Good/Bad sliders) | ✅ done |
| Song Editor — judgment bands on notes (red Bad, yellow Good, colored Perfect) | ✅ done |
| Song Editor — perfect band color distinguishes note type (no text labels) | ✅ done |
| Song Editor — right-click to delete notes | ✅ done |
| Song Editor — pending Press preview (green bar to mouse) | ✅ done |
| Song Editor — scan line editor (track lane layout) | ⚠️ incomplete |
| Song Editor — vertical layout (preview top, assets+config bottom) | ✅ done |
| Song Editor — audio path + Browse + drag-drop | ✅ done |
| Song Editor — score + achievement | ✅ done |
| Song Editor — asset panel | ✅ done |
| Song Editor — score config (Perfect/Good/Bad score values) | ✅ done |
| Song Editor — achievement image pickers (FC/AP with browse, drag-drop, preview) | ✅ done |
| Song Editor — HUD config (score & combo position, font, color, bold, glow) | ✅ done |
| Song Editor — camera config (eye, target, FOV) | ✅ done |
| Song Editor — hit effects (particle only: green Perfect, blue Good, red Bad) | ✅ done |
| Song Editor — chart persistence (exportAllCharts → assets/charts/ JSON) | ✅ done |
| Song Editor — chart loading on setSong() (survives across sessions) | ✅ done |
| Song Editor — save via musicSelectionEditor().save() + exportAllCharts() | ✅ done |
| Song Editor — gameMode persisted in music_selection.json | ✅ done |
| Song Editor — DAW-style layout (scene + timeline simultaneous, sidebar) | ✅ done |
| Song Editor — toolbar: Analyze Beats (Madmom integration) | ✅ done |
| Song Editor — toolbar: Clear Markers / Place All / Clear Notes | ✅ done |
| Song Editor — audio controls in nav bar (Play/Pause/Stop + time) | ✅ done |
| Song Editor — right-click delete in any mode (note first, then marker) | ✅ done |
| Beat analysis via Madmom (Easy/Medium/Hard auto-markers) | ✅ done |
| "Test Game" as separate process (MusicGameEngineTest.exe --test) | ✅ done |
| Unified asset import system (importAssetsToProject in AssetBrowser.h) | ✅ done |
| All "Open File..." dialogs default to "All Files" filter | ✅ done |
| Game Flow Preview — Fade/SlideLeft/ZoomIn/Ripple effects | ✅ done |
| Game Flow Preview — reads TransitionEffect from StartScreenEditor | ✅ done |
| Test mode — StartScreen→MusicSelection fade-to-black transition | ✅ done |
| Test mode — MusicSelection fade-in from black | ✅ done |
| Scene Viewer — gameplay viewport + Play/Stop + stats | ✅ done |
| Gameplay integration (START / Test Game → launch mode) | ✅ done (separate process) |
| HUD rendering (score/combo overlay) | ✅ done |
| Chart editor — note placement on markers | ✅ done |
| Chart editor — note types (Click, Press, Slide) | ✅ done |
| Chart editor — judgment window config (Perfect/Good/Bad/Miss) | ✅ done |
| Deferred Vulkan texture destruction | 🔲 not started |
