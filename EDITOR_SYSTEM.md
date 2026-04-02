# Editor System

The engine ships with a Unity Hub-style editor built on ImGui + Vulkan. It lets you create and manage game projects, configure each project's start screen, and preview gameplay — all without leaving the application.

---

## Architecture

```
MusicGameEngineTest.exe
        ↓
    Engine::runHub()
        ↓
┌─────────────────────────────────────────────┐
│                EditorLayer                   │
│  ProjectHub → StartScreen → MusicSelection  │
│                                  ↓          │
│                             GamePlay         │
└─────────────────────────────────────────────┘
```

Layer switching is handled by `Engine::switchLayer(EditorLayer)`. Each layer is a self-contained ImGui panel rendered inside the main window.

**Files:**
```
engine/src/ui/
├── ImGuiLayer.h/.cpp         — ImGui init, Vulkan backend, texture management
├── SceneViewer.h/.cpp        — Gameplay viewport (Play/Stop, stats)
├── ProjectHub.h/.cpp         — Project list + Create Game dialog
├── StartScreenEditor.h/.cpp  — Start screen config editor
├── AssetBrowser.h            — Asset scanning (images, GIFs, videos, audio)
└── GifPlayer.h/.cpp          — Animated GIF playback via Vulkan textures
```

---

## Working Directory Fix

At startup, `main()` calls `GetModuleFileNameA` and sets the working directory to the exe's own directory (`build/Debug/`). This ensures all relative paths — most importantly `../../Projects` — resolve correctly regardless of how the exe is launched (double-click, IDE, command line from a different directory).

`ProjectHub::scanProjects()` stores discovered project paths as **absolute paths** (`fs::absolute(entry.path())`) so that drag-and-drop import, texture loading, and asset scanning always resolve to the correct location on disk.

---

## Layer 1 — Project Hub

The first screen. Lists all projects found under `Projects/` and provides a button to create new ones.

### Project scanning

On first render, `ProjectHub::scanProjects()` walks `../../Projects/` (resolved to an absolute path), reads every `project.json` it finds, and populates the project list. The list is rescanned whenever `m_scanned` is reset to `false` (e.g. after creating a new project).

### Create Game

Clicking `+ Create Game` opens a centered modal dialog:

- **Project Name** — free text input; sanitized to `[A-Za-z0-9_-]` for the folder name (spaces → `_`)
- **Folder preview** — shows `Projects/<safe_name>` live as you type
- **Create** button — disabled while name is empty; also accepts Enter key
- **Cancel** — closes dialog, resets state
- Error message shown in red if the folder already exists or creation fails

On success, the following folder structure is created and the editor immediately switches to the Start Screen layer for the new project:

```
Projects/<ProjectName>/
├── project.json          — name, version, window size, asset paths
├── start_screen.json     — all start screen settings
└── assets/
    ├── charts/
    │   └── demo.json     — empty UCF stub chart
    ├── audio/
    └── textures/
```

### project.json format

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

Opened when a project is selected from the hub. Resizable two-row layout:

- **Top row:** Preview (left) | Properties (right) — horizontal split ratio draggable left/right
- **Bottom strip:** Assets panel — vertical split ratio draggable up/down
- **Nav bar:** Back / Save / Load / Reset / Next buttons always visible at the bottom

Drag the thin splitter bars between panels to resize. Default split: 60/40 horizontal, 72/28 vertical.

---

### Preview Panel

Renders live using the ImGui draw list (`ImDrawList`). Requires Vulkan context (set via `initVulkan` at engine startup).

- **Background** — static image (`dl->AddImage`), animated GIF (per-frame Vulkan textures advanced each frame), or video placeholder text. Falls back to a dark fill when no asset is set.
- **Logo** — image file centered at logo position, or smooth TTF text (Roboto-Medium) rendered at the exact font size via `dl->AddText(font, fontSize, ...)`. Bold is faked by drawing text twice with a 1px horizontal offset. Glow is rendered by drawing the text 8 times offset in cardinal/diagonal directions before the main draw pass.
- **Tap text** — centered at tap text position. Size is applied via `getLogoFont(m_tapTextSize)` + `dl->AddText(tapFont, tapFontSize, ...)` so the Size slider directly controls what is previewed.

