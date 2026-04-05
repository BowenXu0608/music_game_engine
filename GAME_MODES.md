# System 6 — Game Mode Plugins

**Last updated:** 2026-04-04  
**Status:** ✅ Complete (5 modes implemented, gameplay integration in progress)

Plugin architecture: `GameModeRenderer` abstract interface + 5 concrete implementations.  
See also: [README.md](README.md) | [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md) | [INPUT_SYSTEM.md](INPUT_SYSTEM.md)

---

## Plugin Interface — `engine/src/game/modes/GameModeRenderer.h`

All game modes implement this pure virtual interface. Engine holds the active mode as `std::unique_ptr<GameModeRenderer>`.

The header includes `gameplay/JudgmentSystem.h` and forward-declares `struct GameModeConfig`.

```cpp
#include "gameplay/JudgmentSystem.h"

struct GameModeConfig; // forward declaration

class GameModeRenderer {
public:
    virtual ~GameModeRenderer() = default;

    // Called once after construction, before first frame
    // Optional config for mode-specific settings (camera, track count, etc.)
    virtual void init(Renderer& renderer, const GameModeConfig* config = nullptr) = 0;

    // Load and parse the chart; called before play starts
    virtual void loadChart(const ChartData& chart) = 0;

    // Per-frame update: advance note positions, spawn particles, etc.
    virtual void update(double dt, double songTime) = 0;

    // Per-frame render: draw notes via renderer's batchers
    virtual void render(Renderer& renderer) = 0;

    // Handle a recognized gesture event from InputManager
    virtual void onGesture(const GestureEvent& e) = 0;

    // Called on window resize
    virtual void onResize(uint32_t w, uint32_t h) = 0;

    // Clean up GPU resources
    virtual void cleanup() = 0;

    // Camera used by this mode (Renderer::setCamera is called with this)
    virtual const Camera& getCamera() const = 0;

    // Display a judgment result on a lane (default no-op)
    virtual void showJudgment(int lane, Judgment judgment) {}
};
```

**Design rule:** Game modes render via `Renderer&` only — they **never** allocate Vulkan resources directly. They access shared GPU resources via:
- `renderer.quads()` — QuadBatch
- `renderer.lines()` — LineBatch
- `renderer.meshes()` — MeshRenderer
- `renderer.particles()` — ParticleSystem
- `renderer.whiteView()` / `renderer.whiteSampler()` — fallback white texture
- `renderer.descriptors()` — DescriptorManager (for custom textures)

---

## JudgmentDisplay Colors

| Judgment | Color |
|----------|-------|
| Perfect  | Green |
| Good     | Blue  |
| Bad      | Red   |
| Miss     | Gray  |

---

## Mode Switching

```cpp
// Set a new active game mode
engine.setMode(std::make_unique<BandoriRenderer>());

// Typical flow triggered by START button:
ChartData chart = ChartLoader::load(songInfo.chartPath);
GameModeConfig config = buildConfig(songInfo);  // camera, trackCount, etc.
engine.setMode(std::make_unique<BandoriRenderer>());
engine.activeMode()->init(renderer, &config);
engine.activeMode()->loadChart(chart);
audioEngine.play(songInfo.audioPath);
engine.switchLayer(EditorLayer::GamePlay);
```

---

## Gesture Dispatch (in Engine)

Engine dispatches gesture events to the correct hit mode based on the active plugin type:

```cpp
void Engine::handleGesture(const GestureEvent& e) {
    if (dynamic_cast<ArcaeaRenderer*>(m_activeMode.get()))
        handleGestureArcaea(e);      // position-based hit
    else if (dynamic_cast<PhigrosRenderer*>(m_activeMode.get()))
        handleGesturePhigros(e);     // line-projection hit
    else
        handleGestureLaneBased(e);   // lane-based hit (Bandori, Cytus, Lanota)
}
```

---

## Plugin 1 — BandoriRenderer

**Game:** BanG Dream  
**Files:** `engine/src/game/modes/BandoriRenderer.h/.cpp`

### Visual Style
- Dynamic lane count vertical scrolling highway (lane count from `config.trackCount` / chart data, stored as `m_laneCount`)
- Perspective projection: notes scroll from far depth toward a hit zone at the bottom
- Hit zone rendered as a horizontal bar
- Lane spacing (`m_laneSpacing`) auto-calculated from camera FOV and screen aspect ratio to fill ~88% of screen width

### Camera
- Perspective settings (eye, target, FOV) read from `GameModeConfig`, user-adjustable in editor
- Defaults: FOV 52°, eye `{0, 1.8, 8.0}`, target `{0, 0, -20}`
- Two-camera pattern: perspective VP for `w2s()` projection, ortho for batchers
- **Critical:** eye_z must be ≥8 from the hit zone plane. Closer causes notes to clip before reaching the hit line. (Bug fixed: was eye_z=3.5, caused 93% screen position for hit zone)

