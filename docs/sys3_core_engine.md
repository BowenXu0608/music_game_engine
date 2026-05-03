---
name: Core Engine System
description: ECS, SceneGraph, Engine main loop, GameClock, game flow lifecycle, build config
type: project
originSessionId: d4e6dddd-1cc1-4f7b-8da6-079be9eb81c0
---
# System 3 — Core Engine ✅ COMPLETE

**Files:** `engine/src/core/`, `engine/src/engine/`

## Components

| Component | Purpose |
|---|---|
| ECS.h | EntityID, ComponentPool<T>, Registry (dense storage) |
| SceneNode.h/.cpp | Parent-child transform hierarchy (SceneGraph manager) |
| Transform.h | TRS + quaternion rotation + toMatrix() |
| Engine.h/.cpp | Main loop, owns GameModeRenderer, wires all systems |
| GameClock.h | Wall clock + DSP time override, header-only |

## Engine Main Loop

`Engine::mainLoop()` → poll events → resize check → `update(dt)` → `render()`.

**update(dt):**
1. Lead-in clock advancement (manual dt until audio starts)
2. DSP sync (`m_audio.positionSeconds()` when playing)
3. Input update (hold timeouts)
4. Particle update
5. Test mode transition
6. Gameplay: miss detection, hold sample ticks, slide ticks (Cytus), broken hold cleanup
7. Active mode `onUpdate`
8. Preview mode `onUpdate`
9. Song-end detection

**render():**
1. Background image into scene framebuffer
2. Active mode or preview mode `onRender`
3. ImGui frame (layer-specific panel)
4. ImGui render into command buffer

## Game Flow Lifecycle

**launchGameplay:** Load chart → create renderer → setMode → stop audio → set lead-in clock → switch to GamePlay layer.

**launchGameplayDirect:** Same but with pre-built ChartData (editor play).

**exitGameplay:** Stop audio → shutdown mode → clear touches → clear background → return to previous layer (or exit test mode).

**togglePause:** Pause/resume audio + clock + scene viewer.

**Restart (pause menu):** Stop + play audio, reset clock/judgment/score/touches.

**Results overlay:** Triggered when audio stops naturally. Shows score/combo/judgment breakdown with rank (S/A/B/C).

## Build Configuration

`CMakeLists.txt`: C++20, Vulkan, GLFW, GLM, VMA, STB, ImGui (all from `third_party/`). Outputs: `MusicGameEngine.lib` + `MusicGameEngineTest.exe`.

`NOMINMAX` guard before `<windows.h>` in Engine.cpp to prevent min/max macro conflicts.

OLE initialized in Engine constructor (for Windows drag-drop / file dialogs).

## Auto-save + crash safety

`Engine` owns the editor-state autosave (cadence + crash hooks); the editors own the actual write logic. Surface area (in `Engine.h`):

```cpp
void markEditorDirty();              // editor mutators / ambient mouse-up
void performAutoSaveNow(const char*); // unified flush: timer / close / crash
bool autoSaveStatusActive() const;
const std::string& autoSaveStatusMsg() const;
static Engine* s_autosaveInstance;   // for file-static OS callbacks
```

**`tickAutoSave(dt)`** runs every frame from `mainLoop`. Ambient detection: any `ImGui::IsMouseReleased(0|1|2)` outside `EditorLayer::GamePlay` flips `m_editorDirty = true`. Cadence: `m_autoSaveTimer += dt` while dirty; fires `performAutoSaveNow("auto")` at `kAutoSaveIntervalSec = 30 s`. Drag-safe: skips the fire if `ImGui::IsAnyMouseDown()` and rewinds the sub-second tail of the timer so the next frame after mouse-up retries immediately. Going clean → dirty resets the timer (next save is 30 s after the *first* edit, not relative to the previous save).

**`performAutoSaveNow(reason)`** wraps `m_songEditor.flushChartsForAutoSave()` and `m_musicSelectionEditor.save()` in independent `try/catch` so a throw in one editor doesn't skip the other. Clears `m_editorDirty`, resets the timer, posts a 3 s pale-blue `Auto-saved (reason)` toast.

**Crash hooks (`installCrashHooks`)** installed once after window init:

| Hook | Trigger | Reason string |
|---|---|---|
| `glfwSetWindowCloseCallback` | X-button close | `close` |
| `SetUnhandledExceptionFilter` (Win) | uncaught SEH | `crash` |
| `SetConsoleCtrlHandler` (Win) | Ctrl+C / console close | `ctrl` |
| `std::set_terminate` | unhandled C++ exception | `terminate` |
| `mainLoop` post-loop | clean shutdown | `exit` |

All callbacks reach the engine through `Engine::s_autosaveInstance` (file-static; first instance wins). `SetUnhandledExceptionFilter` returns `EXCEPTION_CONTINUE_SEARCH` after flushing so debugger / WER still gets the crash. `set_terminate` calls `std::abort()` after flushing. `performAutoSaveNow` is idempotent (clears `m_editorDirty`) so cascading triggers don't double-write.

