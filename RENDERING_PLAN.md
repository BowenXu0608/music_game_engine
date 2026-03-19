# Music Game Engine — Rendering System Plan

## Overview

Multi-mode music game engine supporting **BanG Dream**, **Phigros**, **Arcaea**, **Cytus**, and **Lanota**.
Stack: **C++20 + Vulkan**, Windows.
All rendering reduces to transformed primitives (quads, lines, meshes). Game modes are plugins — the engine is mode-agnostic.

---

## Project Structure

```
Music_game/
├── CMakeLists.txt
├── RENDERING_PLAN.md               ← this file
├── src/
│   ├── core/
│   │   ├── ECS.h                   # EntityID, ComponentPool<T>, Registry
│   │   ├── SceneNode.h / .cpp      # Parent-child transform hierarchy
│   │   └── Transform.h             # TRS + cached world matrix
│   ├── renderer/
│   │   ├── vulkan/
│   │   │   ├── VulkanContext.h/.cpp     # Instance, device, queues, surface
│   │   │   ├── Swapchain.h/.cpp         # Swapchain, image views, framebuffers
│   │   │   ├── RenderPass.h/.cpp
│   │   │   ├── Pipeline.h/.cpp          # VkPipeline builder
│   │   │   ├── DescriptorManager.h/.cpp
│   │   │   ├── BufferManager.h/.cpp     # VMA-backed buffers
│   │   │   ├── TextureManager.h/.cpp
│   │   │   ├── CommandManager.h/.cpp
│   │   │   └── SyncObjects.h/.cpp       # Frames-in-flight, semaphores, fences
│   │   ├── RenderTypes.h           # Vertex types, RenderLayer enum
│   │   ├── Camera.h/.cpp           # Unified ortho + perspective camera
│   │   ├── QuadBatch.h/.cpp        # Batched textured quads
│   │   ├── LineBatch.h/.cpp        # Line segments (CPU-expanded to quads)
│   │   ├── MeshRenderer.h/.cpp     # 3D mesh draw (Arcaea arc ribbons)
│   │   ├── ParticleSystem.h/.cpp
│   │   ├── PostProcess.h/.cpp      # Bloom (compute shader mip chain)
│   │   ├── RenderGraph.h/.cpp      # Pass ordering: geometry→bloom→UI→present
│   │   └── Renderer.h/.cpp         # Top-level: owns all batchers + passes
│   ├── game/
│   │   ├── modes/
│   │   │   ├── GameModeRenderer.h       # Abstract plugin interface
│   │   │   ├── BandoriRenderer.h/.cpp
│   │   │   ├── PhigrosRenderer.h/.cpp
│   │   │   ├── ArcaeaRenderer.h/.cpp
│   │   │   ├── CytusRenderer.h/.cpp
│   │   │   └── LanotaRenderer.h/.cpp
│   │   └── chart/
│   │       ├── ChartTypes.h        # NoteEvent, ChartData (unified format)
│   │       └── ChartLoader.h/.cpp  # Parses mode-specific formats → ChartData
│   └── engine/
│       ├── Engine.h/.cpp           # Main loop, owns active GameModeRenderer
│       ├── AudioEngine.h/.cpp
│       ├── InputManager.h/.cpp
│       └── GameClock.h/.cpp        # DSP clock sync
└── shaders/
    ├── quad.vert / quad.frag
    ├── line.vert / line.frag
    ├── mesh.vert / mesh.frag       # Arcaea arcs
    ├── bloom_downsample.comp
    ├── bloom_upsample.comp
    └── composite.vert / composite.frag
```

---

## Third-Party Libraries

| Library | Purpose |
|---|---|
| GLFW 3.x | Window + Vulkan surface |
| GLM | Math (vec2/vec4/mat4/quat) |
| VMA (Vulkan Memory Allocator) | GPU memory management |
| stb_image | Texture loading (single header) |
| glslc (Vulkan SDK) | GLSL → SPIR-V compilation |
| FMOD or miniaudio | Audio playback + DSP clock |

---

## Core Architecture

