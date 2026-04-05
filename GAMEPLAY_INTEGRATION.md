# Gameplay Integration

**Last updated:** 2026-04-05  
**Status:** ✅ Complete

Wires the full gameplay path: **Play button → ChartLoader → GameModeRenderer → AudioEngine → live play → pause → results → exit**.  
Test Game now runs as a **separate process** via `MusicGameEngineTest.exe`.  
See also: [CORE_ENGINE.md](CORE_ENGINE.md) | [INPUT_SYSTEM.md](INPUT_SYSTEM.md) | [GAME_MODES.md](GAME_MODES.md)

---

## Overview

All 7 engine subsystems were implemented independently. This integration connects them into a playable loop:

```
MusicSelectionEditor ──Play──┐
                             ├──► Engine::launchGameplay()
                             │        │
                             │        ├─ ChartLoader::load(chartPath)
                             │        ├─ createRenderer(gameMode)
                             │        ├─ setMode(renderer, chart, config*)
                             │        ├─ loadAudio(audioPath)
                             │        └─ switchLayer(GamePlay)
                             │                │
                             │                ▼
                             │        ┌─────────────┐
                             │        │  Game Loop   │
                             │        │  HUD overlay │◄──── ESC toggles pause
                             │        │  Score/Combo │
                             │        └──────┬──────┘
                             │               │ audio ends (songTime > 2.0)
                             │               ▼
                             │        ┌─────────────┐
                             │        │  Results     │
                             │        │  Score/Stats │──── Back → previous screen
                             │        └─────────────┘
                             │
SongEditor ──Test Game───────┘  (separate process)
      │
      ├─ exportAllCharts()          — write chart JSON to disk
      ├─ saveProject()              — persist project state
      └─ CreateProcessW("MusicGameEngineTest.exe --test <project_path>")
              │
              ▼
      ┌──────────────────────┐
      │  Child process       │
      │  own window + loop   │
      │  ESC closes window   │
      └──────────────────────┘
```

---

## Entry Points

### 1. Music Selection → Play (in-process)

**File:** `engine/src/ui/MusicSelectionEditor.cpp` (line ~909)

User selects a song set, song, and difficulty, then presses the Play button. Calls:

```cpp
m_engine->launchGameplay(song, m_selectedDifficulty, m_projectPath);
```

### 2. Song Editor → Test Game (separate process)

**File:** `engine/src/ui/SongEditor.cpp` (line ~158)

Green "Test Game" button in the top-right of the editor. No longer launches gameplay in the editor process. Instead:

1. Calls `exportAllCharts()` to write all chart files to disk as unified JSON.
2. Saves the project.
3. Spawns a child process via `CreateProcessW`:
   ```
   MusicGameEngineTest.exe --test <project_path>
   ```
4. The child process opens its own window and runs the full game flow independently.
5. The editor remains fully functional and unaffected.

### 3. `main.cpp --test` mode

**File:** `engine/src/main.cpp`

`main.cpp` now parses `--test <project_path>` from command-line arguments. In test mode:

1. Creates an `Engine` instance.
2. Loads the project at the given path.
3. Calls `enterTestMode()` to begin gameplay.
4. Runs the game loop in its own window.
5. ESC closes the test window (exits the process).

---

## Core Methods

All implemented in `engine/src/engine/Engine.h` / `Engine.cpp`.

### `launchGameplay(song, difficulty, projectPath)`

1. Resolves chart path from `SongInfo` + `Difficulty` (Easy/Medium/Hard)
2. Loads chart via `ChartLoader::load()`
3. Creates the correct renderer via `createRenderer()` factory
4. Calls `setMode(renderer, chart, config*)` — initializes renderer with optional `GameModeConfig*`, sets up HitDetector, resets judgment + score
5. Stops any existing audio, loads new audio, starts playback
6. Saves current layer (to return later), switches to `EditorLayer::GamePlay`

### `launchGameplayDirect(chartData, ...)`

Accepts `ChartData` directly without loading from file. Primarily used internally (e.g., by test mode after chart data is already in memory).

### `createRenderer(config)` — Game Mode Factory

Maps `GameModeConfig` to the correct renderer:

