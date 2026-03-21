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
│   │   ├── SceneNode.h / .cpp      # Parent-child transform hierarchy (header-only; .cpp is empty)
│   │   └── Transform.h             # TRS + quat rotation + toMatrix()
│   ├── renderer/
│   │   ├── vulkan/
│   │   │   ├── VulkanContext.h/.cpp     # Instance, device, queues, surface
│   │   │   ├── Swapchain.h/.cpp         # Swapchain, image views, framebuffers
│   │   │   ├── RenderPass.h/.cpp        # Single-subpass color render pass
│   │   │   ├── Pipeline.h/.cpp          # VkPipeline builder (PipelineConfig)
│   │   │   ├── DescriptorManager.h/.cpp # Pool + set0 UBO + set1 sampler layouts
│   │   │   ├── BufferManager.h/.cpp     # VMA-backed buffers (VMA_IMPLEMENTATION here)
│   │   │   ├── TextureManager.h/.cpp    # stb_image load + VMA image alloc (STB_IMAGE_IMPLEMENTATION here)
│   │   │   ├── CommandManager.h/.cpp    # Per-frame command buffer alloc/begin/end
│   │   │   └── SyncObjects.h/.cpp       # MAX_FRAMES_IN_FLIGHT=3, semaphores, fences
│   │   ├── RenderTypes.h           # QuadVertex, LineVertex, MeshVertex, FrameUBO, QuadPushConstants, DrawCall
│   │   ├── Camera.h                # Unified ortho + perspective, header-only
│   │   ├── QuadBatch.h/.cpp        # Batched textured quads (MAX_QUADS=8192)
│   │   ├── LineBatch.h/.cpp        # Lines CPU-expanded to quad triangles (MAX_LINES=4096)
│   │   ├── MeshRenderer.h/.cpp     # Per-mesh draw with depth test
│   │   ├── ParticleSystem.h/.cpp   # Ring buffer 2048 particles, additive blend
│   │   ├── PostProcess.h/.cpp      # Bloom compute mip chain + composite pass
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
│       ├── AudioEngine.h/.cpp      # miniaudio wrapper (MINIAUDIO_IMPLEMENTATION here)
│       ├── GameClock.h             # Wall clock + DSP override, header-only
│       └── miniaudio.h             # Single-header miniaudio bundled in engine/
└── shaders/
    ├── quad.vert / quad.frag
    ├── line.vert / line.frag
    ├── mesh.vert / mesh.frag
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
| VMA (Vulkan Memory Allocator) | GPU memory management (`third_party/vma/`) |
| stb_image | Texture loading (`third_party/stb/`) |
| glslc (Vulkan SDK) | GLSL → SPIR-V compilation |
| miniaudio | Audio playback + DSP clock (bundled at `src/engine/miniaudio.h`) |

---

## Core Architecture

### Design: Hybrid ECS + Scene Graph

- **ECS** (`ECS.h`) for game logic and data — `Registry` + `ComponentPool<T>` (dense storage, sparse map)
- **Scene graph** (`SceneNode.h`) only for spatial hierarchy — used by Phigros only

### SceneNode / SceneGraph

Both live in `SceneNode.h` (fully header-only). `SceneNode.cpp` is an intentionally empty translation unit.

```cpp
// SceneGraph owns all nodes. update() resolves world matrices top-down.
SceneGraph m_scene;
NodeID line = m_scene.createNode();
NodeID note = m_scene.createNode(line);   // child of line
m_scene.get(note)->localTransform.position = {x, y, 0.f};
m_scene.markDirty(line);                  // propagates to children
m_scene.update();
glm::mat4 world = m_scene.worldMatrix(note);
```

`Transform`: position(vec3) + rotation(quat) + scale(vec3). `toMatrix()` = T\*R\*S.
`setRotationZ(float radians)` and `getRotationZ()` are 2D helpers.

### Camera

Header-only in `Camera.h`.

```cpp
Camera::makeOrtho(0, w, h, 0)       // top-left origin, Y down (all 2D modes)
Camera::makeOrtho(-hw, hw, hh, -hh) // centered, Y down (Phigros, Lanota)
Camera::makePerspective(45, aspect, 0.1, 200)  // Arcaea
camera.lookAt({0, 3, 10}, {0, 0, 0});
glm::mat4 vp = camera.viewProjection();  // proj * view
```

`makePerspective` flips `proj[1][1] *= -1` for Vulkan NDC.

### Rendering Primitives

| Class | Flush signature | Notes |
|---|---|---|
| `QuadBatch` | `flush(cmd, ctx, descMgr)` | tracks `m_currentFrame` internally |
| `LineBatch` | `flush(cmd, frameIndex)` | explicit frame index |
| `MeshRenderer` | `flush(cmd, frameIndex)` | explicit frame index |
| `ParticleSystem` | `flush(cmd, frameIndex, whiteTexSet)` | explicit frame index + tex set |

### Vulkan Frame Strategy

- `MAX_FRAMES_IN_FLIGHT = 3`, V-Sync ON by default (FIFO); MAILBOX when vsync=false
- Per-frame persistently mapped dynamic VBOs (VMA `CPU_TO_GPU` + `MAPPED_BIT`)
- Static index buffer shared across all frames (QuadBatch only; LineBatch/ParticleSystem emit triangles directly)
- **Set 0**: per-frame `FrameUBO` (viewProj mat4 + time float + 12 bytes padding = 80 bytes)
- **Set 1**: per-texture combined image sampler
- Push constants: `QuadPushConstants` = tint(vec4) + uvTransform(vec4) + model(mat4) = 96 bytes
- MeshRenderer push constants: model(mat4) + tint(vec4) = 80 bytes
- Pipelines: QUAD (alpha blend), PARTICLE (additive blend, same shaders as QUAD), LINE (alpha blend, no index buffer), MESH (depth test+write on, alpha blend)
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
    virtual void onShutdown(Renderer&) = 0;
    virtual const Camera& getCamera() const = 0;
};
```

`Engine` holds `std::unique_ptr<GameModeRenderer> m_activeMode`. `setMode()` destroys old mode and creates new one.

---

## Chart Data Format

All five games map to a unified `ChartData`. Mode-specific note data lives in a `std::variant`.

```cpp
// src/game/chart/ChartTypes.h