### Note Scroll
- Notes spawn at Z=-55 (top of screen) and scroll toward Z=0 (hit zone)
- `onUpdate` uses songTime directly — no fmod wrapping / note looping
- Post-hit cull: `noteZ > 2.0f` removes notes that passed the hit zone
- `w2s(worldPos)` helper: projects 3D world position → 2D screen coords
- `pxSize(depth)` helper: computes perspective-correct pixel size at a given depth

### Rendering
- `QuadBatch` — note sprites (tap, hold body, flick arrows)
- `LineBatch` — lane divider lines, hit zone bar
- `ParticleSystem` — hit burst effect via `showJudgment` (judgment colored squares removed from `onRender`)
- Stores `Renderer* m_renderer` for particle system access

### Judgment Display
- `showJudgment` overridden: marks closest note as hit, emits colored particle burst
  - **Perfect** = green / 20 particles
  - **Good** = blue / 14 particles
  - **Bad** = red / 8 particles
  - **Miss** = nothing (no particles)
- JudgmentDisplay array size: 12 (supports up to 12 tracks)

### Cleanup
- `onShutdown` properly cleans up all state: notes, hitNotes, judgmentDisplays, renderer pointer

### Hit Mode
Lane-based: `HitDetector::checkHit(lane, songTime)`

---

## Other Renderers — Updated Signatures

ArcaeaRenderer, PhigrosRenderer, CytusRenderer, and LanotaRenderer all updated with the new `onInit` signature (accept `const GameModeConfig* config = nullptr`) but do not use the config parameter yet.

---

## Plugin 2 — PhigrosRenderer

**Game:** Phigros  
**Files:** `engine/src/game/modes/PhigrosRenderer.h/.cpp`

### Visual Style
- Multiple judgment lines that rotate and move independently
- Notes approach perpendicular to their parent line
- No fixed lanes — notes are positioned relative to their line

### Camera
- Orthographic, centered: `makeOrtho(-hw, hw, hh, -hh)` (origin at center)

### SceneGraph Integration
Uses `SceneGraph` (System 3) for the line → note hierarchy:

```cpp
// Each judgment line is a scene node
NodeID lineNode = m_scene.createNode();
// Notes are children of their line
NodeID noteNode = m_scene.createNode(lineNode);
note.localTransform.position = {posOnLine, 0, 0};

// Each frame: update line rotation from chart events, then propagate
m_scene.get(lineNode)->localTransform.rotation = glm::quat(angle);
m_scene.markDirty(lineNode);
m_scene.update();

// Read final world position for rendering
glm::mat4 world = m_scene.worldMatrix(noteNode);
glm::vec2 screenPos = {world[3][0], world[3][1]};
```

### Hit Mode
Line projection: `HitDetector::checkHitPhigros(screenPos, lineOrigin, lineRotation, songTime)`

---

## Plugin 3 — ArcaeaRenderer

**Game:** Arcaea  
**Files:** `engine/src/game/modes/ArcaeaRenderer.h/.cpp`

### Visual Style
- 3D perspective with floor notes and mid-air arcs
- Ground notes on a flat plane (y ≈ -2), sky notes above
- Arc notes: curved ribbon paths the player must track

### Camera
- Perspective: FOV 45°, eye `{0, 3, 10}`, target `{0, 0, 0}`
- Ground plane at y=-2 for depth reference

### Note Types
- **Tap** — ground floor tap (lane 1–4)
- **Hold** — ground hold (lane 1–4)
- **Arc** — curved ribbon from `(startX, startY)` to `(endX, endY)` over `duration`
  - Pre-tessellated at chart load: 32-segment ribbon mesh per arc
  - Scrolled via model matrix Z translation each frame
  - Blue (color=0) or red (color=1) arc variants
- **SkyNote** — air tap note above the arc

### Rendering
- `MeshRenderer` — arc ribbon geometry (static mesh, Z-scroll via model matrix)
- `QuadBatch` — floor tap/hold notes, sky notes

### Hit Mode
- Ground notes: position-based, `HitDetector::checkHitPosition(screenPos, screenSize, songTime)`
- Sky notes: `HitDetector::checkHit` with lenient `judgeSkyNote()` timing
- Arc: `judgeArc(avgTrackingError, completionRatio)` — min 85% completion required

---

## Plugin 4 — CytusRenderer

**Game:** Cytus  
**Files:** `engine/src/game/modes/CytusRenderer.h/.cpp`

### Visual Style
- Static notes at fixed grid positions
- A horizontal scan line sweeps up and down across the screen
- Notes activate when the scan line reaches them

### Camera
- Orthographic top-left origin: `makeOrtho(0, w, h, 0)`

### Scan Line Logic
```
Page duration: 4 seconds
Even pages: scan line moves bottom → top
Odd pages: scan line moves top → bottom
Note visibility window: timeDiff ∈ (-0.3, 1.0) seconds
```