| GameModeType | DropDimension | Renderer |
|---|---|---|
| `DropNotes` | `TwoD` | `BandoriRenderer` |
| `DropNotes` | `ThreeD` | `ArcaeaRenderer` |
| `Circle` | `TwoD` | `CytusRenderer` |
| `Circle` | `ThreeD` | `LanotaRenderer` |
| `ScanLine` | (any) | `PhigrosRenderer` |

### `setMode(renderer, chart, config*)`

Initializes the renderer with `onInit(config*)`, passing optional `GameModeConfig*` so the renderer can read camera and track settings from the config.

### `exitGameplay()` / `exitTestMode()`

Calls `onShutdown()` on the active renderer before resetting state. Clears `activeTouches`. Stops audio, resets pause/results state, returns to the layer the user came from (MusicSelection or SongEditor). `HitDetector` clears `activeHolds` on init.

### `togglePause()`

Toggles `m_gameplayPaused`. When paused: `AudioEngine::pause()`, `GameClock::pause()`, `SceneViewer::setPlaying(false)`. When resumed: reverses all three.

---

## Gameplay HUD — `renderGameplayHUD()`

Rendered during `EditorLayer::GamePlay` in the main `render()` switch. Replaces the previous `SceneViewer` mockup.

**Layers (front to back):**

1. **Scene texture** — full-screen quad showing the game mode renderer output (notes, lanes, arcs, etc.)
2. **Score** — positioned and styled via `GameModeConfig::scoreHud` (`HudTextConfig`)
3. **Combo** — positioned and styled via `GameModeConfig::comboHud` (`HudTextConfig`), shown only when combo > 0
4. **Pause overlay** — shown when ESC pressed (see below)
5. **Results overlay** — shown when song ends (see below)

### HUD Configuration via `GameModeConfig`

`GameModeConfig` now stores `scoreHud` and `comboHud` fields (type `HudTextConfig`), which control position, color, and size of the HUD elements. These same settings are used by both the Scene preview in the editor and the live gameplay HUD overlay in `Engine::renderGameplayHUD`.

---

## Hit Effects & Judgment

### Particle Bursts (BandoriRenderer)

Colored judgment squares have been removed. Only particle burst effects remain:

| Judgment | Color | Particle Count |
|---|---|---|
| **Perfect** | Green | 20 |
| **Good** | Blue | 14 |
| **Bad** | Red | 8 |
| **Miss** | (none) | 0 |

### `showJudgment()` — Virtual Base Method

`showJudgment()` has been moved from `BandoriRenderer` to `GameModeRenderer` as a virtual method. Each game mode renderer can override it to provide mode-specific visual feedback.

### Miss Detection

`HitDetector::update(songTime)` now returns `std::vector<MissedNote>` containing notes that have expired past their hit window. The `Engine` dispatches Miss judgments with lane info for each missed note, feeding into `ScoreTracker` and triggering `showJudgment()` (which produces no particles for Miss).

---

## Camera & Lane Configuration

### Camera Config Passed to Renderer

`setMode()` and `onInit()` now accept an optional `GameModeConfig*`. `BandoriRenderer` reads:

- `cameraEye` — camera position
- `cameraTarget` — look-at point
- `cameraFov` — field of view

Lane count comes from `config.trackCount`.

### Lane Spacing Auto-Calculated

`BandoriRenderer` computes `m_laneSpacing` dynamically based on the camera FOV and screen aspect ratio so the highway fills approximately 88% of screen width. No hardcoded spacing values.

---

## Input Handling

### Lane-Based Gesture Handling

`handleGestureLaneBased` uses `m_gameplayConfig.trackCount` instead of a hardcoded 7-lane assumption. Supports variable lane counts per game mode config.

### Keyboard Mapping (Extended)

Supports up to 12 lanes:

| Key | Lane |
|---|---|
| 1–9 | Lanes 1–9 |
| 0 | Lane 10 |
| Q | Lane 11 |
| W | Lane 12 |

---

## Pause System

**Trigger:** ESC key during gameplay (modified in `keyCallback` — ESC no longer closes the window during gameplay).