enum class NoteType { Tap, Hold, Flick, Drag, Arc, ArcTap, Ring, Slide };

struct TapData         { float laneX; };
struct HoldData        { float laneX; float duration; };
struct FlickData       { float laneX; int direction; };  // direction: -1=left, 1=right
struct ArcData         { glm::vec2 startPos, endPos; float duration,
                         curveXEase, curveYEase; int color; bool isVoid; };
struct PhigrosNoteData { float posOnLine; NoteType subType; float duration; };
struct LanotaRingData  { float angle; int ringIndex; };

struct NoteEvent {
    double   time;    // seconds from song start
    NoteType type;
    uint32_t id;
    std::variant<TapData, HoldData, FlickData,
                 ArcData, PhigrosNoteData, LanotaRingData> data;
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
    float offset;   // audio sync offset seconds
    std::vector<TimingPoint>       timingPoints;
    std::vector<NoteEvent>         notes;
    std::vector<JudgmentLineEvent> judgmentLines;  // Phigros only
};
```

---

## Mode-Specific Rendering

### BanG Dream (Bandori)
- **Two cameras**: `m_perspVP` (perspective, 52° FOV) for 3D→screen projection; `m_camera` (ortho 0..w, 0..h) for batchers
- Camera eye `{0, 1.8, 3.5}`, target `{0, 0, -20}` — slightly elevated, looking down the highway
- 7 lanes in world space: `worldX = (laneX - 3) * 0.5f`, lane dividers from `Z=0` to `Z=-55`
- Note position: `noteZ = -(note.time - songTime) * 14` — Z=0 = hit zone (bottom of screen), Z=-55 = vanishing (top)
- `w2s()` projects world `{x, 0, z}` → screen; `pxSize()` gives perspective-correct pixel width
- Colors: tap=`{1,0.8,0.2}`, hold=`{0.2,0.8,1}`, flick=`{1,0.3,0.3}`
- Hit burst: `|timeDiff| < 0.05s` → `emitBurst(screenPos, color, 16, 250f, 10f, 0.6f)`
- Primitives: `QuadBatch` (notes), `LineBatch` (lane dividers converging to VP + hit zone line)

### Cytus
- Camera: `makeOrtho(0, w, h, 0)`
- Notes stationary at fixed (x, y) grid positions
- Scan line: `page = int(songTime/4s)`, even page bottom→top `(1-t)*h`, odd top→bottom `t*h`
- Visibility: `dt ∈ (-0.3, 1.0s)`; hold drawn as vertical LineBatch connector to scan line
- Primitives: `QuadBatch` (notes: outer dark ring + inner fill), `LineBatch` (scan line glow+core, hold connectors)

### Phigros
- Camera: `makeOrtho(-hw, hw, hh, -hh)` — centered at origin
- Each `JudgmentLineEvent` → `LineState` with one `SceneNode`
- Notes are children: `localTransform.position = {posOnLine, (note.time - songTime)*speed, 0}`
- `m_scene.update()` propagates line rotation to note world positions
- `onRender`: reads `worldMatrix(noteNode)[3][0/1]` for world pos, draws note as rotated quad at `ls.rotation`
- Line: glow (12px, 15% alpha) + core (3px, 90% alpha) via two `drawLine` calls
- Primitives: `LineBatch` (judgment lines), `QuadBatch` (notes)

### Arcaea
- Camera: `makePerspective(45°, aspect, 0.1, 200)`, `lookAt({0,3,10}, {0,0,0})`
- Arc ribbon: 32 segments, `z = -t * duration * 8`, width 0.35, blue or pink per `arc.color`
- Arc render: `translate(0, 0, -(startTime - songTime)*8)` model matrix slides arc toward camera
- Tap notes: `translate(wx, 0, -(note.time - songTime)*8)` with reused `m_tapMesh`
- Ground plane: large XZ quad at `y = -2`, 60 units deep
- Primitives: `MeshRenderer` (arcs, ground, tap notes) only — no QuadBatch or LineBatch

### Lanota
- **Two cameras**: `m_perspVP` (perspective, 60° FOV) for 3D→screen projection; `m_camera` (ortho 0..w, 0..h) for batchers
- Camera eye `{0, 0, 4}`, target `{0, 0, 0}` — straight down the tunnel axis
- Concentric rings at Z=0 hit-plane: `radius = 1.8 + idx * 0.6` world units, `rotationSpeed = 0.4 + idx * 0.15` rad/s
- No SceneGraph — ring angle accumulates as `currentAngle += speed * dt`; note angle = `rd->angle + ring.currentAngle`
- Note position: `{cos(angle)*radius, sin(angle)*radius, -timeDiff * 14}` — far negative Z = tiny dot at screen centre; Z=0 = full size on ring
- `buildRingPolyline()` projects each of 64 circle points at Z=0 via `w2s()` — perspective foreshortening included
- Note pixel size: `NOTE_WORLD_R * m_proj11y * sh * 0.5 / clip.w` — shrinks naturally with distance
- Primitives: `LineBatch` (rings), `QuadBatch` (notes: 1.3× dark halo + 1× yellow fill)

---

## Timing Strategy

| Rule | Detail |
|---|---|
| Ground truth | `AudioEngine::positionSeconds()` → `ma_sound_get_cursor_in_seconds()` |
| Fallback | If no audio playing (`positionSeconds() < 0`), `songTime += dt` each frame |
| DSP override | `Engine::update()` calls `m_clock.setSongTime(dspPos)` when audio is running |
| Delta time | `GameClock::tick()` always returns wall dt — used for particles/animation only |
| Song time | `GameClock::songTime()` — only note positions depend on this |
| Input offset | Configurable `ChartData::offset` (seconds) |

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
| 12 | BandoriRenderer | Notes scroll at correct speed, hit burst fires |
| 13 | CytusRenderer | Scan line position matches song time |
| 14 | PhigrosRenderer | Notes rotate with judgment line |
| 15 | LanotaRenderer | Notes orbit with rotating rings |
| 16 | MeshRenderer + ArcaeaRenderer | Arcs cross judgment line at correct time |
| 17 | ParticleSystem + hit effects | Particles emit on hit, additive blend |
| 18 | PostProcess bloom | Glow effects on notes/lines |
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

## Step 1 — VulkanContext + GLFW

### Overview
Create the Vulkan instance, debug messenger, surface, physical device, logical device, and command pool. GLFW window creation lives in `Engine::init()`.

### Files
- `src/renderer/vulkan/VulkanContext.h/.cpp`
- `src/engine/Engine.h/.cpp`

### Engine::init() sequence
1. `glfwInit()`, `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`
2. `glfwCreateWindow(width, height, title, nullptr, nullptr)`
3. `glfwSetWindowUserPointer(window, this)`, set resize + key callbacks
4. `m_renderer.init(window, shaderDir, validationEnabled)`

### VulkanContext::init() sequence
1. `createInstance(enableValidation)` — API version `VK_API_VERSION_1_2`, GLFW extensions, optionally `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` and `VK_LAYER_KHRONOS_validation`
2. `setupDebugMessenger()` — WARNING+ERROR severity, all message types, `debugCallback` prints to stderr
3. `createSurface(window)` — `glfwCreateWindowSurface`
4. `pickPhysicalDevice()` — first device passing `isDeviceSuitable`: complete queue families + `VK_KHR_SWAPCHAIN_EXTENSION_NAME` + non-empty surface formats+modes
5. `createLogicalDevice(enableValidation)` — enables `samplerAnisotropy`, creates one graphics queue + one present queue (same family → exclusive, different → concurrent)
6. `createCommandPool()` — `RESET_COMMAND_BUFFER_BIT`, graphics queue family

### Key callback
```cpp
// Key ESC → glfwSetWindowShouldClose
void Engine::keyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}
```

### Validation control
`ENABLE_VALIDATION_LAYERS` is a compile-time define (CMake `$<$<CONFIG:Debug>:ENABLE_VALIDATION_LAYERS>`). Engine passes `true` in Debug, `false` in Release.

---

## Step 2 — Swapchain + RenderPass + Framebuffers

### Overview
Create the swapchain (images + views), the swapchain render pass (used for composite-to-screen), and per-image framebuffers. The scene render pass (used by batchers) is created later by `PostProcess`.

### Files
- `src/renderer/vulkan/Swapchain.h/.cpp`
- `src/renderer/vulkan/RenderPass.h/.cpp`

### Swapchain creation
- Format: prefers `VK_FORMAT_B8G8R8A8_SRGB / SRGB_NONLINEAR`; falls back to `formats[0]`
- Present mode: controlled by `bool vsync` (default `true`):
  - V-Sync ON (`vsync=true`) → `FIFO` — caps to display refresh rate, always available
  - V-Sync OFF (`vsync=false`) → prefers `MAILBOX` (low-latency triple-buffer), falls back to `FIFO`
- `m_vsync` stored on `Swapchain`; passed through `recreate()` on resize so mode persists
- Image count: `caps.minImageCount + 1`, clamped to `maxImageCount`
- Sharing mode: `CONCURRENT` if graphics ≠ present family, `EXCLUSIVE` otherwise
- `createFramebuffers()` is a separate call — called from `Renderer::init()` and `Renderer::onResize()` after every recreate

### RenderPass (swapchain)
- Single color attachment: `loadOp=CLEAR`, `storeOp=STORE`, `finalLayout=PRESENT_SRC_KHR`
- One subpass dependency: `EXTERNAL→0`, `COLOR_ATTACHMENT_OUTPUT → COLOR_ATTACHMENT_WRITE`
- `RenderPass::begin()` and `end()` are thin wrappers around `vkCmdBeginRenderPass` / `vkCmdEndRenderPass`

### Note: scene render pass
The batchers (QuadBatch, LineBatch, etc.) bind to `postProcess.sceneRenderPass()`, not this swapchain render pass. That render pass is created inside `PostProcess::createSceneTarget()` with `finalLayout = SHADER_READ_ONLY_OPTIMAL`.

---

## Step 3 — BufferManager (VMA)

### Overview
Wrap VMA for GPU buffer allocation. Two allocation strategies: device-local (static) and CPU-to-GPU (persistently mapped dynamic).

### Files
- `src/renderer/vulkan/BufferManager.h/.cpp`

### Key design
- `VMA_IMPLEMENTATION` is defined at the top of `BufferManager.cpp` — only one translation unit
- `VmaAllocator` created with `VK_API_VERSION_1_2`
- `Buffer` struct: `VkBuffer handle`, `VmaAllocation allocation`, `void* mapped`, `VkDeviceSize size`

### Buffer types
```cpp
// Device-local (index buffers, static meshes)
Buffer createDeviceBuffer(size, usage);
// usage gets | VK_BUFFER_USAGE_TRANSFER_DST_BIT automatically

