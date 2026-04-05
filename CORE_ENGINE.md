# System 3 — Core Engine

**Last updated:** 2026-04-05  
**Status:** ✅ Complete

Foundational runtime layer: data model (ECS + SceneGraph), main loop (Engine), timing (GameClock), and build system.  
See also: [README.md](README.md) | [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md)

---

## Components Overview

```
System 3 — Core Engine
├── ECS.h          — Entity-Component System (dense storage, sparse map)
├── SceneNode.h    — SceneNode (data) + SceneGraph (manager)
├── Transform.h    — TRS + quaternion + toMatrix()
├── Engine.h/.cpp  — Main loop, owns all subsystems, GLFW callbacks
└── GameClock.h    — Wall clock + DSP time override, header-only
```

---

## ECS — `engine/src/core/ECS.h`

Header-only. Used for game logic and note data during gameplay.

| Class | Details |
|---|---|
| `EntityID` | `uint32_t` handle |
| `ComponentPool<T>` | Dense packed array + sparse index map. O(1) add/get/remove |
| `Registry` | Owns all `ComponentPool`s. Create/destroy entities, iterate views |

**Design rationale:** ECS is used for note entities that need fast per-frame iteration (update + render). The dense storage means cache-friendly traversal even with thousands of active notes.

**Usage pattern:**
```cpp
Registry ecs;

// Create a note entity
EntityID note = ecs.create();
ecs.add<NoteData>(note, { .time = 1.5f, .lane = 3 });
ecs.add<Transform>(note, { .position = {x, y, 0.f} });

// Per-frame update (cache-friendly)
for (auto [id, nd, tr] : ecs.view<NoteData, Transform>()) {
    tr.position.y += scrollSpeed * dt;
}

// Cleanup
ecs.destroy(note);
```

---

## SceneNode / SceneGraph — `engine/src/core/SceneNode.h/.cpp`

Used primarily by **PhigrosRenderer** for rotating judgment line hierarchies.  
`SceneNode.cpp` is an intentionally **empty** translation unit — all logic is in the header.

| Class | Details |
|---|---|
| `SceneNode` | Data node: `localTransform`, `worldMatrix`, parent/child IDs |
| `SceneGraph` | Manager: `createNode(parentID)`, `markDirty(id)`, `update()`, `worldMatrix(id)` |

**Design rationale:** Scene graph is only for spatial hierarchies, not general game logic. Phigros needs it because notes are children of judgment lines — rotating the line propagates to all its notes automatically.

**Usage pattern:**
```cpp
SceneGraph scene;

NodeID line = scene.createNode();
NodeID note = scene.createNode(line);   // child of line

scene.get(note)->localTransform.position = {x, y, 0.f};
scene.markDirty(line);   // marks line + all children dirty

scene.update();          // resolves world matrices top-down

glm::mat4 world = scene.worldMatrix(note);
// world includes parent line's rotation applied to note's local transform
```

---

## Transform — `engine/src/core/Transform.h`

Header-only TRS (Translate-Rotate-Scale) transform used by both ECS and SceneGraph.

```cpp
struct Transform {
    glm::vec3 position  = {0, 0, 0};
    glm::quat rotation  = glm::identity<glm::quat>();
    glm::vec3 scale     = {1, 1, 1};

    glm::mat4 toMatrix() const;   // TRS → mat4
};
```

Quaternion rotation avoids gimbal lock for Phigros line rotations.

---

## Engine — `engine/src/engine/Engine.h/.cpp`

Main orchestrator. Owns and wires all other systems.

### Ownership Map

```cpp
class Engine {
    // System 1 — Rendering
    Renderer                     m_renderer;

    // System 2 — Resource Management
    AudioEngine                  m_audio;

    // System 3 — Core (self)
    GameClock                    m_clock;

    // System 4+5 — Input & Gameplay
    InputManager                 m_input;
    HitDetector                  m_hitDetector;
    JudgmentSystem               m_judgment;
    ScoreTracker                 m_score;

    // System 6 — Game Mode Plugin (active)
    unique_ptr<GameModeRenderer> m_activeMode;

    // System 7 — Editor UI
    ImGuiLayer                   m_imgui;
    ProjectHub                   m_hub;
    StartScreenEditor            m_startScreen;
    MusicSelectionEditor         m_musicSelection;
    SongEditor                   m_songEditor;
    SceneViewer                  m_sceneViewer;

    // Hold tracking (Input ↔ Gameplay bridge)
    unordered_map<int32_t, uint32_t> m_activeTouches;  // touchId → noteId
};
```

### Main Loop

```
glfwPollEvents()
  → keyCallback          → m_input.onKey()
  → mouseButtonCallback  → m_input.injectTouch(-1, Began/Ended, ...)
  → cursorPosCallback    → m_input.onMouseMove()
  → dropCallback         → forward file paths to active editor

Engine::update(dt)
  → m_clock.tick()
  → m_input.update(songTime)        ← fires hold timeouts
  → m_hitDetector.update()          ← removes expired notes
  → m_activeMode->update(dt, t)     ← game logic (if playing)

Engine::render()
  → m_renderer.beginFrame()
  → m_activeMode->render(renderer)  ← game draws via batchers (if playing)
  → m_imgui.render()                ← editor UI overlay always
  → m_renderer.endFrame()
```

### GLFW Callback Ownership