---

### Properties Panel

Organized into collapsible sections (all open by default).

#### Background

The background is set by **drag-and-drop** — drag a thumbnail from the asset panel below onto the background drop zone. No dropdown/combo is used.

| Element | Description |
|---|---|
| Drop zone | Shows a live preview of the set background image, or "Drop background here" when empty. Highlighted in blue on hover during a drag. |
| Clear | Removes the current background and unloads its Vulkan texture. |

When a file is dropped, `loadBackground()` is called immediately and the preview updates.

#### Logo

| Field | Type | Description |
|---|---|---|
| Logo Type | combo | Text / Image |
| Text | text input | Logo text (Text mode) |
| Font Size | float slider | 12 – 96 px (rendered in Roboto-Medium) |
| Color | color picker | RGBA |
| Bold / Italic | checkboxes | Style flags |
| Drop zone | drag target | Image mode: drag an image thumbnail here to set the logo image. Shows a preview of the set image or "Drop logo image here" when empty. |
| Clear | button | Removes the current logo image. |
| Position | float2 slider | Normalized [0, 1] — (0.5, 0.3) is center-top |
| Scale | float slider | **0.1 – 10.0** |
| Glow / Outline | checkbox | Enable glow effect |
| Glow Color | color picker | RGBA |
| Glow Radius | float slider | 1 – 32 px |

#### Tap Text

| Field | Type | Description |
|---|---|---|
| Text | text input | e.g. "Tap to Start" |
| Position | float2 slider | Normalized [0, 1] — (0.5, 0.8) is center-bottom |
| Size | int slider | **12 – 120 px** — applied to the live preview immediately |

#### Transition Effect

| Field | Type | Description |
|---|---|---|
| Effect | combo | Fade to Black / Slide Left / Zoom In / Ripple / Custom |
| Duration | float slider | 0.1 – 2.0 s |
| Script Path | text input | Lua script path (Custom only) |

Custom scripts receive `progress` (0→1), `tap_x`, `tap_y`. Runtime executor is a stub — field is saved and the UI is wired.

#### Audio

Background music and tap sound effect are configured here. Both use drag-and-drop zones — drag an audio thumbnail from the asset panel below.

**Background Music**

| Element | Description |
|---|---|
| Drop zone | Shows `[BGM]  <filename>` when set, or "Drop audio file here" when empty. |
| Clear | Removes the music path. |
| Volume | Float slider 0.0 – 1.0 |
| Loop | Checkbox — whether to loop the track |

**Tap Sound Effect**

| Element | Description |
|---|---|
| Drop zone | Same drop zone style, shows the set filename. |
| Clear | Removes the SFX path. |
| Volume | Float slider 0.0 – 1.0 |

---

### Assets Panel

Shows all importable files in `{projectPath}/assets/` as a horizontal strip of **image thumbnails** and **audio tiles**.

#### Toolbar

| Button | Description |
|---|---|
| Open File... | Native Windows file picker (`GetOpenFileNameW`), supports multi-select. Filters: Images & GIFs, Audio, Videos, All Files. Selected files are copied into the correct subfolder and the panel refreshes automatically. |

The panel shows a drop-zone hint ("Drop image / GIF / video files here, or click Open File...") when no assets exist yet.

#### Thumbnails

Each image/GIF/video file is displayed as an **80×80 pixel thumbnail** with a centered filename label below (truncated to 11 chars). Audio files appear as blue **80×80 tiles** labeled "MUS".

Thumbnails are loaded lazily on first display via `getThumb(relPath)` using `TextureManager::loadFromFile`. Results are cached in `m_thumbCache` (a `std::unordered_map<std::string, ThumbEntry>`). The cache is cleared when:
- A new project is loaded (`load()` is called with `m_ctx` available)
- The engine shuts down (`shutdownVulkan`)

