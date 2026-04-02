# Music Game Engine

A C++20/Vulkan-based rhythm game engine with a Unity Hub-style editor for mobile rhythm game development.

---

## Architecture

**Engine as Library** — `MusicGameEngine` builds as a static library  
**Project Hub** — Unity Hub-style launcher for browsing and creating projects  
**Start Screen Editor** — per-project start screen designer with live preview  
**Standalone Projects** — each game project links against the engine library

```
MusicGameEngine/
├── engine/
│   ├── include/MusicGameEngine/   # Public API headers
│   └── src/
│       ├── core/                  # ECS, SceneGraph, Transform
│       ├── engine/                # Engine, AudioEngine, GameClock
│       ├── game/
│       │   ├── chart/             # ChartLoader, ChartTypes (UCF)
│       │   └── modes/             # BandoriRenderer, PhigrosRenderer, …
│       ├── gameplay/              # HitDetector, JudgmentSystem, ScoreTracker
│       ├── input/                 # InputManager, GestureRecognizer, TouchTypes
│       ├── renderer/              # Vulkan pipeline, QuadBatch, LineBatch, …
│       └── ui/                    # ImGui editor (ProjectHub, StartScreenEditor, …)
├── Projects/                      # Game projects (one folder each)
│   └── BandoriSandbox/
├── shaders/                       # GLSL source files
├── third_party/                   # GLFW, GLM, VMA, ImGui, stb, nlohmann
└── build/
    └── Debug/
        └── MusicGameEngineTest.exe    # Hub launcher
```

---

## Features

### Rendering System
- Vulkan-based graphics pipeline (VulkanMemoryAllocator)
- Quad batching (8 192 quads per batch)
- Line/curve rendering (4 096 lines)
- Mesh rendering with instancing
- Particle system (2 048 particles)
- Post-processing (bloom downsample/upsample compute, composite)

### Editor (ImGui)
- **Project Hub** — browse, create, and open game projects
- **Start Screen Editor** — design start screens with live Vulkan preview
- Resizable split panels, drag-and-drop asset management
- See [EDITOR_SYSTEM.md](EDITOR_SYSTEM.md) for full details

### Game Modes
- **Bandori** — 7-lane vertical scrolling highway
- **Phigros** — judgment line rotation system
- **Arcaea** — arc-based sky note gameplay
- **Cytus** — scan-line mechanics
- **Lanota** — concentric ring system

### Input System
- Keyboard input (keys 1–7 for lanes)
- Touch gesture recognition: Tap, Hold, Flick, Slide, Arc, Sky Note
- Hit detection with timing windows
- Score and combo tracking

### Audio
- Miniaudio-based playback
- DSP clock synchronization

### Chart Support
- **Unified Chart Format (UCF)** — single JSON format for all game modes
- Legacy format parsers: Bandori, Arcaea, Cytus, Phigros, Lanota
- Auto-detection of chart format

---

## Build

### Requirements
- CMake 3.20+
- C++20 compiler (MSVC 2022 recommended on Windows)
- Vulkan SDK (1.3+)
- GLFW, GLM, VulkanMemoryAllocator — pre-bundled in `third_party/`

### Steps
```bash
cmake -B build
cmake --build build --config Debug
```

Shaders are compiled automatically by `glslc` (found via `VULKAN_SDK`) and copied to `build/Debug/shaders/`.

---

## Usage

### Running the Editor

```bash
cd build/Debug
./MusicGameEngineTest.exe
```

The editor opens at the **Project Hub**. Use the mouse to interact with all panels. Press **ESC** to exit.

---

## Editor — Page 1: Project Hub

The hub is the first screen you see. It lists all projects found in the `Projects/` folder.

```
┌─────────────────────────────────────┐
│  Music Game Engine - Project Hub    │
│                                     │
│  [+ Create Game]                    │
│  ─────────────────────────────────  │
│  MyGame                      v1.0.0 │
│  BandoriSandbox              v1.0.0 │
└─────────────────────────────────────┘
```

### Opening a project

Click any project button to open it. The editor switches to the **Start Screen Editor** for that project.

### Creating a new project

1. Click **+ Create Game**
2. Type a project name in the dialog (spaces are converted to `_`)
3. Press **Create** or hit Enter

A new project folder is created under `Projects/<name>/` with the following structure:

```
Projects/<name>/
├── project.json           — window size, asset paths, engine version
├── start_screen.json      — start screen configuration
└── assets/
    ├── charts/
    │   └── demo.json      — empty chart stub
    ├── audio/             — music and sound effect files
    └── textures/          — image assets
```

The editor automatically opens the new project in the Start Screen Editor.

---

## Editor — Page 2: Start Screen Editor

The Start Screen Editor lets you design the opening screen of your game — background, logo, tap text, audio, and transition effect — with a **live preview**.

### Layout

```
┌──────────────────┬────────────────────────┐
│                  │  Properties            │
│    Preview       │  ▼ Background          │
│                  │  ▼ Logo                │
│   [live render]  │  ▼ Tap Text            │
│                  │  ▼ Transition Effect   │
│                  │  ▼ Audio               │
├──────────────────┴────────────────────────┤
│  Asset Panel  [thumbnail strip]           │
├───────────────────────────────────────────┤
│ < Back  Save  Load  Reset  Next: Music > │
└───────────────────────────────────────────┘
```

Drag the **splitter bars** (thin lines between panels) to resize any region.

---

### Preview Panel

Shows a real-time render of the start screen using the actual Vulkan renderer:

- **Background** — renders the set image/GIF at full panel size
- **Logo** — text rendered in Roboto-Medium at the configured size, color, and position; or an image file
- **Tap text** — rendered at the configured size and position
- All positions are **normalized** (0.0 = left/top, 1.0 = right/bottom)

---

### Properties Panel

#### Background
Set the background by **dragging an image or GIF thumbnail** from the Asset Panel onto the background drop zone. The drop zone highlights blue when you hover over it during a drag.

- Shows a mini-preview of the current background when set
- **Clear** — removes the background

Supported: `.png`, `.jpg`, `.gif` (animated), `.mp4`/`.webm` (shows filename placeholder)

#### Logo
Choose between **Text** and **Image** logo types.

**Text logo:**
- Type the logo text
- Adjust **Font Size** (12 – 96 px), **Color**, **Bold**, **Italic**
- Enable **Glow / Outline** for a glowing border effect

**Image logo:**
- Drag an image thumbnail from the Asset Panel onto the logo drop zone
- **Clear** removes it

Both types share:
- **Position** — two sliders for X and Y (normalized)
- **Scale** — 0.1 to **10.0** for very large logos

#### Tap Text
- Edit the text (e.g. "Tap to Start")
- **Position** — X and Y sliders (normalized)
- **Size** — 12 to **120 px**, applied to the live preview in real time

#### Transition Effect
Configures the animation when the player taps:
- **Fade to Black**, **Slide Left**, **Zoom In**, **Ripple** — built-in presets
- **Custom** — supply a Lua script path (runtime executor planned)
- **Duration** — 0.1 to 2.0 seconds

#### Audio
Configure the music and sound effects for the start screen.

**Background Music:**
1. Import an audio file via **Open File...** or drag it from File Explorer onto the window
2. The file appears as a "MUS" tile in the Asset Panel
3. Drag the tile onto the **Background Music** drop zone
4. Adjust **Volume** (0.0 – 1.0) and toggle **Loop**

**Tap Sound Effect:**
- Same process — drag an audio tile onto the **Tap Sound Effect** drop zone
- Adjust **Volume**

Supported audio formats: `.mp3`, `.ogg`, `.wav`, `.flac`, `.aac`

---

### Asset Panel

Displays all files in `{project}/assets/` as a scrollable thumbnail strip.

| Tile type | Appearance | Draggable to |
|---|---|---|
| Image (`.png` `.jpg`) | 80×80 pixel thumbnail | Background zone, Logo zone |
| GIF (`.gif`) | 80×80 thumbnail (first frame) | Background zone |
| Video (`.mp4` `.webm`) | Dark placeholder | Background zone |
| Audio (`.mp3` `.ogg` etc.) | Blue tile labeled "MUS" | BGM zone, SFX zone |

**Interactions:**
- **Hover** — highlights the tile with a blue border; tooltip shows full relative path
- **Drag** — drag any tile to a Properties drop zone to assign it
- **Right-click → Delete** — permanently deletes the file from disk, clears any references in the Properties panel, and refreshes the strip

**Importing files:**

| Method | How |
|---|---|
| Open File... | Click the button; native Windows dialog with filters for Images, Audio, Videos, All Files. Multi-select supported. |
| Drag from File Explorer | Drag files from Windows File Explorer directly onto the engine window. Files are imported automatically. |

Imported files are routed to the correct subfolder:

| File type | Destination |
|---|---|
| Images & GIFs | `assets/textures/` |
| Audio | `assets/audio/` |
| Videos | `assets/videos/` |

---

### Nav Bar (always visible)

| Button | Action |
|---|---|
| `< Back` | Return to Project Hub |
| `Save` | Write all settings to `start_screen.json` |
| `Load` | Reload settings from `start_screen.json` |
| `Reset` | Restore all fields to defaults (does not save) |
| `Next: Music Selection >` | Advance to Music Selection (not yet implemented) |

A status message (e.g. "Saved!", "Imported 2 file(s)") appears briefly after each action.

---

## Gameplay Controls

| Key | Action |
|---|---|
| 1 – 7 | Hit notes in the corresponding lane |
| ESC | Exit |

---

## Timing Windows

| Note type | Perfect | Good | Bad |
|---|---|---|---|
| Tap / Flick | ±20 ms | ±60 ms | ±100 ms |
| Hold start | ±20 ms | — | — |
| Hold end | — | ±50 ms | — |
| Sky Note | ±30 ms | ±80 ms | ±120 ms |
| Slide / Arc | position accuracy + completion ratio | | |

---

## Internal Architecture

```
Engine
├── Renderer (Vulkan)
│   ├── QuadBatch, LineBatch, MeshRenderer, ParticleSystem
│   └── PostProcess (bloom compute, composite)
├── AudioEngine (miniaudio)
├── InputManager (GLFW)
├── HitDetector → JudgmentSystem → ScoreTracker
├── SceneGraph (ECS hybrid)
└── GameModeRenderer (active mode plugin)
```

---

## Documentation

| File | Contents |
|---|---|
| [EDITOR_SYSTEM.md](EDITOR_SYSTEM.md) | Full editor architecture, all UI fields, JSON schemas, implementation status |
| [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md) | Vulkan rendering pipeline details |
| [INPUT_SYSTEM.md](INPUT_SYSTEM.md) | Input system design and gesture recognition |
| [CHART_PARSER.md](CHART_PARSER.md) | Chart loading system and format parsers |
| [UNIFIED_CHART_FORMAT.md](UNIFIED_CHART_FORMAT.md) | UCF JSON specification |
