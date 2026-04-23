# Music Game Engine

A C++20/Vulkan-based rhythm game engine with a Unity Hub-style editor for mobile rhythm game development.  
Supports **BanG Dream**, **Arcaea**, **Cytus**, and **Lanota** as plugin game modes (Phigros renderer exists but is not currently reachable from the UI).

**Last updated:** 2026-04-23

---

## Project Structure

```
Music_game/
├── engine/
│   ├── include/MusicGameEngine/       # Public API headers
│   └── src/
│       ├── core/                      # System 3: ECS, SceneGraph, Transform
│       ├── engine/                    # System 3: Engine, AudioEngine, GameClock, AudioAnalyzer
│       ├── game/
│       │   ├── chart/                 # System 2: ChartLoader, ChartTypes (UCF)
│       │   └── modes/                 # System 6: BandoriRenderer, ArcaeaRenderer,
│       │                              #            LanotaRenderer, CytusRenderer, PhigrosRenderer
│       ├── gameplay/                  # System 5: HitDetector, JudgmentSystem, ScoreTracker
│       ├── input/                     # System 4: InputManager, GestureRecognizer, TouchTypes
│       ├── renderer/                  # System 1: Vulkan pipeline, QuadBatch, LineBatch, MeshRenderer
│       └── ui/                        # System 7: ImGui editor (ProjectHub → SongEditor)
├── android/                           # System 8: APK packaging (AGP + NDK + Gradle)
├── docs/                              # Per-system documentation (sys1..sys8)
├── Projects/                          # Game projects (one folder each)
│   └── test/
├── shaders/                           # GLSL source → compiled to build/shaders/*.spv
├── third_party/                       # GLFW, GLM, VMA, ImGui, stb, miniaudio
└── build/
    └── Debug/
        └── MusicGameEngineTest.exe    # Hub launcher; --test <project_path> for standalone test game
```

---

## 8 Systems Overview