**Interactions:**

| Interaction | Effect |
|---|---|
| Hover | Blue highlight border |
| Hover | Tooltip showing full relative path |
| Left-drag | Starts a drag-drop operation with payload type `ASSET_PATH` (the relative path string). Drop onto Background or Logo drop zones in Properties to assign. Drop onto Audio zones to set music/SFX. |
| Right-click | Context menu with **Delete** option |

**Delete** via right-click:
1. Removes the file from disk (`fs::remove`)
2. Clears the background reference if that file was the current background (and unloads the Vulkan texture)
3. Clears the logo image reference if that file was the current logo
4. Clears the background music or tap SFX reference if applicable
5. Evicts the entry from the thumbnail cache and destroys the Vulkan texture
6. Triggers an asset rescan on the next frame

#### Import Routing

Files dropped onto the GLFW window, or selected via Open File, are copied by `importFiles()`:

| Extension | Destination |
|---|---|
| `.png` `.jpg` `.jpeg` `.gif` | `assets/textures/` |
| `.mp3` `.MP3` `.ogg` `.wav` `.flac` `.aac` | `assets/audio/` |
| `.mp4` `.webm` | `assets/videos/` |
| other | `assets/` |

Extensions are lowercased before matching, so `.MP3` and `.mp3` both route to `assets/audio/`.

#### Asset Scanning

`scanAssets(projectPath)` (in `AssetBrowser.h`) uses `fs::absolute()` before calling `fs::relative()` to avoid path resolution issues with relative base paths on Windows. It recursively walks `{projectPath}/assets/` and groups files:

| Group | Extensions |
|---|---|
| images | `.png` `.jpg` `.jpeg` |
| gifs | `.gif` |
| videos | `.mp4` `.webm` |
| audios | `.mp3` `.ogg` `.wav` `.flac` `.aac` |

---

### start_screen.json Format

