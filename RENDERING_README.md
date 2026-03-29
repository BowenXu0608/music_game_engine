# Rendering System Documentation

## Overview

Vulkan-based rendering system for a multi-mode music game engine supporting BanG Dream, Phigros, Arcaea, Cytus, and Lanota. Built with C++20 on Windows.

**Stack**: Vulkan 1.2, GLFW, GLM, VMA (Vulkan Memory Allocator), stb_image

---

## Architecture

### Core Components

**Renderer** (`src/renderer/Renderer.h/.cpp`)
- Top-level rendering coordinator
- Owns all batchers: QuadBatch, LineBatch, MeshRenderer, ParticleSystem
- Manages frame synchronization and render passes
- Exposes white texture fallback for untextured draws

**Batchers**
- `QuadBatch` - Batched 2D textured quads (max 8192 quads/frame)
- `LineBatch` - CPU-expanded line segments to screen-aligned quads (max 4096 lines/frame)
- `MeshRenderer` - Per-mesh 3D rendering with depth testing
- `ParticleSystem` - Ring-buffer particle pool (2048 particles, additive blend)

**PostProcess** (`src/renderer/PostProcess.h/.cpp`)
- Offscreen scene rendering to RGBA16F target
- 5-level bloom mip chain (compute shaders)
- Composite pass to swapchain

---

## Vulkan Architecture

### Frame Strategy

```
MAX_FRAMES_IN_FLIGHT = 3
V-Sync: FIFO (default) or MAILBOX (vsync=false)
```

**Per-frame resources**:
- Persistently mapped dynamic VBOs (VMA `CPU_TO_GPU` + `MAPPED_BIT`)
- UBO for camera/time data
- Command buffer
- Semaphores: imageAvailable, renderFinished
- Fence: inFlight

**Shared resources**:
- Static index buffer (QuadBatch only)
- Pipeline objects
- Descriptor set layouts

### Descriptor Sets

**Set 0** - Per-frame UBO (all pipelines)
```cpp
struct FrameUBO {
    glm::mat4 viewProj;  // 64 bytes
    float     time;      //  4 bytes
    float     _pad[3];   // 12 bytes (alignment)
};
```

**Set 1** - Per-texture sampler (QuadBatch, ParticleSystem)
- Combined image sampler
- LINEAR filtering, CLAMP_TO_EDGE, 16× anisotropy

### Push Constants

**QuadBatch/ParticleSystem** (96 bytes):
```cpp
struct QuadPushConstants {
    glm::vec4 tint;         // 16 bytes
    glm::vec4 uvTransform;  // 16 bytes (xy=offset, zw=scale)
    glm::mat4 model;        // 64 bytes
};
```

**MeshRenderer** (80 bytes):
```cpp
struct MeshPushConstants {
    glm::mat4 model;  // 64 bytes
    glm::vec4 tint;   // 16 bytes
};
```

### Pipelines

| Pipeline | Topology | Blend | Depth | Usage |
|----------|----------|-------|-------|-------|
| QUAD | TRIANGLE_LIST | Alpha | Off | QuadBatch |
| PARTICLE | TRIANGLE_LIST | Additive | Off | ParticleSystem |
| LINE | TRIANGLE_LIST | Alpha | Off | LineBatch |
| MESH | TRIANGLE_LIST | Alpha | On | MeshRenderer |

All pipelines use dynamic viewport/scissor (no recreation on resize).

---

## Render Passes

### Scene Render Pass
- Created by `PostProcess::createSceneTarget()`
- Color attachment: RGBA16F, `finalLayout = SHADER_READ_ONLY_OPTIMAL`
- Used by all batchers
- **Critical**: Requires BOTH subpass dependencies:
  - `EXTERNAL → 0`: ensures previous frame's composite read completes
  - `0 → EXTERNAL`: ensures scene writes visible to bloom compute

### Swapchain Render Pass
- Created by `RenderPass::create()`
- Color attachment: B8G8R8A8_SRGB, `finalLayout = PRESENT_SRC_KHR`
- Used for ImGui UI rendering (Unity-style editor)

