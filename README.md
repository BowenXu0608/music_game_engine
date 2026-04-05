# Music Game Engine

A C++20/Vulkan-based rhythm game engine with a Unity Hub-style editor for mobile rhythm game development.  
Supports **BanG Dream**, **Phigros**, **Arcaea**, **Cytus**, and **Lanota** as plugin game modes.

**Last updated:** 2026-04-05

---

## Project Structure

```
Music_game/
├── engine/
│   ├── include/MusicGameEngine/       # Public API headers
│   └── src/
│       ├── core/                      # System 3: ECS, SceneGraph, Transform
│       ├── engine/                    # System 3: Engine, AudioEngine, GameClock
│       ├── game/
│       │   ├── chart/                 # System 2: ChartLoader, ChartTypes (UCF)
│       │   └── modes/                 # System 6: BandoriRenderer, PhigrosRenderer, …
│       ├── gameplay/                  # System 5: HitDetector, JudgmentSystem, ScoreTracker
│       ├── input/                     # System 4: InputManager, GestureRecognizer, TouchTypes
│       ├── renderer/                  # System 1: Vulkan pipeline, QuadBatch, LineBatch, …
│       └── ui/                        # System 7: ImGui editor (ProjectHub → SongEditor)
├── Projects/                          # Game projects (one folder each)
│   └── BandoriSandbox/
├── shaders/                           # GLSL source → compiled to build/shaders/*.spv
├── third_party/                       # GLFW, GLM, VMA, ImGui, stb, nlohmann
└── build/
    └── Debug/
        └── MusicGameEngineTest.exe    # Hub launcher; supports --test <project_path> for standalone test game mode
```

---

## 7 Systems Overview