// CPU→GPU persistently mapped (dynamic VBOs, UBOs)
Buffer createDynamicBuffer(size, usage);
// VMA_MEMORY_USAGE_CPU_TO_GPU + MAPPED_BIT → buf.mapped = VmaAllocationInfo.pMappedData
```

### Upload helper
```cpp
// uploadToBuffer: creates CPU_ONLY staging, memcpy, beginSingleTimeCommands → vkCmdCopyBuffer
//                → endSingleTimeCommands, destroys staging
bufMgr.uploadToBuffer(ctx, dst, data, size);
```

### `TextureManager` also uses VMA
`TextureManager::init()` stores `m_allocator = bufMgr.allocator()` and calls `vmaCreateImage` directly.

---

## Step 4 — Pipeline builder

### Overview
`Pipeline` is a reusable graphics pipeline builder driven by `PipelineConfig`. All batchers construct their own layout and pass it in.

### Files
- `src/renderer/vulkan/Pipeline.h/.cpp`

### PipelineConfig fields
```cpp
struct PipelineConfig {
    VkRenderPass   renderPass;
    VkPipelineLayout layout;          // owned by caller (QuadBatch, LineBatch, etc.)
    std::string    vertShaderPath, fragShaderPath;
    VkVertexInputBindingDescription vertexBinding;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    VkPrimitiveTopology topology = TRIANGLE_LIST;
    bool depthTest = false, depthWrite = false;
    enum class Blend { None, Alpha, Additive } blend = Alpha;
};
```

### Fixed pipeline state
- Rasterizer: `FILL`, `CULL_NONE`, CCW front face, lineWidth=1
- Multisampling: 1 sample
- Dynamic state: `VIEWPORT` + `SCISSOR` only — no pipeline recreation on resize

### Blend modes
- Alpha: `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` — used by QuadBatch, LineBatch, MeshRenderer
- Additive: `SRC_ALPHA / ONE` — used by ParticleSystem

### Depth
Only `MeshRenderer` sets `depthTest=true, depthWrite=true`. All 2D batchers leave depth off.

### `Pipeline::shutdown()` only destroys the `VkPipeline`. The layout is owned and destroyed by the batcher.

---

## Step 5 — QuadBatch

### Overview
Batched 2D textured quad renderer. Vertices are CPU-built each frame into a per-frame persistently mapped VBO.

### Files
- `src/renderer/QuadBatch.h/.cpp`
- `src/renderer/RenderTypes.h`

### Initialization
```
MAX_QUADS = 8192, QUAD_VERTS = 32768, QUAD_INDICES = 49152
```
- 3× dynamic VBOs (`sizeof(QuadVertex) * QUAD_VERTS` each, persistently mapped)
- 3× UBO buffers + frame descriptor sets (set 0)
- 1× static index buffer: pre-built pattern `{v,v+1,v+2, v+2,v+3,v}` for all 8192 quads, uploaded once via `uploadToBuffer`
- Pipeline layout: set0=UBO + set1=texture + push=`QuadPushConstants` (96 bytes)

### `drawQuad()` per-call work
1. Compute 4 rotated corners in world space
2. Apply `uvTransform` (xy=offset, zw=scale) to UVs
3. Append 4 `QuadVertex` to `m_vertices`
4. If new texture ≠ last batch's texture → push new `Batch`; else extend last batch's `indexCount += 6`
5. Texture → descriptor set lookup via `m_texSetCache` (unordered_map<VkImageView, VkDescriptorSet>)

### `flush()` per-frame work
1. `memcpy` to `m_vertexBuffers[m_currentFrame].mapped`
2. Bind pipeline, VBO, index buffer, set0 (UBO)
3. Push default `QuadPushConstants` (identity model, white tint, full UV)
4. For each batch: bind set1 (texture) → `vkCmdDrawIndexed`
5. Clear `m_vertices`, `m_batches`; advance `m_currentFrame`

### QuadVertex layout
```cpp
struct QuadVertex {
    glm::vec2 pos;    // binding 0, location 0, R32G32_SFLOAT
    glm::vec2 uv;     // binding 0, location 1, R32G32_SFLOAT
    glm::vec4 color;  // binding 0, location 2, R32G32B32A32_SFLOAT
};
```

---

## Step 6 — DescriptorManager + FrameUBO + Camera

### Overview
Two shared descriptor set layouts used by all batchers. Per-frame UBO uploads happen in `Renderer::endFrame()` before flush.

### Files
- `src/renderer/vulkan/DescriptorManager.h/.cpp`
- `src/renderer/RenderTypes.h`
- `src/renderer/Camera.h`

### Descriptor pool
```
64 × UNIFORM_BUFFER
256 × COMBINED_IMAGE_SAMPLER
320 max sets
FREE_DESCRIPTOR_SET_BIT (allows individual set free)
```

### Set layouts
- **Set 0** (`frameUBOLayout`): binding 0, `UNIFORM_BUFFER`, `VERTEX|FRAGMENT`
- **Set 1** (`textureLayout`): binding 0, `COMBINED_IMAGE_SAMPLER`, `FRAGMENT`

### FrameUBO
```cpp
struct FrameUBO {
    glm::mat4 viewProj;   // 64 bytes
    float     time;       //  4 bytes
    float     _pad[3];    // 12 bytes → total 80 bytes
};
```

### Camera usage
Game mode's `onRender()` calls `renderer.setCamera(m_camera)` first. `Renderer::endFrame()` reads `m_camera.viewProjection()` and calls `updateFrameUBO(vp, time, frame)` on each batcher before flushing.

### Default camera
`Renderer::init()` sets `Camera::makeOrtho(0, w, h, 0)`. Recreated on resize.

---

## Step 7 — TextureManager

### Overview
Load PNG/JPG textures via stb_image, upload to GPU via staging buffer, create image view + sampler.

### Files
- `src/renderer/vulkan/TextureManager.h/.cpp`

### Key implementation details
- `STB_IMAGE_IMPLEMENTATION` defined at top of `TextureManager.cpp`
- `TextureManager::init()` stores `m_allocator = bufMgr.allocator()` — uses VMA for `vmaCreateImage`
- Image format: `VK_FORMAT_R8G8B8A8_SRGB`, tiling OPTIMAL, usage = `TRANSFER_DST | SAMPLED`
- Transition sequence (single-time commands):
  1. `UNDEFINED → TRANSFER_DST_OPTIMAL` (barrier TOP_OF_PIPE → TRANSFER)
  2. `vkCmdCopyBufferToImage`
  3. `TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` (barrier TRANSFER → FRAGMENT_SHADER)
- Sampler: LINEAR mag/min, CLAMP_TO_EDGE, anisotropy 16×, mipmap NEAREST

### White 1×1 fallback
```cpp
uint8_t pixels[4] = {255, 255, 255, 255};
m_whiteTexture = m_texMgr.createWhite1x1(ctx, bufMgr);
m_whiteTexSet  = m_descMgr.allocateTextureSet(ctx, m_whiteTexture.view, m_whiteTexture.sampler);
```
All game modes use `renderer.whiteView()` / `renderer.whiteSampler()` for untextured draws.

### `destroyTexture()` calls `vmaDestroyImage(m_allocator, tex.image, tex.allocation)`.

---

## Step 8 — SyncObjects (frames in flight)

### Overview
3-frame ring of per-frame semaphores and fences. Defined in `SyncObjects.h`; `MAX_FRAMES_IN_FLIGHT` constant lives here.

### Files
- `src/renderer/vulkan/SyncObjects.h/.cpp`

### Per-frame objects
```
imageAvailable[3]  — VkSemaphore
renderFinished[3]  — VkSemaphore
inFlight[3]        — VkFence (created SIGNALED so first frame doesn't stall)
```

### Full frame loop (Renderer)

**beginFrame()**:
1. `vkWaitForFences(inFlight[frame])` — wait for GPU done with this slot
2. `vkAcquireNextImageKHR(swapchain, imageAvailable[frame])` → `m_imageIndex`
3. Return `false` on `OUT_OF_DATE` (triggers resize)
4. `vkResetFences(inFlight[frame])`
5. `cmdMgr.begin(frame)` — `ONE_TIME_SUBMIT_BIT`
6. Begin scene render pass (`postProcess.sceneFramebuffer()`)

**endFrame()**:
1. `updateFrameUBO(vp, time, frame)` on all 4 batchers
2. Flush: quads → lines → meshes → particles
3. `vkCmdEndRenderPass` (scene)
4. `postProcess.render(cmd, ctx, sceneView, VK_NULL_HANDLE)` — bloom compute
5. Begin swapchain render pass → composite draw → end
6. `cmdMgr.end(frame)`
7. Submit: wait `imageAvailable`, signal `renderFinished`, signal `inFlight` fence
8. Present: wait `renderFinished`
9. `m_sync.advance()` — `m_currentFrame = (m_currentFrame + 1) % 3`

---

## Step 9 — Swapchain Recreation

### Overview
Handle window resize gracefully. Two triggers: GLFW callback sets a flag, and `vkAcquireNextImageKHR` returns `OUT_OF_DATE`.

### Files
- `src/renderer/vulkan/Swapchain.cpp`
- `src/renderer/Renderer.cpp`
- `src/engine/Engine.cpp`

### Trigger paths
1. GLFW framebuffer resize callback → `engine->m_framebufferResized = true`
2. `beginFrame()` returns `false` (OUT_OF_DATE) → `Engine::render()` calls `renderer.onResize(window)`
3. `endFrame()` sees `SUBOPTIMAL` on present → sets `m_framebufferResized = true` (handled next frame)

### `Renderer::onResize()` sequence
1. `vkDeviceWaitIdle`
2. `m_swapchain.recreate(ctx, window)` — waits for minimization (polls `glfwGetFramebufferSize` until w>0,h>0), cleanup old, create new
3. `m_swapchain.createFramebuffers(ctx, m_renderPass.handle())`
4. `m_postProcess.resize(ctx, w, h)` — recreates scene target + bloom mips, re-allocates descriptor sets
5. Reset camera to new aspect ratio

### `GameModeRenderer::onResize()` called from `Engine::mainLoop()` after `renderer.onResize()`.

---

## Step 10 — SceneNode + Transform

### Overview
Parent-child transform hierarchy used by Phigros (notes parented to rotating judgment lines) and Lanota (notes parented to rotating rings).

### Files
- `src/core/SceneNode.h` — fully header-only; both `SceneNode` and `SceneGraph` defined here
- `src/core/SceneNode.cpp` — intentionally empty (just `#include "SceneNode.h"`)
- `src/core/Transform.h`
- `src/core/ECS.h` — `Registry` + `ComponentPool<T>` (not used by game modes, available for future)

### SceneGraph API
```cpp
NodeID createNode(NodeID parent = INVALID_NODE); // adds to m_roots if no parent
void   destroyNode(NodeID id);                   // recursive child cleanup
SceneNode* get(NodeID id);
void   markDirty(NodeID id);                     // propagates to all children
void   update();                                 // top-down world matrix resolution
glm::mat4 worldMatrix(NodeID id) const;          // reads cached result
```

### update() algorithm
```cpp
for each root: updateNode(root, identity, false)
// updateNode: if dirty || parentDirty → worldMatrix = parentWorld * local.toMatrix()
//             recurse into children with updated parentWorld
```

### Transform::toMatrix()
```cpp
glm::mat4 t = glm::translate(mat4(1), position);
glm::mat4 r = glm::mat4_cast(rotation);  // quaternion
glm::mat4 s = glm::scale(mat4(1), scale);
return t * r * s;
```

### 2D usage pattern (Phigros/Lanota)
```cpp
node->localTransform.setRotationZ(angle);   // angleAxis(angle, {0,0,1})
node->localTransform.position = {x, y, 0};
m_scene.markDirty(nodeID);
m_scene.update();
glm::mat4 world = m_scene.worldMatrix(noteNode);
glm::vec2 wpos = {world[3][0], world[3][1]};  // translation column
```

---

## Step 11 — LineBatch

### Overview
CPU-expands line segments into screen-aligned quad triangles. No index buffer — triangles emitted directly.

### Files
- `src/renderer/LineBatch.h/.cpp`

### Initialization
```
MAX_LINES = 4096 → vertex buffer = sizeof(QuadVertex) * MAX_LINES * 4 per frame
```
- 3× dynamic VBOs + UBOs + frame descriptor sets (set 0 only — no texture)
- Pipeline layout: set0 only, no push constants

### `expandLine(a, b, width, color)` algorithm
```cpp
dir  = normalize(b - a)
perp = vec2(-dir.y, dir.x) * (width * 0.5)
tl = a - perp,  tr = a + perp
bl = b - perp,  br = b + perp
emit: tl, tr, bl   (triangle 1)
emit: tr, br, bl   (triangle 2)
```
Each vertex has `uv = {0,0}` (color comes from vertex color).

### `drawPolyline(points, width, color, closed)`
Calls `expandLine` for each consecutive pair. If `closed=true`, also connects `back() → front()`.

### `flush(cmd, frameIndex)`
Takes explicit `frameIndex` (unlike QuadBatch which tracks internally). No index buffer — `vkCmdDraw(vertexCount, 1, 0, 0)`.

---

## Step 12 — BandoriRenderer

### Overview
7-lane perspective highway. Lane dividers converge to a vanishing point. Notes emerge from infinity at the top of the screen and approach the hit zone at the bottom. Particle bursts on hit.

### Files
- `src/game/modes/BandoriRenderer.h/.cpp`

### Two-camera strategy
```cpp
// Perspective VP — used only for w2s() projection, never passed to renderer.setCamera()
Camera persp = Camera::makePerspective(52.f, aspect, 0.5f, 300.f);
persp.lookAt({0.f, 1.8f, 3.5f}, {0.f, 0.f, -20.f});
m_perspVP = persp.viewProjection();
m_proj11y = std::abs(persp.projection()[1][1]);  // |1/tan(fov/2)| for pxSize

// Ortho screen camera — used by all batchers
m_camera = Camera::makeOrtho(0.f, w, h, 0.f);  // y=0 bottom, y=h top
renderer.setCamera(m_camera);
```

### World-space constants
```cpp
LANE_COUNT    = 7
LANE_SPACING  = 0.50f   // world units between lane centres
HIT_ZONE_Z    = 0.f     // near end of highway (bottom of screen)
APPROACH_Z    = -55.f   // far end / vanishing (top of screen)
SCROLL_SPEED  = 14.f    // world units / second
NOTE_WORLD_W  = 0.40f   // note width in world units
```

### Projection helpers
```cpp
// w2s: world pos → screen (y=0 bottom, y=h top)
// Vulkan-corrected perspective: ndcY=+1 → screen bottom (y=0), ndcY=-1 → screen top (y=h)
glm::vec2 w2s(glm::vec3 pos, const glm::mat4& vp, float sw, float sh) {
    glm::vec4 clip = vp * glm::vec4(pos, 1.f);
    return { (clip.x/clip.w * 0.5 + 0.5) * sw,
             (0.5 - clip.y/clip.w * 0.5) * sh };
}

// pxSize: perspective-correct pixel size
float pxSize(float worldSz, float clipW, float proj11y, float sh) {
    return worldSz * proj11y * sh * 0.5f / clipW;
}
```

### Note position
```cpp
float noteZ  = -(note.time - songTime) * SCROLL_SPEED;
float worldX = (laneX - (LANE_COUNT - 1) * 0.5f) * LANE_SPACING;
// noteZ == 0 → hit zone (bottom); noteZ == -55 → vanishing point (top)
```

### Render calls
- Lane dividers: `drawLine(w2s({wx,0,HIT_ZONE_Z}), w2s({wx,0,APPROACH_Z}), 1.5, {1,1,1,0.2})`
- Hit zone: `drawLine(w2s({left,0,0}), w2s({right,0,0}), 2, {1,1,0,0.8})`
- Notes: `drawQuad(screen, {sz, sz*0.4}, 0, color, ...)` where `sz = pxSize(0.40, clip.w, ...)`
- Hit burst: `particles().emitBurst(screen, color, 16, 250, 10, 0.6)`

---

## Step 13 — CytusRenderer

### Overview
Notes are stationary at fixed positions. A scan line sweeps up/down driven by song time. No scene graph needed.

### Files
- `src/game/modes/CytusRenderer.h/.cpp`

### Scan line position
```cpp
int   page = (int)(songTime / pageDuration);   // pageDuration = 4.0s
float t    = fmod(songTime, pageDuration) / pageDuration;
// Even page: bottom→top  (1-t)*h
// Odd  page: top→bottom   t*h
m_scanLineY = (page % 2 == 0) ? (1.f - t) * h : t * h;
```

### Note layout
Notes placed at: `x = (laneX/6)*w*0.8 + w*0.1`, `y = colsX[i%5] * h` (5 Y columns).

### Visibility + alpha
```
dt = note.time - songTime
visible if dt ∈ (-0.3, 1.0)
alpha: if dt > 0 → 1 - dt/1.0 * 0.3 (slight dimming for future notes)
       if dt < 0 → max(0, 1 + dt/0.3)  (fade out 0.3s after hit)
```

### Render calls
- Scan line: glow `drawLine(0→w, 24px, {1,1,1,0.07})` + core `drawLine(0→w, 4px, {1,1,1,0.9})`
- Hold connector: `drawLine({note.x, note.y}, {note.x, scanLineY}, radius*0.5, {0.4,0.8,1,alpha*0.5})`
- Each note: outer dark ring (size+8) + inner fill quad

---

## Step 14 — PhigrosRenderer

### Overview
Multiple rotating judgment lines with notes as scene-graph children. World matrix gives each note's final screen position.

### Files
- `src/game/modes/PhigrosRenderer.h/.cpp`

### Camera
`makeOrtho(-hw, hw, hh, -hh)` — origin at screen center, Y increases downward.

### Scene graph wiring
```cpp
// onInit: one LineState per JudgmentLineEvent
ls.nodeID = m_scene.createNode();
noteNode  = m_scene.createNode(ls.nodeID);  // note is child of line

// onUpdate: set line pose
node->localTransform.position = {pos.x, pos.y, 0};
node->localTransform.setRotationZ(rotation);
// set note local position
noteNode->localTransform.position = {posOnLine, timeDiff*speed, 0};
m_scene.markDirty(ls.nodeID);
m_scene.update();

// onRender: read world position
glm::mat4 world = m_scene.worldMatrix(noteNode);
glm::vec2 wpos  = {world[3][0], world[3][1]};
```

### Line rendering
```cpp
float c = cosf(ls.rotation), s = sinf(ls.rotation);
glm::vec2 dir = {c, s};
float len = hw * 1.5f;
renderer.lines().drawLine(pos - dir*len, pos + dir*len, 12.f, {1,1,1,0.15f}); // glow
renderer.lines().drawLine(pos - dir*len, pos + dir*len,  3.f, {1,1,1,0.9f});  // core
```

### Note rendering
```cpp
renderer.quads().drawQuad(wpos, {50, 18}, ls.rotation, color, {0,0,1,1},
                           whiteView, whiteSampler, ctx, desc);
```

---

## Step 15 — LanotaRenderer

### Overview
Tunnel perspective — camera looks straight down the ring axis. Notes emerge from a vanishing point at screen centre and grow outward to their ring positions, creating a radial approach effect. No SceneGraph.

### Files
- `src/game/modes/LanotaRenderer.h/.cpp`

### Two-camera strategy (same pattern as BandoriRenderer)
```cpp
Camera persp = Camera::makePerspective(60.f, aspect, 0.1f, 200.f);
persp.lookAt({0.f, 0.f, 4.f}, {0.f, 0.f, 0.f});  // straight down tunnel
m_perspVP = persp.viewProjection();
m_proj11y = std::abs(persp.projection()[1][1]);

m_camera = Camera::makeOrtho(0.f, w, h, 0.f);  // batchers
renderer.setCamera(m_camera);
```

### World-space constants
```cpp
BASE_RADIUS    = 1.8f   // innermost ring radius in world units
RING_SPACING   = 0.6f   // radius increment per ring
NOTE_WORLD_R   = 0.22f  // note visual radius (world units)
SCROLL_SPEED_Z = 14.f   // world units / second along Z
APPROACH_SECS  = 2.5f   // seconds of approach window
```

### Ring setup (no SceneGraph)
```cpp
ring.radius        = BASE_RADIUS + idx * RING_SPACING;
ring.rotationSpeed = 0.4f + idx * 0.15f;   // rad/s
ring.currentAngle += ring.rotationSpeed * dt;  // updated in onUpdate
```

### Note world position
```cpp
float angle = rd->angle + ring.currentAngle;
float noteZ = -timeDiff * SCROLL_SPEED_Z;  // large -Z = tiny dot; Z=0 = on ring
glm::vec3 notePos = { cos(angle)*ring.radius, sin(angle)*ring.radius, noteZ };
```
- `timeDiff = 2.5s` → noteZ = -35 → clip.w ≈ 39 → tiny dot near screen centre
- `timeDiff = 0` → noteZ = 0 → clip.w ≈ 4 → full size on ring circle

### Perspective-correct note size
```cpp
float sz = NOTE_WORLD_R * m_proj11y * sh * 0.5f / clip.w;
// At Z=0 (clip.w≈4, FOV60 720p): sz ≈ 34 px
// At Z=-35 (clip.w≈39):          sz ≈  3 px  (approaching dot)
```

### Ring polyline (projected 3D circle)
```cpp
void buildRingPolyline(float radius, vector<vec2>& out) const {
    for (int i = 0; i <= 64; ++i) {
        float a = TWO_PI * i / 64;
        out[i] = w2s({cos(a)*radius, sin(a)*radius, 0.f}, m_perspVP, sw, sh);
    }
}
// Ring at Z=0 projects with perspective foreshortening — correct for the camera angle
renderer.lines().drawPolyline(pts, 2.f, {0.5,0.7,1,0.5}, true);
```

### Note render
Two quads per note: dark halo `(sz*1.3)` + yellow fill `(sz)`, both perspective-sized.

---

## Step 16 — MeshRenderer + ArcaeaRenderer

### Overview
Perspective 3D view. Arc notes are tessellated ribbon meshes uploaded at init time. Scroll is achieved via model matrix Z offset each frame.

### Files
- `src/renderer/MeshRenderer.h/.cpp`
- `src/game/modes/ArcaeaRenderer.h/.cpp`

### MeshRenderer
- Meshes are static (`createDeviceBuffer` + `uploadToBuffer`) — geometry doesn't change
- `drawMesh(mesh, model, tint)` queues a `DrawEntry`
- `flush()`: bind pipeline → for each entry: bind VBO+IB, push `{model, tint}` (80 bytes), `vkCmdDrawIndexed`
- No texture: vertex colors only
- `depthTest = depthWrite = true` — only pipeline with depth

### Arc mesh construction
```cpp
// 32 segments, width=0.35
for i in 0..ARC_SEGMENTS:
    t = i / 32.0
    z = -t * arc.duration * 8.0      // pre-baked Z, slides via model matrix
    xy = evalArc(arc, t)             // eased Bezier
    vL = {xy.x - 0.175, xy.y, z}
    vR = {xy.x + 0.175, xy.y, z}
    indices: triangle strip quad between segments
```

### Arc render
```cpp
float zOffset = (am.startTime - m_songTime) * SCROLL_SPEED;
glm::mat4 model = translate(identity, {0, 0, -zOffset});
renderer.meshes().drawMesh(am.mesh, model, tint);
```

### Camera
```cpp
m_camera = Camera::makePerspective(45.f, aspect, 0.1f, 200.f);
m_camera.lookAt({0.f, 3.f, 10.f}, {0.f, 0.f, 0.f});
```

### Ground + tap meshes
Built once in `onInit()` as static `Mesh` objects. Ground: 3×60 XZ quad at `y=-2`. Tap: 0.8×0.8 XZ quad at `y=-1.95`.

---

## Step 17 — ParticleSystem + hit effects

### Overview
Ring-buffer particle pool, additive blend. Emitted on note hit, updated every frame, flushed as non-indexed triangles.

### Files
- `src/renderer/ParticleSystem.h/.cpp`

### Pool
```
MAX_PARTICLES = 2048
std::array<Particle, 2048> m_pool
m_head wraps via m_head % MAX_PARTICLES (overwrites oldest)
```

### Particle struct
```cpp
struct Particle {
    glm::vec2 pos, vel;
    glm::vec4 color;
    float size, life, maxLife;
};
```

### `update(dt)` per live particle
```cpp
life -= dt;
pos  += vel * dt;
vel  *= 0.92f;                       // drag
color.a = (life / maxLife)^2;        // quadratic alpha fade
```

### `emitBurst(pos, color, count=12, speed=200, size=8, lifetime=0.5)`
```cpp
for i in 0..count:
    angle = TWO_PI * i / count + random_jitter
    spd   = speed * (0.5 + rand01)
    emit(pos, {cos(a)*spd, sin(a)*spd}, color, size*(0.5+rand01), lifetime*rand)
```

### `flush(cmd, frameIndex, whiteTexSet)`
Builds vertex buffer from alive particles (6 verts each, no index buffer), binds additive pipeline, draws. Uses same `quad.vert/quad.frag` shaders as QuadBatch but with `Blend::Additive`.

### BandoriRenderer hit trigger
```cpp
if (timeDiff > -0.05 && timeDiff < 0.05 && !m_hitNotes.count(note.id)) {
    m_hitNotes.insert(note.id);
    renderer.particles().emitBurst({x, hitZoneY}, color, 16, 250.f, 10.f, 0.6f);
}
```

---

## Step 18 — PostProcess Bloom

### Overview
Scene renders to an offscreen RGBA16F image → bloom compute mip chain → composite fullscreen pass onto swapchain.

### Files
- `src/renderer/PostProcess.h/.cpp`

### Files modified
- `src/renderer/Renderer.h` — `PostProcess m_postProcess` member
- `src/renderer/Renderer.cpp` — wired into init/shutdown/beginFrame/endFrame/onResize

### Memory strategy
- Scene image: `VK_FORMAT_R16G16B16A16_SFLOAT`, usage = `COLOR_ATTACHMENT | SAMPLED | STORAGE`
- Bloom mips (5): same format, each half the previous, usage = `STORAGE | SAMPLED`
- All images use raw `vkAllocateMemory` (consistent with `PostProcess.h` using `VkDeviceMemory`)
- Bloom mips transitioned `UNDEFINED → GENERAL` once at init and on resize; they stay `GENERAL` permanently

### Descriptor layouts (owned by PostProcess)
- `m_computeSetLayout`: binding0 = `COMBINED_IMAGE_SAMPLER` (src), binding1 = `STORAGE_IMAGE` (dst)
- `m_compositeSetLayout`: binding0 = sampler2D (scene), binding1 = sampler2D (bloom mip0)

### Pipelines
- `m_downsamplePipeline` from `bloom_downsample.comp.spv`
- `m_upsamplePipeline` from `bloom_upsample.comp.spv`
- Both share `m_computeLayout` (push = `vec2 srcTexelSize`)
- `m_compositePipeline` from `composite.vert.spv` / `composite.frag.spv` (push = `float bloomStrength`)

### `render()` per-frame sequence
1. Downsample mip0→1→2→3→4: memory barrier (GENERAL write→read), bind set, push texelSize, dispatch `(w+7)/8, (h+7)/8, 1`
2. Upsample mip4→3→2→1→0: memory barrier, bind set, push texelSize, dispatch, accumulate into destination
3. Final memory barrier mip0 `COMPUTE_SHADER → FRAGMENT_SHADER` (layout stays GENERAL)

### Renderer frame loop
- `beginFrame()`: begin render pass on `postProcess.sceneFramebuffer()` instead of swapchain framebuffer
- `endFrame()`:
  1. End scene render pass (finalLayout auto-transitions to `SHADER_READ_ONLY_OPTIMAL`)
  2. `postProcess.render(cmd, ctx, sceneView, VK_NULL_HANDLE)`
  3. Begin swapchain render pass
  4. Bind composite pipeline + set, push `bloomStrength`, `vkCmdDraw(3, 1, 0, 0)`
  5. End swapchain render pass → submit → present

### composite.vert
```glsl
// No vertex input — fullscreen triangle via gl_VertexIndex
vec2 pos[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
vec2 uvs[3] = vec2[](vec2(0,0),   vec2(2,0),  vec2(0,2));
gl_Position = vec4(pos[gl_VertexIndex], 0, 1);
fragUV      = uvs[gl_VertexIndex];
```

---

## Step 19 — Audio Sync

### Overview
Replace wall-clock song time with the audio DSP clock from miniaudio. Falls back to wall clock when no audio is playing.

### Files
- `src/engine/AudioEngine.h/.cpp`
- `src/engine/GameClock.h`
- `src/engine/Engine.cpp`

### AudioEngine
- `MINIAUDIO_IMPLEMENTATION` defined at top of `AudioEngine.cpp`
- Pimpl pattern: `struct Impl { ma_engine engine; ma_sound sound; bool soundLoaded; }`
- `init()` → `ma_engine_init(nullptr, &m_impl->engine)`
- `load(path)` → `ma_sound_init_from_file` (uninits previous sound if any)
- `play()` → `ma_sound_seek_to_pcm_frame(0)` + `ma_sound_start`
- `positionSeconds()` → `ma_sound_get_cursor_in_seconds` or `-1.0` if not playing

### Engine::update() timing logic
```cpp
double dspPos = m_audio.positionSeconds();
if (dspPos >= 0.0)
    m_clock.setSongTime(dspPos);       // DSP clock drives song time
else
    m_clock.setSongTime(m_clock.songTime() + dt);  // wall clock fallback
```

### GameClock
- `tick()` always returns wall dt — used for particles, animations, anything frame-rate dependent
- `setSongTime(t)` only overrides `m_songTime` — wall time is independent
- All game modes receive `m_clock.songTime()` via `onUpdate(dt, songTime)`

### loadAudio() usage
```cpp
engine.loadAudio("path/to/song.ogg");  // load + play + setSongTime(0)
engine.setMode(GameMode::Bandori, chart);
```
Audio and chart must be loaded/set before `engine.run()` for perfectly synchronized playback.

---

## Critical Files

| File | Why critical |
|---|---|
| `src/core/SceneNode.h` | Transform hierarchy — Phigros depends on this |
| `src/game/modes/GameModeRenderer.h` | Plugin interface — boundary between engine and game logic |
| `src/game/chart/ChartTypes.h` | Unified note format — all loaders and renderers depend on this |
| `src/renderer/Camera.h` | Ortho/perspective abstraction shared by all modes |
| `src/renderer/LineBatch.h` | Needed by Bandori, Cytus, Phigros, Lanota |
| `src/renderer/MeshRenderer.h` | Needed exclusively by Arcaea arc notes |
| `src/renderer/vulkan/SyncObjects.h` | Defines `MAX_FRAMES_IN_FLIGHT=3` — all per-frame arrays sized from this |
| `src/renderer/vulkan/BufferManager.cpp` | Contains `VMA_IMPLEMENTATION` — must be exactly one TU |
| `src/renderer/vulkan/TextureManager.cpp` | Contains `STB_IMAGE_IMPLEMENTATION` — must be exactly one TU |
| `src/engine/AudioEngine.cpp` | Contains `MINIAUDIO_IMPLEMENTATION` — must be exactly one TU |