---

## Frame Loop (Unity-Style Editor)

```cpp
// Renderer::beginFrame()
1. vkWaitForFences(inFlight[frame])
2. vkAcquireNextImageKHR → imageIndex
3. vkResetFences(inFlight[frame])
4. Begin command buffer
5. Begin scene render pass (offscreen RGBA16F)

// Game mode renders here (only if playing)
if (playing) gameMode->onRender(renderer);

// Renderer::endFrame()
6. Update FrameUBO for all batchers
7. Flush: quads → lines → meshes → particles
8. End scene render pass
9. PostProcess bloom compute passes
10. Begin swapchain render pass (clear to dark gray, no composite)

// ImGui renders here (Engine::render())
11. ImGui displays scene texture in viewport + UI panels

// Renderer::finishFrame()
12. End swapchain render pass
13. Submit (wait imageAvailable, signal renderFinished + inFlight)
14. Present (wait renderFinished)
15. Advance frame counter
```

**Key difference from traditional flow**: Scene is NOT composited fullscreen. Instead, ImGui displays the scene texture inside the Scene window viewport, allowing for a Unity-style editor interface.

---

## Camera System

Header-only in `src/renderer/Camera.h`.

### Orthographic (2D modes)
```cpp
// Top-left origin, Y down (Bandori, Cytus)
Camera::makeOrtho(0, width, height, 0);

// Centered, Y down (Phigros, Lanota)
Camera::makeOrtho(-halfWidth, halfWidth, halfHeight, -halfHeight);
```

### Perspective (3D modes)
```cpp
// Arcaea, Bandori highway, Lanota tunnel
Camera::makePerspective(fovDegrees, aspect, near, far);
camera.lookAt(eye, target);
```

**Note**: `makePerspective` flips `proj[1][1] *= -1` for Vulkan NDC (Y-down).

### Dual-Camera Pattern

BandoriRenderer and LanotaRenderer use two cameras:
1. **Perspective VP** - for world→screen projection (`w2s()` helper)
2. **Ortho camera** - passed to `renderer.setCamera()` for batchers

This allows 3D perspective calculations while batchers work in screen space.

---

## Batchers Deep Dive

### QuadBatch

**Initialization**:
- 3× dynamic VBOs (32768 vertices each, persistently mapped)
- 1× static index buffer (49152 indices, pattern: `{v,v+1,v+2, v+2,v+3,v}`)
- Pipeline layout: set0 (UBO) + set1 (texture) + push constants

**Per-call** (`drawQuad`):
1. Compute 4 rotated corners in world space
2. Apply UV transform (offset + scale)
3. Append 4 vertices to CPU buffer
4. Batch by texture (new texture → new batch)

**Per-frame** (`flush`):
1. `memcpy` vertices to mapped VBO
2. Bind pipeline, VBO, index buffer, set0
3. For each batch: bind texture set1 → `vkCmdDrawIndexed`
4. Clear CPU buffers, advance frame counter

**Vertex layout**:
```cpp
struct QuadVertex {
    glm::vec2 pos;    // location 0
    glm::vec2 uv;     // location 1
    glm::vec4 color;  // location 2
};
```

### LineBatch

**Initialization**:
- 3× dynamic VBOs (4096 lines × 6 vertices each)
- No index buffer (triangles emitted directly)
- Pipeline layout: set0 only (no textures)

**Line expansion algorithm**:
```cpp
dir  = normalize(b - a)
perp = vec2(-dir.y, dir.x) * (width * 0.5)
// Emit 2 triangles forming screen-aligned quad
tl = a - perp,  tr = a + perp
bl = b - perp,  br = b + perp
triangles: {tl, tr, bl}, {tr, br, bl}
```

**API**:
- `drawLine(a, b, width, color)` - single segment
- `drawPolyline(points, width, color, closed)` - connected segments

### MeshRenderer