**Critical rule:** Engine registers ALL GLFW callbacks and stores `Engine*` as the user pointer via `glfwSetWindowUserPointer`. `InputManager::init()` is a **no-op** — it must NOT call `glfwSetWindowUserPointer`.

Prior bug: InputManager called `glfwSetWindowUserPointer`, overwriting Engine's pointer. All GLFW callbacks then crashed when trying to retrieve `Engine*`.

### Gesture Dispatch

Engine routes gesture events to the correct hit mode based on the active plugin:

```cpp
void Engine::handleGesture(const GestureEvent& e) {
    if (dynamic_cast<ArcaeaRenderer*>(m_activeMode.get()))
        handleGestureArcaea(e);
    else if (dynamic_cast<PhigrosRenderer*>(m_activeMode.get()))
        handleGesturePhigros(e);
    else
        handleGestureLaneBased(e);   // Bandori, Cytus, Lanota
}
```

### Hold Tracking Bridge

`m_activeTouches` maps touch finger ID → note ID across frames:
- `GestureType::HoldBegin` → `beginHold()` → insert into `m_activeTouches`
- `GestureType::HoldEnd` → lookup in `m_activeTouches` → `endHold()` → remove

---

## GameClock — `engine/src/engine/GameClock.h`

Header-only. Provides a unified time source that switches between two modes:

| Mode | Source | When used |
|---|---|---|
| Wall clock | `std::chrono::high_resolution_clock` | Menu, editing, paused |
| DSP clock | `AudioEngine::currentTime()` | Song playing |

`currentTime()` returns DSP time when audio is running (for precise chart sync), wall clock otherwise.

```cpp
class GameClock {
public:
    void tick(AudioEngine& audio);
    double currentTime() const;   // DSP time if playing, wall clock otherwise
    double deltaTime() const;     // Frame delta (capped at 100ms for debugger pauses)
};
```

---

## Gameplay Lead-in Clock Management (Added 2026-04-04)

`Engine::update()` has a special code path for the lead-in period before audio starts:

```
Engine::update(dt)
  ├─ if m_gameplayLeadIn:
  │     songTime += dt              ← manual wall-clock advance
  │     if songTime >= 0.0:
  │         AudioEngine::play()     ← start audio
  │         m_audioStarted = true
  │         m_gameplayLeadIn = false
  └─ else (normal):
        songTime = AudioEngine::currentTime()  ← DSP sync
```

New Engine members for lead-in state:
- `m_gameplayLeadIn` (`bool`) — true from `launchGameplay()` until audio starts
- `m_audioStarted` (`bool`) — prevents double-start of audio
- `m_pendingAudioPath` (`std::string`) — deferred audio path for lead-in

The initial song time is set to `-(2.0 + audioOffset)` by `launchGameplay()`, giving a visual lead-in before the first beat.

---

## AudioEngine Waveform LOD Bug Fix (2026-04-04)

`AudioEngine::decodeWaveform()` generates multi-level LOD waveform data. The LOD generation loop previously took a `const WaveformLOD& prev` reference to the last element of a `std::vector<WaveformLOD>`, then called `emplace_back()` to add the next level. Since `emplace_back()` can reallocate the vector, `prev` became a dangling reference, causing a crash.

**Fix:** Build each coarse LOD in a local `WaveformLOD` variable, then `std::move` it into the vector after computation is complete.

---

## Build System

### Structure

```
CMakeLists.txt (root)
├── C++20, GLOB_RECURSE for engine/src/**/*.cpp
├── find_package: Vulkan, glfw3, glm
├── VMA_INCLUDE_DIR: third_party/vma/
├── ImGui sources: third_party/imgui/**
├── nlohmann/json: third_party/json/
├── stb: third_party/stb/
├── Shader compilation: glslc → build/shaders/*.spv
└── Post-build copy: build/shaders/ → build/Debug/shaders/
```

### Shader Compilation

```cmake
file(GLOB SHADER_SOURCES "shaders/*.vert" "shaders/*.frag" "shaders/*.comp")
foreach(SHADER ${SHADER_SOURCES})
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv
        COMMAND glslc ${SHADER} -o ...spv
        DEPENDS ${SHADER}
    )
endforeach()

# Post-build: copy spv files next to the exe
add_custom_command(TARGET MusicGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders $<TARGET_FILE_DIR:MusicGame>/shaders
)
```

### Working Directory

`main()` calls `GetModuleFileNameA` at startup → sets CWD to the exe's directory (`build/Debug/`).  
This ensures `../../Projects` always resolves correctly regardless of how the exe is launched.

### Build Defines

| Define | When | Effect |
|---|---|---|
| `ENABLE_VALIDATION_LAYERS` | Debug builds | Enables `VK_LAYER_KHRONOS_validation` |
| `VMA_IMPLEMENTATION` | `BufferManager.cpp` only | Instantiates VMA (once) |
| `STB_IMAGE_IMPLEMENTATION` | `TextureManager.cpp` only | Instantiates stb_image (once) |
| `MINIAUDIO_IMPLEMENTATION` | `AudioEngine.cpp` only | Instantiates miniaudio (once) |

### Windows Pitfalls

- **`near`/`far` macros:** Windows.h defines these. Use `nearZ`/`farZ` in renderer code.
- **`min`/`max` macros:** Use `(std::min)()` with parentheses, or `#define NOMINMAX` before including Windows.h.