### Design: Hybrid ECS + Scene Graph

- **ECS** for game logic and data (cache-friendly note iteration)
- **Scene graph** only for spatial hierarchy (parent-child transforms)

This makes Phigros (notes parented to rotating judgment lines) and Lanota (notes parented to rotating rings) trivial — no manual trigonometry needed.

### SceneNode

```cpp
class SceneNode {
    Transform  localTransform;       // position, rotation, scale
    glm::mat4  worldMatrix() const;  // lazy cached, walks parent chain
    void       markDirty();          // invalidates cache + children
    void       setParent(ID);
};
```

### Camera

```cpp
class Camera {
    static Camera makeOrtho(float l, float r, float b, float t);          // 2D modes
    static Camera makePerspective(float fovY, float aspect, float n, float f); // Arcaea
    glm::mat4 viewProjection() const;
};
```

### Rendering Primitives

| Class | Use |
|---|---|
| `QuadBatch` | All textured quads — notes, lanes, UI. Single `vkCmdDrawIndexed` per texture group. |
| `LineBatch` | Line segments CPU-expanded to quads. Used by Cytus scan line, Phigros judgment lines, Lanota ring outlines. |
| `MeshRenderer` | Per-mesh draw with depth test. Used by Arcaea arc ribbon geometry. |
| `ParticleSystem` | Ring buffer, 2048 particles, additive blend pipeline. |

### Vulkan Frame Strategy

- `MAX_FRAMES_IN_FLIGHT = 3`, mailbox present mode
- Per-frame persistently mapped dynamic vertex buffer (VMA `CPU_TO_GPU`)
- Static index buffer shared across all frames
- **Set 0**: per-frame UBO (VP matrix + time)
- **Set 1**: per-texture combined image sampler
- Push constants (96 bytes): `vec4 tint` + `vec4 uvTransform`
- Pipelines: `QUAD` (alpha blend), `PARTICLE` (additive), `LINE`, `MESH` (depth test on)
- Dynamic viewport/scissor — no pipeline recreation on resize

---

## Game Mode Plugin Interface

```cpp
// src/game/modes/GameModeRenderer.h
class GameModeRenderer {
public:
    virtual void onInit(Renderer&, const ChartData&) = 0;
    virtual void onResize(uint32_t w, uint32_t h) = 0;
    virtual void onUpdate(float dt, double songTime) = 0;
    virtual void onRender(Renderer&) = 0;
    virtual void onShutdown() = 0;
    virtual const Camera& getCamera() const = 0;
};
```

`Engine` holds `std::unique_ptr<GameModeRenderer> activeMode`. Switching modes = `activeMode = ModeFactory::create(type)`.

---

## Chart Data Format

All five games map to a unified `ChartData`. Mode-specific note data lives in a `std::variant`.

```cpp
// src/game/chart/ChartTypes.h

enum class NoteType { Tap, Hold, Flick, Drag, Arc, ArcTap, Ring, Slide };

struct TapData         { float laneX; };
struct HoldData        { float laneX; float duration; };
struct ArcData         { glm::vec2 startPos, endPos; float duration,
                         curveXEase, curveYEase; int color; bool isVoid; };
struct PhigrosNoteData { float posOnLine; NoteType subType; float duration; };
struct LanotaRingData  { float angle; int ringIndex; };

struct NoteEvent {
    double   time;    // seconds from song start
    NoteType type;
    uint32_t id;
    std::variant<TapData, HoldData, ArcData,
                 PhigrosNoteData, LanotaRingData> data;
};

struct JudgmentLineEvent {  // Phigros only
    double    time;
    glm::vec2 position;     // normalized screen coords
    float     rotation;     // radians
    float     speed;
    std::vector<NoteEvent> attachedNotes;
};

struct ChartData {
    std::string title, artist;
    float offset;   // audio sync offset ms
    std::vector<TimingPoint>       timingPoints;
    std::vector<NoteEvent>         notes;
    std::vector<JudgmentLineEvent> judgmentLines;  // Phigros only
};
```

---