**Initialization**:
- Static VBOs/IBOs (device-local, uploaded once via staging)
- Pipeline: depth test + write enabled
- Push constants: model matrix + tint

**Usage**:
```cpp
Mesh mesh = createMesh(vertices, indices);
meshRenderer.drawMesh(mesh, modelMatrix, tint);
```

**Per-frame** (`flush`):
- For each queued mesh: bind buffers, push constants, `vkCmdDrawIndexed`
- Used exclusively by ArcaeaRenderer for arc ribbons

### ParticleSystem

**Initialization**:
- Ring buffer: 2048 particles
- 3× dynamic VBOs (same size as QuadBatch)
- Pipeline: additive blend, same shaders as QuadBatch

**Particle struct**:
```cpp
struct Particle {
    glm::vec2 pos, vel;
    glm::vec4 color;
    float size, life, maxLife;
};
```

**Update logic** (per frame):
```cpp
life -= dt;
pos  += vel * dt;
vel  *= 0.92f;              // drag
color.a = (life/maxLife)²;  // quadratic fade
```

**Burst emission**:
```cpp
emitBurst(pos, color, count=12, speed=200, size=8, lifetime=0.5);
// Emits particles in radial pattern with random jitter
```

---

## PostProcess Bloom

### Pipeline

1. **Scene render** → RGBA16F offscreen target
2. **Downsample** → 5 mip levels (compute shader, each half previous size)
3. **Upsample** → accumulate back to mip0 (compute shader)
4. **Composite** → fullscreen triangle blends scene + bloom → swapchain

### Memory Layout

- Scene image: `R16G16B16A16_SFLOAT`, usage = `COLOR_ATTACHMENT | SAMPLED | STORAGE`
- Bloom mips: same format, usage = `STORAGE | SAMPLED`
- All mips stay in `GENERAL` layout permanently (transitioned once at init)

### Compute Shaders

**Downsample** (`bloom_downsample.comp`):
- 13-tap Karis average (prevents fireflies)
- Push constants: `vec2 srcTexelSize`
- Dispatch: `(width+7)/8, (height+7)/8, 1`

**Upsample** (`bloom_upsample.comp`):
- 9-tap tent filter
- Accumulates into destination (additive)
- Same dispatch pattern

### Composite Pass

**Vertex shader** (`composite.vert`):
```glsl
// Fullscreen triangle (no vertex buffer)
vec2 pos[3] = {vec2(-1,-1), vec2(3,-1), vec2(-1,3)};
gl_Position = vec4(pos[gl_VertexIndex], 0, 1);
```

**Fragment shader** (`composite.frag`):
```glsl
vec3 scene = texture(sceneTexture, uv).rgb;
vec3 bloom = texture(bloomTexture, uv).rgb;
fragColor = vec4(scene + bloom * bloomStrength, 1.0);
```

---

## Shaders

### Quad/Particle Shaders

**quad.vert**:
- Input: `pos`, `uv`, `color` (per-vertex)
- UBO: `viewProj`, `time`
- Push: `model`, `tint`, `uvTransform`
- Output: transformed position, modulated color, transformed UV

**quad.frag**:
- Input: `color`, `uv`
- Samples texture, multiplies by vertex color
- Used by both QuadBatch (alpha blend) and ParticleSystem (additive blend)

### Line Shaders

**line.vert**:
- Same vertex layout as quad
- No push constants (lines pre-expanded on CPU)

**line.frag**:
- Vertex color only (no texture sampling)

### Mesh Shaders

**mesh.vert**:
- Input: `pos`, `normal`, `color`
- Push: `model`, `tint`
- Outputs transformed position + modulated color

**mesh.frag**:
- Vertex color only
- Depth testing enabled

---

## Game Mode Integration

### Plugin Interface