**Pause overlay (`renderPauseOverlay()`):**
- Semi-transparent black background (alpha 160)
- Centered ImGui window titled "Paused" with three buttons:
  - **Resume** — unpauses, audio + clock resume
  - **Restart** — resets score/combo/judgment, replays audio from 0
  - **Exit** — calls `exitGameplay()`, returns to previous screen

---

## Song End & Results

**Detection:** In `Engine::update()`, after the game mode update block:
- If `!m_audio.isPlaying()` and `songTime > 2.0` (avoids false trigger at song start or during short initial silence), sets `m_showResults = true` and stops scene updates.

**Results overlay (`renderResultsOverlay()`):**
- Semi-transparent black background (alpha 180)
- Centered panel showing:
  - **Score** (7-digit) and **Max Combo**
  - **Perfect** (yellow), **Good** (green), **Bad** (blue), **Miss** (red) counts
  - **Back** button → `exitGameplay()`

---

## Chart Export Format

Charts are exported as unified JSON. The first line contains `{"version": "1.0", ...}` so that `ChartLoader::load` correctly dispatches to `loadUnified`. This is the format written by `exportAllCharts()` in SongEditor before spawning the test process.

---

## Gameplay Lead-in and Audio Offset (Added 2026-04-04)

### Audio Offset

New `audioOffset` field added to `GameModeConfig`:
- Editable slider in SongEditor Audio section (range: -2.0 to +2.0 seconds)
- Saved/loaded in project JSON as `"audioOffset"` key per-song
- Positive offset = audio starts later, negative = audio starts earlier

### 2-Second Visual Lead-in

Before gameplay begins, there is a 2-second visual lead-in period where notes scroll toward the hit zone but no audio plays yet:

1. Game clock starts at `-(2.0 + audioOffset)` (negative time)
2. Notes scroll in visually before the first note reaches the hit zone
3. Audio starts when the game clock reaches `0.0`
4. After audio starts, `GameClock` switches to DSP-synced time as usual

### Engine Implementation

New members in `Engine`:
- `m_gameplayLeadIn` (`bool`) — true during the lead-in phase
- `m_audioStarted` (`bool`) — tracks whether audio playback has begun
- `m_pendingAudioPath` (`std::string`) — path to audio file, played when lead-in ends

`Engine::update()` behavior during lead-in:
- Clock is advanced manually each frame (wall-clock delta) instead of reading DSP time
- When `songTime >= 0.0`, calls `AudioEngine::play(m_pendingAudioPath)`, sets `m_audioStarted = true`, `m_gameplayLeadIn = false`
- After lead-in, normal DSP-synced clock resumes

---

## Gameplay HUD Fix (2026-04-04)

### Problem

Score and combo text were **invisible during Test Game** because the ImGui HUD window was layered behind the scene texture. The scene texture is drawn as a full-screen `ImGui::Image()`, and the HUD window rendered on top via ImGui but was still occluded by the texture quad.

### Fix

HUD rendering now uses `ImGui::GetForegroundDrawList()` instead of a regular ImGui window. The foreground draw list renders on top of **all** ImGui windows and textures, guaranteeing visibility.

Additional improvements:
- Semi-transparent background panels behind score and combo text for readability
- Added "SCORE" and "COMBO" labels above/below the numbers
- HUD elements still use `HudTextConfig` from `GameModeConfig` for position, color, size, etc.

---

## Data Flow During Play

```
glfwPollEvents()
  └─ keyCallback / mouseButtonCallback / cursorPosCallback
       └─ InputManager → GestureRecognizer

Engine::update(dt)
  ├─ AudioEngine::positionSeconds() → GameClock::setSongTime()  (DSP sync)
  ├─ InputManager::update(songTime)                              (hold timeouts)
  ├─ HitDetector::update(songTime) → vector<MissedNote>          (expire missed notes)
  │     └─ Engine dispatches Miss judgments with lane info
  ├─ GameModeRenderer::onUpdate(dt, songTime)                    (animate notes)
  └─ Song-end detection (songTime > 2.0 && !audio.isPlaying())

Engine::render()
  ├─ GameModeRenderer::onRender(renderer)                        (draw notes to scene)
  └─ renderGameplayHUD()                                         (ImGui score/combo/overlays)

Gesture callback (fires on tap/hold/flick/slide)
  └─ Mode-specific handler (lane-based / Arcaea / Phigros)
       ├─ HitDetector::checkHit*()
       ├─ JudgmentSystem::judge*()
       ├─ ScoreTracker::onJudgment()
       └─ GameModeRenderer::showJudgment()  (particle burst)
```