## Player-game / editor split (2026-05-03)

`Engine` is now a player-engine implementation, not the only thing the player sees. The structural changes:

### IPlayerEngine — the abstraction layer

New `engine/src/engine/IPlayerEngine.h` is a pure-virtual interface that exposes the **player-facing** surface of the engine. Every game-side view class (`StartScreenView`, `MusicSelectionView`, `GameplayHudView`, `ResultsView`) takes `IPlayerEngine&` (or `*` when nullable transitions are okay) when it needs engine services. The interface members:

```cpp
virtual AudioEngine&          audio()           = 0;
virtual Renderer&             renderer()        = 0;
virtual GameClock&            clock()           = 0;
virtual PlayerSettings&       playerSettings()  = 0;
virtual MaterialAssetLibrary& materialLibrary() = 0;
virtual InputManager&         inputManager()    = 0;
virtual ImGuiLayer*           imguiLayer()      = 0;   // nullable — Android returns nullptr
virtual ScoreTracker&         score()           = 0;
virtual JudgmentSystem&       judgment()        = 0;
virtual HitDetector&          hitDetector()     = 0;
virtual GameModeRenderer*     activeMode()      = 0;
virtual const GameModeConfig& gameplayConfig()  const = 0;
virtual bool  isTestMode()         const = 0;
virtual bool  isTestTransitioning() const = 0;
virtual float testTransProgress()   const = 0;
virtual void launchGameplay(const SongInfo&, Difficulty, const std::string& projectPath, bool autoPlay) = 0;
virtual void exitGameplay() = 0;
```

`Engine : public IPlayerEngine`. Existing accessors got `override`. New public accessors added: `score()`, `judgment()`, `hitDetector()`, `activeMode()`, `gameplayConfig()`, `imguiLayer()` (returns `&m_imgui`). Engine's `launchGameplay` and `exitGameplay` got `override`.

The Android side ships `AndroidEngineAdapter : public IPlayerEngine` (in `engine/src/android/`) wrapping `AndroidEngine&`. `imguiLayer()` returns `nullptr`; `isTestMode()` returns `false` (phone players are not in test mode).

### Player views own their rendering, not Engine

`Engine.cpp` no longer hosts the gameplay-HUD or results-overlay rendering. Both moved to game-side `View` classes:

- `Engine::renderGameplayHUD()` is now a thin wrapper: composites the desktop offscreen scene texture (`m_sceneViewer.sceneTexture()`) into the swapchain, then `m_hudView.render(displaySz, *this)`, then dispatches to `renderResultsOverlay` / `renderPauseOverlay` based on state. The score/combo panel drawing (~110 lines) lives in `GameplayHudView::render`.
- `Engine::renderResultsOverlay()` is a one-liner: `m_resultsView.render(ImGui::GetIO().DisplaySize, *this)`. The full results panel (background dim, score / max combo / Perfect-Good-Bad-Miss / Back button) lives in `ResultsView::render`.
- `Engine::renderPauseOverlay()` stays in Engine — pause is desktop-keyboard-only, not part of the phone player loop.

`Engine.h` gained `GameplayHudView m_hudView;` and `ResultsView m_resultsView;` members.

### What stayed in Engine

Everything that's desktop-specific or editor-coupled: `m_sceneViewer` offscreen-render compositing, `EditorLayer` state machine, `m_preGameplayLayer`, `m_testMode` / `m_testReturnLayer`, `restartGameplay`, `togglePause`, `renderPauseOverlay`, `m_currentChart` cache for restart, the GLFW callbacks (`framebufferResize`, `key`, `mouseButton`, `cursorPos`, `drop`), `applyPlayerSettings`, `setMode`, `createRenderer`, `loadBackgroundTexture`, the gesture handlers (`handleGestureLaneBased`, `handleGestureArcaea`, `handleGesturePhigros`, `handleGestureCircle`, `handleGestureScanLine`), the auto-save subsystem.

`Engine::launchGameplay` also stayed Engine-specific. The shared launch helper (`GameplayLauncher`) was descoped — `Engine::launchGameplay` and `AndroidEngine::startGameplay` diverge for valid platform reasons (`openProject`/`m_sceneViewer`/`EditorLayer` vs. `extractToInternal`/`GameScreen`); lifting the common ~25-line spine would have required plumbing ~10 currently-private methods through `IPlayerEngine`. The interface's `launchGameplay` virtual is the cleavage point. Memory note: `project_gameplay_launcher_deferred.md`.

### Direction of travel

Player-facing rendering should keep moving toward `engine/src/game/screens/`. Future views (settings overlay, achievement popup, network errors) follow the same pattern: pure rendering + state owned in the view; desktop editor wraps it; Android engine drives it via the adapter. The class-name test is "does the name describe an editor concept?" If yes, extract a game-side counterpart instead of porting the editor class.