```cpp
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

### Rendering Pattern

```cpp
void MyModeRenderer::onRender(Renderer& renderer) {
    // Set camera first
    renderer.setCamera(m_camera);

    // Draw using batchers
    renderer.quads().drawQuad(pos, size, rotation, color, uv,
                              renderer.whiteView(),
                              renderer.whiteSampler(),
                              renderer.context(),
                              renderer.descriptors());

    renderer.lines().drawLine(a, b, width, color);

    // Particles update automatically
    renderer.particles().emitBurst(pos, color, 16, 250, 10, 0.6);
}
```

### Mode-Specific Techniques

**BandoriRenderer** - Perspective highway
- Two cameras: perspective VP for projection, ortho for batchers
- Camera: eye `{0, 1.8, 8.0}`, target `{0, 0, -20}`, FOV 52°
- Notes scroll from Z=-55 (top) to Z=0 (hit zone at bottom)
- `w2s()` helper projects 3D world → 2D screen
- `pxSize()` calculates perspective-correct pixel size
- Post-hit cull: `noteZ > 2.0f`

**CytusRenderer** - Scanning line
- Ortho camera: `(0, w, h, 0)`
- Notes stationary at fixed grid positions
- Scan line alternates: even pages bottom→top, odd pages top→bottom
- Page duration: 4 seconds
- Visibility window: `timeDiff ∈ (-0.3, 1.0)` seconds

**PhigrosRenderer** - Rotating judgment lines
- Ortho camera: `(-hw, hw, hh, -hh)` centered at origin
- SceneGraph: notes parented to judgment line nodes
- Line rotation propagates to all child notes
- `m_scene.update()` resolves world matrices top-down
- Reads `worldMatrix[3][0/1]` for final screen position

**ArcaeaRenderer** - 3D arc ribbons
- Perspective camera: FOV 45°, eye `{0, 3, 10}`
- MeshRenderer only (no QuadBatch/LineBatch)
- Arc meshes: 32-segment ribbons, pre-tessellated at init
- Scroll via model matrix Z translation
- Ground plane at y=-2 for depth reference

**LanotaRenderer** - Tunnel perspective
- Two cameras: perspective VP for projection, ortho for batchers
- Camera: eye `{0, 0, 4}`, target `{0, 0, 0}`, FOV 60°
- Concentric rings rotate independently
- Notes emerge from screen center (far Z) and grow to ring size (Z=0)
- No SceneGraph (ring angles accumulate per-frame)
- `buildRingPolyline()` projects 3D circles with perspective

---

## Memory Management

### VMA Integration

**BufferManager** (`src/renderer/vulkan/BufferManager.cpp`):
- `VMA_IMPLEMENTATION` defined here (only once)
- Device-local buffers: `VMA_MEMORY_USAGE_GPU_ONLY` + `TRANSFER_DST_BIT`
- Dynamic buffers: `VMA_MEMORY_USAGE_CPU_TO_GPU` + `MAPPED_BIT`

**TextureManager** (`src/renderer/vulkan/TextureManager.cpp`):
- `STB_IMAGE_IMPLEMENTATION` defined here (only once)
- Stores `m_allocator` from BufferManager
- Uses `vmaCreateImage` for texture allocation
- Transition: `UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY`

**PostProcess**:
- Uses raw `vkAllocateMemory` (not VMA)
- Consistent with existing `VkDeviceMemory` usage in PostProcess.h

### Upload Strategy

**Static data** (index buffers, meshes):
1. Create staging buffer (`CPU_ONLY`)
2. `memcpy` to staging
3. Single-time command: `vkCmdCopyBuffer`
4. Destroy staging

**Dynamic data** (per-frame VBOs, UBOs):
- Persistently mapped at creation
- Direct `memcpy` to `buffer.mapped` pointer each frame
- No staging needed

---

## Synchronization

### Frame Pacing

```cpp
MAX_FRAMES_IN_FLIGHT = 3