---

## Files Modified

| File | Change |
|---|---|
| `engine/src/main.cpp` | `--test <project_path>` argument parsing, test mode game loop |
| `engine/src/engine/Engine.h` | Added `launchGameplayDirect`, `enterTestMode`, `exitTestMode`, config-aware `setMode`/`onInit`, HUD config members |
| `engine/src/engine/Engine.cpp` | Factory, lifecycle, HUD (config-driven), pause, results, ESC handling, song-end detection (>2.0s), proper cleanup (`onShutdown`, clear touches/holds), miss dispatch |
| `engine/src/ui/MusicSelectionEditor.cpp` | Play button wired to `launchGameplay()` |
| `engine/src/ui/SongEditor.cpp` | Test Game button: `exportAllCharts()` + save + `CreateProcessW` child process |
| `engine/src/ui/SceneViewer.h` | Added `sceneTexture()` getter |
| `engine/src/gameplay/HitDetector.cpp` | `update()` returns `vector<MissedNote>`, `activeHolds` cleared on init |
| `engine/src/gameplay/GameModeRenderer.h` | `showJudgment()` virtual method, `onInit(config*)` signature |
| `engine/src/modes/BandoriRenderer.cpp` | Removed judgment squares, particle-only effects, dynamic lane spacing from FOV, camera config from `GameModeConfig` |
| `engine/src/engine/AudioAnalyzer.h/.cpp` | Beat detection via Madmom subprocess (2026-04-04) |
| `tools/analyze_audio.py` | Python script for beat/downbeat/onset detection (2026-04-04) |

---

## Expected Behavior

### Music Selection → Play
1. Open a project, navigate to Music Selection
2. Select a song set, song, and difficulty
3. Click Play → screen switches to fullscreen gameplay
4. Audio plays, notes render and scroll according to game mode
5. Score and combo update on hits (position/style from HUD config)
6. Particle bursts on hit: green (Perfect), blue (Good), red (Bad)
7. Console prints hit feedback: `Hit - Perfect | Score: 1000 | Combo: 1`

### Song Editor → Test Game (separate process)
1. Open a song in the editor
2. Click green "Test Game" button (top-right)
3. Charts are exported, project is saved
4. A new window opens (`MusicGameEngineTest.exe --test <path>`)
5. Gameplay runs in the child process; editor remains usable
6. ESC in the test window closes it

### Pause (ESC)
1. Press ESC during gameplay → audio freezes, game pauses
2. Dark overlay with "Paused" window appears
3. **Resume** → continues playback from where it stopped
4. **Restart** → resets everything, replays from beginning
5. **Exit** → returns to Music Selection or Song Editor

### Song End → Results
1. Audio finishes and songTime exceeds 2.0s → dark overlay with "Results" panel
2. Shows final score, max combo, and per-judgment counts (color-coded)
3. Click **Back** → returns to previous screen

### Prerequisites
- The selected song must have a valid chart file assigned for the chosen difficulty
- The audio file must exist at the path specified in `SongInfo.audioFile`
- If chart or audio is missing, the console logs an error and gameplay does not launch
- For Test Game: `MusicGameEngineTest.exe` must be in the executable search path

---

## Known Limitations

- **Single audio track** — AudioEngine supports one sound at a time (no SFX mixing yet)
- **No HUD scaling** — score/combo text uses HudTextConfig but no DPI-aware scaling
- **No score persistence** — results are displayed but not saved back to `SongInfo.score`
- **Slide accuracy placeholder** — `HitDetector::getSlideAccuracy()` returns hardcoded 0.05f (TODO at `HitDetector.cpp:168`)
- **Lead-in not configurable** — fixed at 2 seconds plus audio offset (not exposed to editor yet)