### Rendering
- `LineBatch` — the horizontal scan line
- `QuadBatch` — tap and hold notes

### Hit Mode
Lane-based: `HitDetector::checkHit(lane, songTime)`

---

## Plugin 5 — LanotaRenderer

**Game:** Lanota  
**Files:** `engine/src/game/modes/LanotaRenderer.h/.cpp`

### Visual Style
- Concentric ring tunnel perspective
- Notes emerge from the screen center (far depth) and grow toward ring size at Z=0
- Rings rotate independently; disk center moves across the screen per chart events

### Camera
- Perspective: FOV 60°, eye `{diskX, diskY, 4}`, target `{diskX, diskY, 0}`
- Eye and target are both offset by `m_diskCenter` — shifting them moves the entire disk on-screen without touching any note positions
- Two-camera pattern (same as Bandori): perspective VP + ortho for batchers
- `rebuildPerspVP()` rebuilds `m_perspVP` whenever `m_diskCenter` changes or the window resizes

### Note Scroll
- Notes start at far Z (small, near center) and expand toward the player
- `buildRingPolyline(radius, segments)` projects 3D circle with perspective for each ring

### Rendering
- `QuadBatch` — notes at their projected positions
- `LineBatch` — concentric ring outlines via `drawPolyline`

### Disk Rotation (chart-driven)

Each `Ring` holds a `std::vector<RotationEvent>` (sorted by time):

```cpp
struct RotationEvent { double time; float targetAngle; };
```

`getCurrentRotation(songTime, events, fallbackAngle)` — static helper:
- Before first event → clamp to `events[0].targetAngle`
- Between two events → linear interpolation
- After last event → hold `events.back().targetAngle`
- Empty list → return `fallbackAngle` (caller accumulates constant `rotationSpeed * dt`)

In `onUpdate`, `ring.currentAngle` is set from the event curve (or accumulated by constant speed if no events). In `onRender`, `angle = rd->angle + ring.currentAngle` rotates all notes on the ring — no model matrix needed, the trig call is the transform.

### Dynamic Disk Center (chart-driven)

`m_moveEvents` is a `std::vector<DiskMoveEvent>` (sorted by time):

```cpp
struct DiskMoveEvent {
    double     time;
    glm::vec2  target;   // world-space XY the disk center should be at
    EasingType easing;   // applies for the segment from this event to the next
};
```

`getDiskCenter(songTime, events)` interpolates `target` between adjacent keyframes using the source event's easing. Result stored in `m_diskCenter`; triggers `rebuildPerspVP()` when changed.

### Easing Functions

```cpp
enum class EasingType { Linear, SineInOut, QuadInOut, CubicInOut };
```

`applyEasing(t, easing)` maps `t ∈ [0,1] → [0,1]`:

| Type | Formula | Feel |
|---|---|---|
| `Linear` | `t` | Constant speed |
| `SineInOut` | `-(cos(πt) - 1) / 2` | Gentle S-curve |
| `QuadInOut` | `2t²` / `1-(−2t+2)²/2` | Moderate ease |
| `CubicInOut` | `4t³` / `1-(−2t+2)³/2` | Snappy, long midpoint |

### View Matrix vs Screen-Space Offset

The disk center is implemented as a **View Matrix shift** (move the camera), not a screen-space offset added to each note. This is correct because all elements (rings + notes) already pass through `m_perspVP` via `w2s()`. Shifting the camera eye/target moves everything together — adding a screen offset to every draw call would lose perspective-correct depth scaling.

### Hit Mode
Lane-based: `HitDetector::checkHit(lane, songTime)`

---

## Sandbox Project — `Projects/BandoriSandbox/`

A standalone project used for early BanG Dream prototyping and isolated testing.  
Has its own `BandoriRenderer.h/.cpp` and `main.cpp`, independent of the engine.  
Not part of the engine library — exists for reference and experimentation.

---

## Future: Gameplay Integration

The complete flow for pressing START (not yet implemented):

```
1. MusicSelectionEditor: user clicks START
2. Read SongInfo: chartPath, audioPath, gameMode string
3. ChartData chart = ChartLoader::load(chartPath)
4. Build GameModeConfig from SongInfo (camera, trackCount, etc.)
5. Instantiate plugin:
     if (gameMode == "bandori")  → make_unique<BandoriRenderer>()
     if (gameMode == "phigros")  → make_unique<PhigrosRenderer>()
     if (gameMode == "arcaea")   → make_unique<ArcaeaRenderer>()
     if (gameMode == "cytus")    → make_unique<CytusRenderer>()
     if (gameMode == "lanota")   → make_unique<LanotaRenderer>()
6. engine.setMode(std::move(mode))
7. activeMode->init(renderer, &config)
8. activeMode->loadChart(chart)
9. audioEngine.play(audioPath)
10. engine.switchLayer(EditorLayer::GamePlay)
```