Per-frame sync objects:
- VkSemaphore imageAvailable[3]  // GPU-GPU: acquire → render
- VkSemaphore renderFinished[3]  // GPU-GPU: render → present
- VkFence inFlight[3]            // CPU-GPU: frame completion
```

### Critical Barriers

**Scene → Bloom**:
```cpp
// After scene render pass ends (automatic transition)
finalLayout = SHADER_READ_ONLY_OPTIMAL
// Bloom compute reads scene as sampled image
```

**Bloom compute mips**:
```cpp
// Between each downsample/upsample pass
srcStage  = COMPUTE_SHADER
dstStage  = COMPUTE_SHADER
srcAccess = SHADER_WRITE
dstAccess = SHADER_READ
layout    = GENERAL (stays GENERAL permanently)
```

**Bloom → Composite**:
```cpp
// After final upsample, before composite frag shader
srcStage  = COMPUTE_SHADER
dstStage  = FRAGMENT_SHADER
srcAccess = SHADER_WRITE
dstAccess = SHADER_READ
layout    = GENERAL
```

### Subpass Dependencies

**Scene render pass** (PostProcess):
```cpp
// CRITICAL: Both dependencies required
Dependency 0: EXTERNAL → Subpass 0
  srcStage  = COLOR_ATTACHMENT_OUTPUT
  dstStage  = COLOR_ATTACHMENT_OUTPUT
  srcAccess = 0
  dstAccess = COLOR_ATTACHMENT_WRITE

Dependency 1: Subpass 0 → EXTERNAL
  srcStage  = COLOR_ATTACHMENT_OUTPUT
  dstStage  = FRAGMENT_SHADER
  srcAccess = COLOR_ATTACHMENT_WRITE
  dstAccess = SHADER_READ
```

Missing dependency 1 causes black screen (no memory visibility for bloom reads).

---

## Resize Handling

### Trigger Paths

1. GLFW callback → `m_framebufferResized = true`
2. `vkAcquireNextImageKHR` returns `OUT_OF_DATE`
3. `vkQueuePresentKHR` returns `SUBOPTIMAL`

### Resize Sequence

```cpp
Renderer::onResize(window):
1. vkDeviceWaitIdle()
2. Swapchain::recreate()
   - Polls glfwGetFramebufferSize until w>0, h>0 (minimization)
   - Destroys old swapchain
   - Creates new swapchain (preserves vsync mode)
3. Swapchain::createFramebuffers()
4. PostProcess::resize()
   - Recreates scene target + bloom mips
   - Re-allocates descriptor sets
5. Update default camera aspect ratio
6. GameModeRenderer::onResize()
```

No pipeline recreation needed (dynamic viewport/scissor).

---

## Performance Characteristics

### Batching Efficiency

- QuadBatch: single `vkCmdDrawIndexed` per texture (up to 8192 quads)
- LineBatch: single `vkCmdDraw` per flush (up to 4096 lines)
- MeshRenderer: one draw call per mesh
- ParticleSystem: single `vkCmdDraw` per flush (all alive particles)

### Memory Footprint (per frame)

```
QuadBatch VBO:    32768 vertices × 32 bytes = 1 MB
LineBatch VBO:    24576 vertices × 32 bytes = 768 KB
MeshRenderer:     Static (uploaded once)
ParticleSystem:   32768 vertices × 32 bytes = 1 MB
FrameUBO:         80 bytes × 4 batchers = 320 bytes