| # | System | Status | Doc |
|---|---|---|---|
| 1 | [Rendering](#system-1--rendering) | ✅ Complete | [docs/sys1_rendering.md](docs/sys1_rendering.md) |
| 2 | [Resource Management](#system-2--resource-management) | ✅ Complete | [docs/sys2_resources.md](docs/sys2_resources.md) |
| 3 | [Core Engine](#system-3--core-engine) | ✅ Complete | [docs/sys3_core_engine.md](docs/sys3_core_engine.md) |
| 4 | [Input & Gesture](#system-4--input--gesture) | ✅ Complete | [docs/sys4_input.md](docs/sys4_input.md) |
| 5 | [Gameplay](#system-5--gameplay) | ✅ Complete | [docs/sys5_gameplay.md](docs/sys5_gameplay.md) |
| 6 | [Game Mode Plugins](#system-6--game-mode-plugins) | ✅ Complete | [docs/sys6_game_modes.md](docs/sys6_game_modes.md) |
| 7 | [Editor UI](#system-7--editor-ui) | ✅ Complete | [docs/sys7_editor.md](docs/sys7_editor.md) |
| 8 | [Android Packaging](#system-8--android-packaging) | 🟡 In Progress | [docs/sys8_android.md](docs/sys8_android.md) |

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
- `QuadBatch` — textured quads, pipeline-per-`MaterialKind` (max 8192/frame)
- `LineBatch` — line segments CPU-expanded to quads (max 4096/frame)
- `MeshRenderer` — per-mesh 3D draw with depth test, pipeline-per-`MaterialKind`
- `ParticleSystem` — ring buffer 2048 particles, additive blend
- `PostProcess` — bloom compute mip chain (5-level) + composite pass
- `Camera.h` — unified ortho + perspective, header-only
- `Material.h` / `MaterialSlots.h` — material kinds (Unlit / Glow / Scroll / Pulse / Gradient / Custom), per-mode slot tables
- `MaterialAsset.h` / `MaterialAssetLibrary.h` — project-level `.mat` asset system (Phase 4)
- `ShaderCompiler.h` — runtime `.frag` → `.spv` via `glslc`, mtime-cached (Phase 4 Custom kind)
- `Renderer.h/.cpp` — top-level owner, exposes `whiteView()`, `whiteSampler()`, `descriptors()`

**Shaders** (`shaders/`): quad_{unlit,glow,scroll,pulse,gradient}, mesh_{unlit,glow,scroll,pulse,gradient}, line, bloom_downsample/upsample (compute), composite

> Full details: [docs/sys1_rendering.md](docs/sys1_rendering.md)

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

**Note types:** Tap, Hold (with multi-waypoint cross-lane paths + transition styles Straight / Angle90 / Curve / Rhomboid), Flick, Drag, Slide (with custom paths for ScanLine mode), Arc (Arcaea-style 3D curves with X/Y easing), ArcTap, Ring.

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

> Full details: [docs/sys2_resources.md](docs/sys2_resources.md)

---

## System 3 — Core Engine

Foundational runtime: data model, main loop, timing.

- **ECS** (`engine/src/core/ECS.h`) — `EntityID`, `ComponentPool<T>`, `Registry`; dense storage, sparse map
- **SceneNode / SceneGraph** (`engine/src/core/SceneNode.h`) — parent-child transform hierarchy; used by PhigrosRenderer
- **Transform** (`engine/src/core/Transform.h`) — TRS + quaternion; `toMatrix()`
- **Engine** (`engine/src/engine/Engine.h/.cpp`) — main loop, owns all subsystems as members, owns GLFW callbacks and user pointer
- **GameClock** (`engine/src/engine/GameClock.h`) — wall clock + DSP time override for chart sync; header-only

> Full details: [docs/sys3_core_engine.md](docs/sys3_core_engine.md)

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

> Full details: [docs/sys4_input.md](docs/sys4_input.md)

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

> Full details: [docs/sys5_gameplay.md](docs/sys5_gameplay.md)

---

## System 6 — Game Mode Plugins

Plugin architecture: `GameModeRenderer` abstract interface + 5 implementations. Selected via `GameModeType` enum (DropNotes, Circle, ScanLine) with optional `DropDimension` (TwoD / ThreeD) for DropNotes.

| Plugin | Game | GameModeType | Notes |
|---|---|---|---|
| `BandoriRenderer` | BanG Dream | DropNotes + TwoD | Dynamic lane count from config, perspective camera, cross-lane holds with 4 transition styles, per-type note colors (Tap/Hold/Flick/Drag/Slide) |
| `ArcaeaRenderer` | Arcaea | DropNotes + ThreeD | Ground + sky regions, 32-segment arc ribbon meshes with X/Y easing, diamond ArcTap quads on arc paths |
| `LanotaRenderer` | Lanota | Circle | Concentric disks with keyframed rotate/scale/move animation, lane-enable mask timeline, per-lane spans (1/2/3) |
| `CytusRenderer` | Cytus | ScanLine | Variable-speed scan line with phase accumulation table, straight-line slides, multi-sweep holds, page visibility |
| `PhigrosRenderer` | Phigros | (unreachable) | Rotating judgment lines with SceneGraph — renderer exists but not wired to any GameModeType |

`Engine` holds active mode as `std::unique_ptr<GameModeRenderer>`. `Engine::createRenderer(config)` factory dispatches by GameModeType.
Game modes render via `Renderer&` — never allocate Vulkan resources directly.

The `GameModeRenderer` base class provides a `showJudgment()` virtual method for per-mode judgment display, and `onInit` accepts an optional `GameModeConfig` for runtime configuration.

**All 4 reachable modes are playable end-to-end** — create in editor, save, play via Test Game.

> Full details: [docs/sys6_game_modes.md](docs/sys6_game_modes.md)

---

## System 7 — Editor UI

Unity Hub-style editor built on ImGui + Vulkan. Layer-based flow:

```
ProjectHub → StartScreenEditor → MusicSelectionEditor → SongEditor → (TestGame process)
```

| Layer | Purpose |
|---|---|
| **Project Hub** | Browse + create + import projects; search box, per-project modification timestamp, select-then-Build-APK flow |
| **Start Screen Editor** | Background, logo, tap text, transition, audio; live preview with aspect-ratio control (landscape-only presets + custom W:H); per-section Default reset; auto text fit-to-width |
| **Music Selection Editor** | Arcaea-style song wheel with rhombus-slot FC/AP badges next to each title; page background + frosted overlay (heavy on sides, light middle); page-level FC/AP image drop zones; AI-picked 30-second audio preview that plays in real game / test game |
| **Song Editor** | DAW-style layout with per-mode features (see below) |
| **Test Game** | Green button on all editor pages; launches child process of `MusicGameEngineTest.exe --test <project>` |
| **Asset Browser** | Unified import system shared across all pages, "All Files" default; MAT tiles render live material previews that match the slot's target shape (note / track / arc / disk / ScanLine sweep) |

**SongEditor features:**
- DAW-style layout: scene preview + chart timeline simultaneous, left sidebar config, waveform strip
- Per-difficulty notes (Easy/Medium/Hard)
- Madmom beat analysis — auto-generate markers for 3 difficulties
- **Autocharter Place All** — feature-driven (strength/sustain/centroid) note-type + lane selection with anti-jack cooldown; AI... knob popup exposes thresholds
- **Editor Copilot** — natural-language chart edits via local Ollama / OpenAI-compatible endpoint; 6 ops, single-level undo
- Game mode config: DropNotes/Circle/ScanLine + 2D/3D dimension + track count + camera + HUD
- Multi-waypoint hold authoring via drag-to-record
- **Materials panel** — per-slot visual overrides (Unlit / Glow / Scroll / Pulse / Gradient / Custom), project-level `.mat` asset library shared across charts
- **AI Shader Generator** — for Custom-kind materials: describe an effect in English, worker writes & compiles a `.frag` with glslc-error retry loop
- **Music-selection preview clip (AI-picked)** — Auto-Detect button slides a 30-second window over the hardest-difficulty AI-marker strengths and selects the peak-energy region as the song's preview start; Start + Length sliders for manual tuning. The preview is played by the Music Selection page in real-game / test-game mode only.
- **Material Builder** — relocated from Start Screen; full CRUD (Unlit / Glow / Scroll / Pulse / Gradient / Custom) lives with the rest of the gameplay-authoring controls
- **Circle mode:** keyframed disk animation (rotate/scale/move) with easing
- **ScanLine mode:** paginated page-based authoring, per-page speed overrides, variable-speed keyframes, Cytus-style slides (LMB+RMB), multi-sweep holds
- **3D DropNotes mode:** multi-waypoint arc editor (click-to-place, chain merge on import), per-waypoint height handles, ArcTap click-to-place on parent arc
- **Player Settings page:** 4th editor layer previewing the shipped in-game settings screen
- Chart persistence: save/load as Unified Chart Format JSON

> Full details: [docs/sys7_editor.md](docs/sys7_editor.md)

---

## System 8 — Android Packaging

Build-APK pipeline that exports any project to a standalone Android app. Desktop code untouched; Android-specific code isolated in `AndroidVulkanContext`, `AndroidSwapchain`, `AndroidEngine`, `AndroidFileIO`, and `android_main.cpp`.

- **Toolchain:** AGP 8.5, NDK r27c, CMake, SDK 36, Build Tools 37.0.0
- **Asset pipeline:** Bundles project + shaders into APK
- **Network:** China-friendly (Aliyun mirrors, BITS for downloads)
- **UI:** "Build APK" button → save dialog → progress → APK ready
- **Features:** Landscape lock, window resize/rotation handlers, surface transform fix for portrait↔landscape

> Full details: [docs/sys8_android.md](docs/sys8_android.md)

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

One document per system, all under `docs/`:

| System | Document | Contents |
|---|---|---|
| System 1 | [docs/sys1_rendering.md](docs/sys1_rendering.md) | Vulkan backend, batchers, shaders, descriptor pools |
| System 2 | [docs/sys2_resources.md](docs/sys2_resources.md) | ChartTypes, UCF format, ChartLoader, AudioEngine, dynamic BPM |
| System 3 | [docs/sys3_core_engine.md](docs/sys3_core_engine.md) | ECS, SceneGraph, Engine main loop, GameClock, build system |
| System 4 | [docs/sys4_input.md](docs/sys4_input.md) | Keyboard + touch, gesture recognition, platform bridges, DPI |
| System 5 | [docs/sys5_gameplay.md](docs/sys5_gameplay.md) | HitDetector, judgment, score, cross-lane holds, sample-tick scoring |
| System 6 | [docs/sys6_game_modes.md](docs/sys6_game_modes.md) | GameModeRenderer interface + Bandori/Arcaea/Lanota/Cytus renderers |
| System 7 | [docs/sys7_editor.md](docs/sys7_editor.md) | SongEditor DAW layout, all authoring flows, arc editing, chart persistence |
| System 8 | [docs/sys8_android.md](docs/sys8_android.md) | APK packaging, Android Vulkan context, build flow, China network |

---

## Recent Additions

**2026-04-19**
- **AI Shader Generator** — natural-language → compiled `.spv` for custom-kind materials. Materials tab prompt → worker runs a compile-retry loop (default 3 attempts), feeding glslc errors back to the model on failure. Slot-aware prompt context (`targetMode` + `targetSlotSlug`) nudges the AI toward role-appropriate shaders. Uses `qwen2.5:3b` via Ollama by default; any OpenAI-compatible `http://` endpoint works.
- **Shared `AIChatRequest` helper** — extracted from `AIEditorClient` so the Copilot and the new Shader Generator share one HTTP/JSON path.
- **ShaderCompiler bug fix** — `_popen` on Windows was mangling the glslc path via `cmd.exe /c` outer-quote stripping. Fixed; hand-written Compile button in the Materials tab is fixed for free.

**2026-04-18**
- **Player Settings page** — 4th editor layer + in-game runtime. 8 settings (volumes, offset, note speed, dim, FPS, language), tap-to-calibrate audio-offset wizard, shared `SettingsPageUI` across editor / Test Game / Android runtime.
- **Material system (Phases 1–4)** — per-slot material overrides (Unlit / Glow / Scroll / Pulse / Gradient / Custom) on all 5 modes, `QuadBatch` + `MeshRenderer` pipeline-per-kind, 128 B push constants, promoted to **project-level `.mat` assets** with auto-migration, and a runtime `ShaderCompiler` that accepts custom `.frag` per material.
- **Arcaea 3D visual refresh** — hexagonal-prism arcs with outward-radial normals, rectangular-prism arctaps, flat ground shadows for both. Restart-from-pause freeze fixed (`GameClock::resume` in `launchGameplay`).
- **Autocharter Phase 1+2** — feature-driven Place All. `AudioAnalyzer` emits per-marker strength/sustain/centroid; `SongEditor` picks note type + lane with anti-jack cooldown. New AI... gear popup exposes the knobs.
- **Editor Copilot** — natural-language chart edits via local Ollama (default `qwen2.5:3b`) or any OpenAI-compatible endpoint. 6 ops (delete_range / insert / mirror_lanes / shift_lanes / shift_time / convert_type), single-level undo, Copilot panel in SongEditor Properties.

**2026-04-17**
- **3D Drop (Arcaea) end-to-end rebuild** — single-source-of-truth playfield constants, Bandori-style slot lane mapping with auto-expand, rectangle judgment gate, smooth per-frame arc clipping via host-mapped vertex buffer, world-space hit particles with lane-vs-sky-event routing.
- **Scan Line editor redesign** — paginated page-based authoring (one sweep = one page), per-page speed overrides, AI marker integration, cursor-follow scan line, auto page turning during playback and at page edges.
- **Arc editor multi-waypoint rework** — click-to-place workflow, chain merging on import, per-waypoint height handles.

**2026-04-12**
- **Arc/ArcTap editor** — 3-panel editing for Arcaea-style arcs: timeline ribbons (click-drag create), height curve editor (draggable start/end Y handles), cross-section preview (front-face view with arc dots). Full JSON round-trip, ArcaeaRenderer renders arc ribbons + diamond ArcTaps.
- **Scan Line variable-speed** — phase-accumulation table for keyframed scan speed, straight-line slides (Cytus-style with LMB+RMB nodes), multi-sweep holds
- **Circle disk animation** — keyframed rotate/scale/move with easing, lane-enable mask timeline
- **Cross-mode integration audit** — 10 bug fixes across all modes, Bandori Slide coloring, all 4 reachable modes verified end-to-end

**2026-04-10**
- **Cross-lane holds** — multi-waypoint hold authoring with 4 transition styles (Straight/Angle90/Curve/Rhomboid), Bandori-style sample-tick scoring
- **Game-mode factory fix** — Circle→LanotaRenderer, ScanLine→CytusRenderer
- **Test Game unification** — all Test Game buttons route through `Engine::spawnTestGameProcess()`

**2026-04-09 — Android packaging (Rounds 1–5)**
- APK pipeline, landscape lock, window resize/rotation handlers, surface transform fix for portrait↔landscape, StartScreen integration

**2026-04-04 ~ 2026-04-05**
- ChartLoader completion, Beat Analysis via Madmom (3 difficulty markers), DAW-style SongEditor layout, gameplay lead-in, HUD foreground fix

---

## Future Plans

### Priority 1: Android Packaging Validation
Round 5d swapchain extent fix needs on-device validation. Multi-device testing.

### Priority 2: Editor Polish
- BPM grid snapping for note placement
- Copy/paste note patterns
- Undo/redo
- Note drag-to-reposition

### Priority 3: Replay & Auto-Play
Deterministic replay record/playback, auto-play mode for chart validation.