| # | System | Status | Doc |
|---|---|---|---|
| 1 | [Rendering](#system-1--rendering) | ✅ Complete | [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md) |
| 2 | [Resource Management](#system-2--resource-management) | ✅ Complete | [RESOURCE_MANAGEMENT.md](RESOURCE_MANAGEMENT.md) |
| 3 | [Core Engine](#system-3--core-engine) | ✅ Complete | [CORE_ENGINE.md](CORE_ENGINE.md) |
| 4 | [Input & Gesture](#system-4--input--gesture) | ✅ Complete | [INPUT_SYSTEM.md](INPUT_SYSTEM.md) |
| 5 | [Gameplay](#system-5--gameplay) | ✅ Complete | [INPUT_SYSTEM.md](INPUT_SYSTEM.md) |
| 6 | [Game Mode Plugins](#system-6--game-mode-plugins) | ✅ Complete | [GAME_MODES.md](GAME_MODES.md) |
| 7 | [Editor UI](#system-7--editor-ui) | ✅ Complete | [EDITOR_SYSTEM.md](EDITOR_SYSTEM.md) |

---

## System 1 — Rendering

Vulkan-based graphics pipeline. Two sub-layers: Vulkan backend + Batcher layer.

**Vulkan Backend** (`engine/src/renderer/vulkan/`):
- `VulkanContext` — instance, physical/logical device, queues, surface
- `Swapchain` — swap chain, image views, framebuffers
- `RenderPass` — render pass management
- `Pipeline` — graphics pipeline builder (`PipelineConfig`)
- `BufferManager` — VMA-backed buffers (`VMA_IMPLEMENTATION` here)
- `TextureManager` — stb_image → VMA image alloc (`STB_IMAGE_IMPLEMENTATION` here)
- `DescriptorManager` — pool + set0 UBO + set1 sampler layouts
- `CommandManager` — per-frame command buffer alloc/begin/end
- `SyncObjects` — `MAX_FRAMES_IN_FLIGHT = 3`, semaphores, fences

**Batcher Layer** (`engine/src/renderer/`):
- `QuadBatch` — textured quads (max 8192/frame)
- `LineBatch` — line segments CPU-expanded to quads (max 4096/frame)
- `MeshRenderer` — per-mesh 3D draw with depth test
- `ParticleSystem` — ring buffer 2048 particles, additive blend
- `PostProcess` — bloom compute mip chain (5-level) + composite pass
- `Camera.h` — unified ortho + perspective, header-only
- `Renderer.h/.cpp` — top-level owner, exposes `whiteView()`, `whiteSampler()`, `descriptors()`

**Shaders** (`shaders/`): quad, line, mesh, bloom_downsample/upsample (compute), composite

> Full details: [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md)

---

## System 2 — Resource Management

All external file I/O: textures, audio, charts, asset browsing, GIFs.

- **TextureManager** — stb_image → GPU via VMA; ImGui texture handle management
- **AudioEngine** — miniaudio wrapper (`engine/src/engine/AudioEngine.h/.cpp`); play/stop/seek/position
- **ChartLoader + ChartTypes** — unified `ChartData` / `NoteEvent` schema; auto-detects format
- **AssetBrowser** (`engine/src/ui/AssetBrowser.h`) — thumbnail grid, `"ASSET_PATH"` drag-drop, import, delete
- **GifPlayer** (`engine/src/ui/GifPlayer.h/.cpp`) — animated GIF via per-frame Vulkan textures

**Chart formats supported:**

| Format | Extension | Game |
|---|---|---|
| Unified Chart Format (UCF) | `.json` with `"version"` | All modes |
| Bandori (legacy) | `.json` (no version) | BanG Dream |
| Arcaea (legacy) | `.aff` | Arcaea |
| Cytus (legacy) | `.xml` | Cytus |
| Phigros (legacy) | `.pec` / `.pgr` | Phigros |
| Lanota (legacy) | `.lan` | Lanota |

**Project folder layout:**
```
Projects/<ProjectName>/
├── project.json
├── start_screen.json
├── music_selection.json
└── assets/
    ├── charts/     — .json chart files
    ├── audio/      — .mp3 / .ogg / .wav
    └── textures/   — .png / .jpg / .gif
```

> Full details: [RESOURCE_MANAGEMENT.md](RESOURCE_MANAGEMENT.md)

---

## System 3 — Core Engine

Foundational runtime: data model, main loop, timing.

- **ECS** (`engine/src/core/ECS.h`) — `EntityID`, `ComponentPool<T>`, `Registry`; dense storage, sparse map
- **SceneNode / SceneGraph** (`engine/src/core/SceneNode.h`) — parent-child transform hierarchy; used by PhigrosRenderer
- **Transform** (`engine/src/core/Transform.h`) — TRS + quaternion; `toMatrix()`
- **Engine** (`engine/src/engine/Engine.h/.cpp`) — main loop, owns all subsystems as members, owns GLFW callbacks and user pointer
- **GameClock** (`engine/src/engine/GameClock.h`) — wall clock + DSP time override for chart sync; header-only

> Architecture details in [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md) (Core Architecture section)

---

## System 4 — Input & Gesture

Multi-touch input pipeline. Supports desktop (GLFW mouse simulation) and mobile (Android JNI / iOS UITouch).

- **TouchTypes.h** — `TouchPoint`, `GestureEvent`, `TouchThresholds` (all `constexpr`)
- **GestureRecognizer** — per-finger state machine: PotentialTap → Holding / Sliding → emits Tap / Flick / HoldBegin / HoldEnd / SlideBegin / SlideMove / SlideEnd
- **InputManager.h** — keyboard + touch aggregator; `injectTouch(id, phase, pos, t)` is the single entry point for all platforms

**Gesture thresholds:**

| Constant | Value | Meaning |
|---|---|---|
| `TAP_SLOP_PX` | 20 px | Max drift for a tap |
| `TAP_MAX_DURATION_S` | 0.15 s | Hold fires after this |
| `FLICK_MIN_VELOCITY` | 400 px/s | Min speed for flick |
| `SLIDE_SLOP_PX` | 25 px | Min movement for slide |
| `VELOCITY_WINDOW_S` | 0.08 s | Velocity averaging window |

> Full details: [INPUT_SYSTEM.md](INPUT_SYSTEM.md)

---

## System 5 — Gameplay

Hit detection, judgment grading, and score tracking.

- **HitDetector** — three hit modes: lane-based (Bandori/Cytus/Lanota), position-based (Arcaea), line-projection (Phigros)
- **JudgmentSystem** — timing → Perfect/Good/Bad/Miss for all note types (tap, flick, hold, slide, arc, skyNote)
- **JudgmentDisplay** — visual judgment feedback (not yet wired to HUD)
- **ScoreTracker** — score (Perfect=1000, Good=500, Bad=100, Miss=0) + combo + maxCombo
- **Hit Effects** — particle bursts on judgment: Perfect = green, Good = blue, Bad = red, Miss = no effect. Miss detection dispatches judgments properly when notes pass their timing window.

**Timing windows:**

| Judgment | Window |
|---|---|
| Perfect | ±20 ms |
| Good | ±60 ms |
| Bad | ±100 ms |
| Miss | >100 ms or note passed |

> Full details: [INPUT_SYSTEM.md](INPUT_SYSTEM.md)

---

## System 6 — Game Mode Plugins

Plugin architecture: `GameModeRenderer` abstract interface + 5 implementations.

| Plugin | Game | Notes |
|---|---|---|
| `BandoriRenderer` | BanG Dream | 5-lane highway, perspective, eye_z ≥8 |
| `PhigrosRenderer` | Phigros | Rotating judgment lines, uses SceneGraph |
| `ArcaeaRenderer` | Arcaea | 3D perspective, arc ribbons via MeshRenderer |
| `CytusRenderer` | Cytus | Horizontal scanning line |
| `LanotaRenderer` | Lanota | Concentric ring tunnel perspective |

`Engine` holds active mode as `std::unique_ptr<GameModeRenderer>`.  
Game modes render via `Renderer&` — never allocate Vulkan resources directly.

The `GameModeRenderer` base class provides a `showJudgment()` virtual method for per-mode judgment display, and `onInit` accepts an optional `GameModeConfig` for runtime configuration.

> Full details: [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md) (Game Mode Integration section)

---

## System 7 — Editor UI

Unity Hub-style editor built on ImGui + Vulkan. Layer-based flow:

```
ProjectHub → StartScreenEditor → MusicSelectionEditor → SongEditor → (GamePlay)
```

| Layer | Purpose |
|---|---|
| **Project Hub** | Browse + create projects, folder scaffolding |
| **Start Screen Editor** | Background, logo, tap text, transition, audio; live preview |
| **Music Selection Editor** | Arcaea-style card stack wheels, hierarchy panel, cover picker |
| **Song Editor** | DAW-style layout (scene preview + chart timeline simultaneous), left sidebar config, Madmom beat analysis (auto-generate markers for 3 difficulties), toolbar (Analyze/Place All/Clear), audio playback controls, difficulty selector (Easy/Medium/Hard) with per-difficulty notes, camera controls, HUD config (score/combo position and style), chart persistence (save/load as unified JSON) |
| **Scene Viewer** | Gameplay viewport, Play/Stop, stats |
| **Test Game** | Green button on all editor pages; launches full game flow preview |
| **Asset Browser** | Unified import system (`importAssetsToProject`), shared across all pages, "All Files" default |

> Full details: [EDITOR_SYSTEM.md](EDITOR_SYSTEM.md)

---

## Build

### Requirements
- CMake 3.20+
- C++20 compiler (MSVC 2022 recommended on Windows)
- Vulkan SDK 1.3+
- GLFW, GLM, VulkanMemoryAllocator — pre-bundled in `third_party/`

### Steps
```bash
cmake -B build
cmake --build build --config Debug
```

Shaders are compiled automatically by `glslc` (found via `VULKAN_SDK`) and copied to `build/Debug/shaders/`.

### Running
```bash
cd build/Debug
./MusicGameEngineTest.exe
```

The editor opens at the **Project Hub**. Use the mouse to interact with all panels. Press **ESC** to exit.

---

## Gameplay Controls

| Key | Action |
|---|---|
| 1 – 7 | Hit notes in the corresponding lane |
| ESC | Exit |

---

## Test Game

The **Test Game** button (available in the Song Editor) launches a separate `MusicGameEngineTest.exe --test <project_path>` process in its own window. This mirrors the workflow in RPG Maker or Unity's Play button — the editor window remains fully interactive and unaffected while the test game runs independently.

The test game window runs the full game flow:

1. **Start Screen** — tap to continue
2. **Music Selection** — pick a song
3. **Gameplay** — play the chart with hit detection, judgment, and scoring

Press **ESC** at any time to close the test game window and return to editing.

---

## Documentation

One document per system:

| System | Document | Contents |
|---|---|---|
| System 1 | [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md) | Vulkan backend, batchers, shaders, frame loop, performance, lessons learned |
| System 2 | [RESOURCE_MANAGEMENT.md](RESOURCE_MANAGEMENT.md) | TextureManager, AudioEngine, ChartLoader, UCF format, AssetBrowser, GifPlayer |
| System 3 | [CORE_ENGINE.md](CORE_ENGINE.md) | ECS, SceneGraph, Transform, Engine main loop, GameClock, build system |
| System 4+5 | [INPUT_SYSTEM.md](INPUT_SYSTEM.md) | Gesture recognition, hit detection, judgment, score, platform integration |
| System 6 | [GAME_MODES.md](GAME_MODES.md) | GameModeRenderer interface + BandoriRenderer, PhigrosRenderer, ArcaeaRenderer, CytusRenderer, LanotaRenderer |
| System 7 | [EDITOR_SYSTEM.md](EDITOR_SYSTEM.md) | ProjectHub, StartScreen, MusicSelection, SongEditor, GameFlowPreview, AssetBrowser |

---

## Recent Additions (2026-04-04 ~ 2026-04-05)

- **ChartLoader completion** — all 6 format parsers fully implemented with complete note type coverage (drag, arctap, ring, slide, flick direction, arc easing)
- **Beat Analysis via Madmom** — `tools/analyze_audio.py` + `AudioAnalyzer` C++ class; auto-generates markers for Easy (downbeats), Medium (all beats), Hard (beats + onsets)
- **DAW-style SongEditor layout** — scene preview and chart timeline visible simultaneously; left sidebar for config
- **Gameplay lead-in** — 2-second visual lead-in before audio starts; configurable audio offset per song
- **HUD foreground fix** — score/combo now uses `ImGui::GetForegroundDrawList()` for guaranteed visibility
- **Bug fixes** — waveform LOD dangling reference crash; MusicSelectionEditor Play button bounds check

---

## Future Plans

### Priority 1: Mobile Platform
Android JNI multi-touch (bridge designed), iOS UITouch bridge (bridge designed), Vulkan Android surface.

### Priority 2: Configurable Timing & Replay
Per-song judgment windows (Perfect/Good/Bad/Miss) — ✅ done (editor UI sliders).  
Remaining: wire to gameplay HitDetector, replay system (record + deterministic replay), auto-play mode.

### Priority 3: Advanced Chart Editor
Remaining: BPM grid snapping, copy/paste note patterns, undo/redo, note drag-to-reposition.