Total dynamic per frame: ~2.8 MB × 3 frames = ~8.4 MB
```

### Validation Layers

Controlled by `ENABLE_VALIDATION_LAYERS` define (CMake Debug builds only).
- Enables `VK_LAYER_KHRONOS_validation`
- Debug messenger: WARNING + ERROR severity
- Zero validation errors expected in normal operation

---

## Critical Lessons Learned

### Camera Distance for Hit Zone Visibility

**Problem**: BandoriRenderer hit zone was at 93% from top (y=671/720), causing notes to hardware-clip before reaching the line.

**Root cause**: Camera eye_z=3.5 was too close to the highway.

**Solution**: Increased eye_z to 8.0 world units. Hit zone now at 66% from top (y≈477), providing proper visibility margin.

**Rule**: For perspective highways, camera distance must be ≥8 world units from hit zone plane.

### Subpass Dependencies

**Problem**: Black screen after implementing bloom, despite correct compute shader execution.

**Root cause**: Missing `0 → EXTERNAL` subpass dependency in scene render pass.

**Solution**: Added both dependencies:
- `EXTERNAL → 0`: ensures previous frame's reads complete
- `0 → EXTERNAL`: ensures scene writes visible to bloom compute

**Rule**: Render passes with external reads (compute/fragment shaders) need BOTH dependencies for memory visibility.

### CMake Shader Synchronization

**Problem**: Manual copying of SPV files from `build/shaders/` to `build/Debug/shaders/` after each build.

**Solution**: Added post-build custom command in CMakeLists.txt to auto-copy shaders.

**Rule**: Use CMake `add_custom_command(TARGET ... POST_BUILD)` for build artifacts that need deployment.

### Windows Macro Conflicts

**Problem**: BandoriRenderer variables `near` and `far` conflicted with Windows.h macros.

**Solution**: Renamed to `nearZ` and `farZ`.

**Rule**: Avoid common Windows macro names: `near`, `far`, `min`, `max`, `ERROR`, `IGNORE`.

---

## Build System

### CMakeLists.txt Structure

```cmake
cmake_minimum_required(VERSION 3.20)
project(MusicGame CXX)
set(CMAKE_CXX_STANDARD 20)

