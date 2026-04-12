---
name: Rendering System
description: Vulkan backend + batcher layer — all rendering infrastructure
type: project
date: 2026-04-03
originSessionId: d4e6dddd-1cc1-4f7b-8da6-079be9eb81c0
---
# System 1 — Rendering System ✅ COMPLETE

**Root:** `engine/src/renderer/`

---

## Architecture: Two-Layer Design

```
┌──────────────────────────────────────────────────┐
│  Renderer (top-level owner)                       │
│  Renderer.h/.cpp                                  │
│  Exposes: whiteView(), whiteSampler(), descriptors()│
├──────────────────────────────────────────────────┤
│  Batcher Layer                                    │
│  QuadBatch | LineBatch | MeshRenderer             │
│  ParticleSystem | PostProcess                     │
├──────────────────────────────────────────────────┤
│  Vulkan Backend                                   │
│  VulkanContext → Swapchain → RenderPass           │
│  Pipeline | BufferManager | DescriptorManager     │
│  CommandManager | SyncObjects | TextureManager    │
└──────────────────────────────────────────────────┘
```

---

## Vulkan Backend — `engine/src/renderer/vulkan/`

| File | Responsibility |
|---|---|
| `VulkanContext.h/.cpp` | Instance, physical/logical device, queues, surface |
| `Swapchain.h/.cpp` | Swapchain creation, image views, framebuffers |
| `RenderPass.h/.cpp` | Single-subpass color render pass. **Critical:** needs BOTH EXTERNAL→0 and 0→EXTERNAL subpass dependencies for PostProcess |
| `Pipeline.h/.cpp` | `PipelineConfig` builder → `VkPipeline`. One pipeline per shader variant |
| `DescriptorManager.h/.cpp` | Pool + set0 UBO + set1 sampler layouts |
| `BufferManager.h/.cpp` | VMA-backed vertex/index/uniform buffers. `VMA_IMPLEMENTATION` lives here |
| `TextureManager.h/.cpp` | stb_image load → VMA image alloc. `STB_IMAGE_IMPLEMENTATION` lives here |
| `CommandManager.h/.cpp` | Per-frame command buffer alloc/begin/end |
| `SyncObjects.h/.cpp` | `MAX_FRAMES_IN_FLIGHT = 3`, semaphores, fences |

---

## Batcher Layer — `engine/src/renderer/`

| File | Details |
|---|---|
| `QuadBatch.h/.cpp` | Textured quads. `MAX_QUADS = 8192`. Self-contained per-frame UBOs + descriptor sets |
| `LineBatch.h/.cpp` | Lines CPU-expanded to quad triangles. `MAX_LINES = 4096`. Self-contained |
| `MeshRenderer.h/.cpp` | Per-mesh draw with depth test |
| `ParticleSystem.h/.cpp` | Ring buffer 2048 particles, additive blend |
| `PostProcess.h/.cpp` | Bloom compute mip chain (downsample→upsample) + composite pass |
| `RenderTypes.h` | `QuadVertex`, `LineVertex`, `MeshVertex`, `FrameUBO`, `QuadPushConstants`, `DrawCall` |
| `Camera.h` | Unified ortho + perspective. Header-only |
| `Renderer.h/.cpp` | Owns all batchers. Exposes `whiteView()`, `whiteSampler()`, `descriptors()` for game mode plugins |

---

## Shaders — `shaders/`

| Shader | Purpose |
|---|---|
| `quad.vert / quad.frag` | Textured quad rendering |
| `line.vert / line.frag` | Line rendering |
| `mesh.vert / mesh.frag` | 3D mesh rendering |
| `bloom_downsample.comp` | Bloom compute pass — downsample |
| `bloom_upsample.comp` | Bloom compute pass — upsample |
| `composite.vert / composite.frag` | Final bloom composite |

Compiled by `glslc` to `build/shaders/*.spv`.

---

## Key Design Rules
- All batchers are **self-contained**: each manages its own per-frame UBOs + descriptor sets
- `Renderer` is the **single owner** — game modes never allocate Vulkan resources directly
- **Camera distance critical**: BandoriRenderer eye_z must be >=8 from hit zone (lesson from past debug)
- **PostProcess subpass dependency**: must declare BOTH `EXTERNAL->0` AND `0->EXTERNAL` dependencies

## ImGuiLayer — Descriptor Pool

`engine/src/ui/ImGuiLayer.cpp`. Pool sized to **256 sets** (was 32, caused crash when 3 editor screens loaded thumbnails simultaneously). Each `addTexture()` call allocates one `COMBINED_IMAGE_SAMPLER` descriptor set. Consumers: font atlas, scene texture, per-editor thumbnails (~50 each), cover images, backgrounds.

**Bugfix (2026-04-08):** Navigation from StartScreen to MusicSelection crashed with `Invalid VkDescriptorSet 0xCCCC...` (MSVC uninitialized fill) because the 32-set pool was exhausted by StartScreenEditor thumbnails before MusicSelectionEditor could allocate its own.

## Known Issue: Duplicate ChartTypes.h

`engine/src/game/chart/ChartTypes.h` (internal, compiled) vs `engine/include/MusicGameEngine/ChartTypes.h` (public header, not compiled). Definitions may diverge — ODR violation if any consumer includes both. Currently harmless since public headers are unused by compiled code.

---

## Third-Party Dependencies

| Library | Purpose | Location |
|---|---|---|
| VMA | GPU memory management | `third_party/vma/` |
| stb_image | Texture loading | `third_party/stb/` |
| GLFW 3.x | Window + Vulkan surface | `third_party/glfw/` |
| GLM | Math (vec2/vec4/mat4/quat) | `third_party/glm_extracted/` |
| glslc (Vulkan SDK) | GLSL → SPIR-V | system PATH |