```json
{
  "background": {
    "file": "assets/textures/bg.png",
    "type": "image"
  },
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

`background.type` values: `none`, `image`, `gif`, `video`.

**Backward compatibility:** `load()` accepts both the old flat format (`"logo": "text"`, `"background": ""`) and the current nested format. Old projects load and display correctly; saving converts them to the current nested format.

---

### Nav Bar

Always visible at the bottom of the editor window:

| Button | Description |
|---|---|
| `< Back` | Returns to Project Hub |
| `Save` | Writes `{projectPath}/start_screen.json`. Green "Saved!" status for 2 s. |
| `Load` | Re-reads `start_screen.json` and reloads all textures. Green "Loaded!" status for 2 s. |
| `Reset` | Restores all fields to default values (does not save). |
| `Next: Music Selection >` | Placeholder — Music Selection layer not yet implemented. |

Status messages (green for success, yellow for import results) appear between Reset and Next and auto-dismiss after 2–5 seconds.

---

## Layer 3 — Music Selection (planned)

Not yet implemented. Will allow browsing and selecting chart files from `assets/charts/`.

---

## Layer 4 — Gameplay / Scene Viewer

The `SceneViewer` renders the active game scene as an ImGui texture viewport.

- **Play/Stop** button controls game execution
- When stopped: game update and render are paused, scene is frozen
- **Stats window**: FPS, frame time, song time, play status

### Render Flow

```
1. beginFrame()           — acquire swapchain image, begin scene render pass
2. Game renders to offscreen scene (only if playing)
3. End scene render pass
4. Bloom compute passes on scene texture
5. Begin swapchain render pass (clear to dark gray)
6. ImGui renders UI with scene texture in viewport
7. End swapchain render pass
8. Submit & present
```

---

## ImGui Integration

`ImGuiLayer` owns the ImGui context and Vulkan descriptor pool (32 texture slots).

- `init(window, ctx, renderPass)` — sets up GLFW + Vulkan backends, loads Roboto-Medium.ttf at 24/32/48/64 px into the font atlas, uploads fonts
- `addTexture(view, sampler)` — registers a Vulkan texture for use in `ImGui::Image()`
- `getLogoFont(targetSize)` — returns the Roboto font closest to `targetSize`; falls back to default ImGui font if Roboto failed to load
- `beginFrame()` / `endFrame()` / `render(cmd)` — per-frame lifecycle

**Font file:** `third_party/imgui/misc/fonts/Roboto-Medium.ttf` (bundled with ImGui). Loaded relative to the exe working directory as `../../third_party/imgui/misc/fonts/Roboto-Medium.ttf`.

The scene texture is registered at startup and passed to `SceneViewer::setSceneTexture()`.

---

## Running the Editor

```bash
cd build/Debug
./MusicGameEngineTest.exe
```

- ESC — exit
- Mouse — interact with all panels
- Drag files from File Explorer onto the window — imports into the current project

---

## Known Issues

**Window maximize — black borders**
Clicking the Windows maximize button causes black borders around the UI. Root cause: Vulkan swapchain does not recreate immediately on maximize. Workaround: drag window edges to resize manually instead of using the maximize button.

**Vulkan validation warnings on texture reload**
When switching backgrounds rapidly or loading a new project, validation layers may warn about a sampler being destroyed while still referenced by a descriptor set. This is a known issue with the immediate-destroy pattern used in `unloadBackground` / `unloadLogoImage` / `clearThumbnails`. No crash occurs in practice; fix requires deferred destruction or a `vkDeviceWaitIdle` before unload.

---

## Implementation Status

| Feature | Status |
|---|---|
| ImGui + Vulkan integration | done |
| ImGui descriptor pool (32 slots) | done |
| Roboto-Medium TTF at 24/32/48/64 px | done |
| Working directory anchored to exe location | done |
| Project Hub — list projects (absolute paths) | done |
| Project Hub — Create Game dialog | done |
| Project scaffolding (folder + JSON files) | done |
| Start Screen Editor — resizable panels (splitter bars) | done |
| Start Screen Editor — preview (background, logo, tap text) | done |
| Start Screen Editor — logo text smooth (Roboto TTF, color, size, glow) | done |
| Start Screen Editor — logo text bold (fake double-draw) | done |
| Start Screen Editor — logo image via drag-and-drop zone | done |
| Start Screen Editor — background via drag-and-drop zone | done |
| Start Screen Editor — background PNG/JPG/GIF/video | done |
| Start Screen Editor — tap text size applied to preview | done |
| Start Screen Editor — logo scale range 0.1 – 10.0 | done |
| Start Screen Editor — tap text size range 12 – 120 px | done |
| Start Screen Editor — audio section (bgMusic, tapSfx, volume, loop) | done |
| Start Screen Editor — audio save/load (start_screen.json) | done |
| Start Screen Editor — asset panel with image thumbnails (80×80, lazy load) | done |
| Start Screen Editor — asset panel with audio tiles ("MUS") | done |
| Start Screen Editor — thumbnail drag-and-drop to background/logo/audio zones | done |
| Start Screen Editor — right-click context menu to delete assets | done |
| Start Screen Editor — asset import via drag-drop (GLFW drop callback) | done |
| Start Screen Editor — asset import via Open File button (multi-select) | done |
| Start Screen Editor — audio file import routing to assets/audio/ | done |
| Start Screen Editor — Open File dialog includes Audio filter | done |
| Start Screen Editor — transition effect selector (4 presets + custom) | done |
| Start Screen Editor — custom Lua transition slot | done (stub) |
| Start Screen Editor — save/load JSON (nlohmann, backward compat) | done |
| Start Screen Editor — Save/Load/Reset in always-visible nav bar | done |
| Scene Viewer — gameplay viewport | done |
| Scene Viewer — Play/Stop + stats | done |
| Music Selection layer | not started |
| Inspector / hierarchy panels | not started |
| Chart editor / timeline | not started |
| Audio waveform display | not started |
| Deferred Vulkan texture destruction | not started |