# Find packages
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# VMA (header-only)
set(VMA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/vma" CACHE PATH "VMA include dir")

# Shader compilation
file(GLOB SHADER_SOURCES "shaders/*.vert" "shaders/*.frag" "shaders/*.comp")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv
        COMMAND glslc ${SHADER} -o ${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv
        DEPENDS ${SHADER}
    )
endforeach()

# Post-build shader copy
add_custom_command(TARGET MusicGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:MusicGame>/shaders
)
```

### Third-Party Dependencies

```
third_party/
├── vma/
│   └── vk_mem_alloc.h
├── stb/
│   └── stb_image.h
└── glm/  (via find_package)
```

**Installation**:
- GLFW: system package manager or vcpkg
- GLM: system package manager or vcpkg
- Vulkan SDK: LunarG installer
- VMA: header-only, included in repo
- stb_image: header-only, included in repo

---

## File Organization

### Critical Files

| File | Purpose | Notes |
|------|---------|-------|
| `BufferManager.cpp` | VMA implementation | `VMA_IMPLEMENTATION` here only |
| `TextureManager.cpp` | stb_image implementation | `STB_IMAGE_IMPLEMENTATION` here only |
| `SyncObjects.h` | Frame count constant | `MAX_FRAMES_IN_FLIGHT = 3` |
| `Camera.h` | Camera abstraction | Header-only |
| `RenderTypes.h` | Shared vertex/UBO structs | Header-only |
| `GameModeRenderer.h` | Plugin interface | Pure virtual |

### Batcher Flush Signatures

```cpp
// QuadBatch - manages frame internally
void flush(VkCommandBuffer cmd, VulkanContext& ctx, DescriptorManager& descMgr);

// LineBatch, MeshRenderer, ParticleSystem - explicit frame index
void flush(VkCommandBuffer cmd, uint32_t frameIndex);

// ParticleSystem also needs white texture
void flush(VkCommandBuffer cmd, uint32_t frameIndex, VkDescriptorSet whiteTexSet);
```

**Rationale**: QuadBatch was implemented first with internal frame tracking. Later batchers use explicit index for consistency with Renderer's frame management.

---

## Debugging Tips

### RenderDoc Verification

**QuadBatch batching**:
- Check draw call count matches texture batch count
- Verify single `vkCmdDrawIndexed` per texture
- Inspect vertex buffer: should see all quads in one buffer

**Bloom passes**:
- 5 downsample dispatches (mip0→1→2→3→4)
- 5 upsample dispatches (mip4→3→2→1→0)
- Check mip dimensions: each half of previous
- Verify compute shader invocations: `(w+7)/8 × (h+7)/8`

**Frame synchronization**:
- Should see 3 command buffers in flight
- Each frame waits on its own fence
- No validation errors about fence/semaphore usage

### Common Issues

**Black screen after resize**:
- Check `vkDeviceWaitIdle` before swapchain recreation
- Verify framebuffer recreation with new dimensions
- Ensure PostProcess::resize() called

**Notes clipping early** (perspective modes):
- Increase camera distance from hit zone
- Check near/far plane values
- Verify `w2s()` projection math

**Validation errors on shutdown**:
- Ensure `vkDeviceWaitIdle` before destroying resources
- Destroy in reverse order: pipelines → layouts → descriptor sets → pools

---

## Future Enhancements

### Potential Optimizations

1. **Indirect drawing**: Use `vkCmdDrawIndexedIndirect` for QuadBatch to reduce CPU overhead
2. **Descriptor indexing**: Bindless textures to eliminate per-batch descriptor set binds
3. **Push descriptors**: `VK_KHR_push_descriptor` for single-texture draws
4. **Mesh shaders**: Replace LineBatch CPU expansion with mesh shader geometry generation

### Feature Additions

1. **MSAA**: Add multisampling to scene render pass
2. **HDR**: Use R16G16B16A16_SFLOAT swapchain format
3. **Shadows**: Add depth-only pre-pass for MeshRenderer
4. **Text rendering**: SDF font atlas + dedicated text batcher

---

## API Reference

### Renderer Public Interface

```cpp
class Renderer {
public:
    void init(GLFWwindow*, const std::string& shaderDir, bool validation);
    void shutdown();

    bool beginFrame();
    void endFrame();
    void onResize(GLFWwindow*);

    void setCamera(const Camera& camera);

    // Batcher access
    QuadBatch& quads();
    LineBatch& lines();
    MeshRenderer& meshes();
    ParticleSystem& particles();

    // Shared resources
    VkImageView whiteSampler();
    VkSampler whiteView();
    VulkanContext& context();
    DescriptorManager& descriptors();
};
```

### QuadBatch API

```cpp
void drawQuad(
    glm::vec2 position,
    glm::vec2 size,
    float rotation,
    glm::vec4 color,
    glm::vec4 uvTransform,  // xy=offset, zw=scale
    VkImageView texture,
    VkSampler sampler,
    VulkanContext& ctx,
    DescriptorManager& descMgr
);
```

### LineBatch API

```cpp
void drawLine(glm::vec2 a, glm::vec2 b, float width, glm::vec4 color);
void drawPolyline(const std::vector<glm::vec2>& points, float width,
                  glm::vec4 color, bool closed);
```

### MeshRenderer API

```cpp
struct Mesh {
    VkBuffer vertexBuffer;
    VkBuffer indexBuffer;
    uint32_t indexCount;
};

Mesh createMesh(const std::vector<MeshVertex>& vertices,
                const std::vector<uint32_t>& indices);
void drawMesh(const Mesh& mesh, const glm::mat4& model, const glm::vec4& tint);
void destroyMesh(Mesh& mesh);
```

### ParticleSystem API

```cpp
void emitBurst(glm::vec2 pos, glm::vec4 color,
               int count = 12, float speed = 200.f,
               float size = 8.f, float lifetime = 0.5f);
void update(float dt);  // Called automatically by Renderer
```

---

## Conclusion

This rendering system provides a flexible, high-performance foundation for rhythm game rendering. The plugin architecture allows each game mode to implement unique visual styles while sharing common infrastructure.

Key strengths:
- Efficient batching (single draw call per texture/primitive type)
- Clean separation between engine and game logic
- Dual-camera support for hybrid 2D/3D rendering
- Robust frame synchronization with triple buffering
- Modern Vulkan best practices (dynamic state, VMA, persistent mapping)

The system is production-ready for the five supported game modes and extensible for future additions.