## Mode-Specific Rendering

### BanG Dream (Bandori)
- Camera: 2D ortho
- 7 vertical lanes, notes fall straight down
- Note Y = `hitZoneY - (note.time - songTime) * scrollSpeed`
- No scene graph needed — direct coordinate math
- Primitives: `QuadBatch` (notes), `LineBatch` (lane dividers)

### Cytus
- Camera: 2D ortho
- Notes are **stationary** quads at fixed positions
- Scan line sweeps left↔right: `x = (songTime % pageDuration) / pageDuration * screenW`, direction flips each page
- Hit detection: `abs(note.x - scanLineX) < threshold`
- Primitives: `QuadBatch` (notes), `LineBatch` (scan line)

### Phigros
- Camera: 2D ortho (centered at origin)
- Multiple judgment lines with keyframe-animated position/rotation/speed
- Notes are **children** of their line's `SceneNode`
- `note.localTransform.position.y` = approach distance (counts down to 0)
- World matrix = line rotation + position applied automatically
- Primitives: `LineBatch` (judgment lines), `QuadBatch` (notes)

### Arcaea
- Camera: 3D perspective (45° FoV, positioned above/behind playfield)
- Sky/ground split — two world-space quad planes
- Arc notes tessellated into ribbon meshes (32 segments)
- Arc Z = `(noteTime - songTime) * scrollSpeed` — slides toward camera
- Tap notes = quads at `y = groundY`
- Primitives: `QuadBatch` (tap notes, background), `MeshRenderer` (arc ribbons)

### Lanota
- Camera: 2D ortho (centered)
- Concentric rings as `SceneNode`s rotating at chart-defined speeds
- Notes are **children** at fixed local angle `(radius, 0, 0)`
- Ring rotation carries all child notes automatically
- Rings drawn as `LineBatch` polyline circles (~64 segments)
- Primitives: `LineBatch` (rings), `QuadBatch` (notes)

---

## Timing Strategy

| Rule | Detail |
|---|---|
| Ground truth | Audio DSP clock — `FMOD::Channel::getPosition` or miniaudio frame counter |
| Delta time | `glfwGetTime()` for animation/particles only |
| Note positions | Time-derived → correct at any refresh rate automatically |
| Input offset | Configurable `inputOffsetMs`: `adjustedHit = rawInputTime + offsetMs / 1000.f` |

---

## Implementation Order

| Step | Task | Validates |
|---|---|---|
| 1 | VulkanContext + GLFW | Clear screen, zero validation errors |
| 2 | Swapchain + RenderPass + Framebuffers | Clear to color |
| 3 | BufferManager (VMA) | No crashes |
| 4 | Pipeline + hardcoded triangle | First triangle on screen |
| 5 | QuadBatch + index buffer + dynamic VBO | Colored quads |
| 6 | DescriptorManager + FrameUBO + Camera | Correct ortho projection |
| 7 | TextureManager | Textured quads |
| 8 | SyncObjects (frames in flight) | Smooth rendering, no validation errors |
| 9 | Swapchain recreation | Resize without crash |
| 10 | SceneNode + Transform hierarchy | Parent-child transforms correct |
| 11 | LineBatch | Lines render at correct screen positions |
| 12 | BandoriRenderer | Notes scroll at correct BPM speed |
| 13 | CytusRenderer | Scan line position matches audio time |
| 14 | PhigrosRenderer | Notes rotate with judgment line |
| 15 | LanotaRenderer | Notes orbit with rotating rings |
| 16 | MeshRenderer + ArcaeaRenderer | Arcs cross judgment line at correct time |
| 17 | ParticleSystem + hit effects | Particles emit on hit |
| 18 | PostProcess bloom | Glow effects on notes/lines — see bloom implementation notes below |
| 19 | Audio sync | Swap wall clock for DSP clock |

---

## Verification Checklist

