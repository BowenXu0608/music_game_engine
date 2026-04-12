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