- [ ] Step 5: 8192 quads in one `vkCmdDrawIndexed` — confirm in RenderDoc
- [ ] Step 12: Bandori notes scroll at correct BPM, no stutter at 60fps
- [ ] Step 13: Cytus scan line position matches audio time exactly
- [ ] Step 14: Phigros notes rotate correctly with their judgment line
- [ ] Step 16: Arcaea arcs slide toward camera and cross judgment line on beat
- [ ] All modes: note positions stable when audio is paused/seeked
- [ ] Stress test: 500+ simultaneous notes, single draw call per flush in RenderDoc

---

## Step 18 — Bloom Post-Process Implementation

### Overview
Scene renders to an offscreen RGBA16F image → bloom compute mip chain → composite fullscreen pass onto swapchain.

### Files to create
- `src/renderer/PostProcess.cpp`

### Files to modify
- `src/renderer/Renderer.h` — add `PostProcess m_postProcess` member
- `src/renderer/Renderer.cpp` — wire PostProcess into init/shutdown/beginFrame/endFrame/onResize
- `shaders/composite.vert` — replace vertex input with gl_VertexIndex fullscreen triangle (no VBO needed)

### Memory strategy
- Scene image: `VK_FORMAT_R16G16B16A16_SFLOAT`, usage = `COLOR_ATTACHMENT | SAMPLED | STORAGE`
- Bloom mips (5): same format, each half the previous, usage = `STORAGE | SAMPLED`
- All images use raw `vkAllocateMemory` (consistent with PostProcess.h using `VkDeviceMemory`)

### Descriptor layouts (owned by PostProcess)
- `m_computeSetLayout`: binding0 = sampler2D (src), binding1 = storage image (dst)
- `m_compositeSetLayout`: binding0 = sampler2D (scene), binding1 = sampler2D (bloom mip0)

### Pipelines
- `m_downsamplePipeline` from `bloom_downsample.comp.spv`
- `m_upsamplePipeline` from `bloom_upsample.comp.spv`
- Both share `m_computeLayout` (push = `vec2 srcTexelSize`)
- `m_compositePipeline` from `composite.vert.spv` / `composite.frag.spv` (push = `float bloomStrength`)

### render() per-frame sequence
1. Downsample loop mip 0→1→2→3→4: barrier src→SHADER_READ, dst→GENERAL, dispatch, repeat
2. Upsample loop mip 4→3→2→1→0: barrier src→SHADER_READ, dst→GENERAL, dispatch (accumulate), repeat
3. Barrier mip0 GENERAL→SHADER_READ_ONLY

### Renderer frame loop changes
- `beginFrame()`: begin render pass on `postProcess.sceneFramebuffer()` instead of swapchain
- `endFrame()`:
  1. End scene render pass
  2. Barrier scene image COLOR_ATTACHMENT→SHADER_READ
  3. `m_postProcess.render(cmd, ctx, sceneView, sceneSampler)`
  4. Begin swapchain render pass
  5. Bind composite pipeline + set, push bloomStrength, `vkCmdDraw(3,1,0,0)`
  6. End swapchain render pass → submit → present

### composite.vert change
```glsl
// No vertex input — fullscreen triangle via gl_VertexIndex
vec2 pos[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
vec2 uvs[3] = vec2[](vec2(0,0),   vec2(2,0),  vec2(0,2));
gl_Position = vec4(pos[gl_VertexIndex], 0, 1);
fragUV      = uvs[gl_VertexIndex];
```

---

## Critical Files

| File | Why critical |
|---|---|
| `src/core/SceneNode.h` | Transform hierarchy — Phigros and Lanota depend on this |
| `src/game/modes/GameModeRenderer.h` | Plugin interface — boundary between engine and game logic |
| `src/game/chart/ChartTypes.h` | Unified note format — all loaders and renderers depend on this |
| `src/renderer/Camera.h` | Ortho/perspective abstraction shared by all modes |
| `src/renderer/LineBatch.h` | Needed by Cytus, Phigros, Lanota |
| `src/renderer/MeshRenderer.h` | Needed exclusively by Arcaea arc notes |
| `src/renderer/vulkan/SyncObjects.h` | Frames-in-flight — owns per-frame VBOs, UBOs, semaphores |
