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
| `QuadBatch.h/.cpp` | Textured quads. `MAX_QUADS = 8192`. Self-contained per-frame UBOs + descriptor sets. Pipeline-per-MaterialKind (5 built-in + Custom) |
| `LineBatch.h/.cpp` | Lines CPU-expanded to quad triangles. `MAX_LINES = 4096`. Self-contained |
| `MeshRenderer.h/.cpp` | Per-mesh 3D draw with depth test. Pipeline-per-MaterialKind matching QuadBatch so Custom shaders work on 3D geometry too |
| `ParticleSystem.h/.cpp` | Ring buffer 2048 particles, additive blend |
| `PostProcess.h/.cpp` | Bloom compute mip chain (downsample→upsample) + composite pass |
| `RenderTypes.h` | `QuadVertex`, `LineVertex`, `MeshVertex`, `FrameUBO`, `QuadPushConstants`, `MeshPushConstants`, `DrawCall` |
| `Material.h/.cpp` | `Material` struct + `MaterialKind` enum (Unlit/Glow/Scroll/Pulse/Gradient/Custom). Runtime value a batcher consumes |
| `MaterialAsset.h/.cpp` | On-disk material asset: `.mat` JSON with name, kind, tint, params, texture path, optional custom shader path, and `(targetMode, targetSlotSlug)` compatibility pinning |
| `MaterialAssetLibrary.h/.cpp` | Per-project registry. Loads `project/assets/materials/*.mat`, seeds built-in-kind defaults for modes in use, migrates chart inline materials into `.mat` files, provides slot-filtered picker lookup |
| `MaterialSlots.h/.cpp` | Per-mode slot tables (Bandori/Arcaea/Cytus/Lanota/Phigros) with display name, group, default kind/tint/params. Helpers: `materialSlotSlug`, `materialModeName`, `detectChartMode` |
| `ShaderCompiler.h/.cpp` | Runtime glslc invoker for Custom-kind materials. Accepts `.frag` (GLSL), `.spv` (load-verbatim), rejects `.hlsl` with a clear error. mtime-cached |
| `Camera.h` | Unified ortho + perspective. Header-only |
| `Renderer.h/.cpp` | Owns all batchers. Exposes `whiteView()`, `whiteSampler()`, `descriptors()` for game mode plugins |

---

## Shaders — `shaders/`

| Shader | Purpose |
|---|---|
| `quad.vert + quad.frag` | Baseline textured quad rendering (legacy; the batcher now uses the per-kind fragments below) |
| `quad_unlit.frag` / `quad_glow.frag` / `quad_scroll.frag` / `quad_pulse.frag` / `quad_gradient.frag` | Per-MaterialKind fragments for `QuadBatch`. Shared `quad.vert`. All consume the same 128 B push-constant block |
| `line.vert / line.frag` | Line rendering |
| `mesh.vert` | Shared vertex shader for `MeshRenderer`. Outputs `fragUV` (loc 0), `fragColor` (loc 1), `fragNormal` (loc 2) |
| `mesh_unlit.frag` / `mesh_glow.frag` / `mesh_scroll.frag` / `mesh_pulse.frag` / `mesh_gradient.frag` | Per-MaterialKind fragments for `MeshRenderer`. Same shape as the quad set but with `fragNormal` in scope (used by `mesh_glow` for rim lighting) |
| `bloom_downsample.comp` / `bloom_upsample.comp` | Bloom compute passes |
| `composite.vert / composite.frag` | Final bloom composite |

Compiled by `glslc` at build time to `build/shaders/*.spv`. Custom user shaders are also compiled by `glslc` at author time (see Material System below).

---

## Material System

Project-level material assets assigned per-slot per-chart. Layered as:

```
┌───────────────────────────────────────────────────────────────────────┐
│  Author-time authoring                                                 │
│  StartScreenEditor → Properties → Materials tab                        │
│  Create/edit/delete .mat + optional .frag compile via glslc            │
├───────────────────────────────────────────────────────────────────────┤
│  Project store                                                         │
│  <project>/assets/materials/*.mat   (one file per material)            │
│  MaterialAssetLibrary loads the dir at project open                    │
├───────────────────────────────────────────────────────────────────────┤
│  Chart assignment                                                      │
│  SongEditor → game-mode config → Materials section                     │
│  Per-slot dropdown writes {slot, asset: "<name>"} into the chart JSON  │
├───────────────────────────────────────────────────────────────────────┤
│  Runtime                                                               │
│  Renderer onInit: resolveMaterial(md, lib) → Material                  │
│  Batcher draw: kind==Custom → custom pipeline cache (lazy glslc)       │
└───────────────────────────────────────────────────────────────────────┘
```

### Kinds + custom shaders

`MaterialKind` primitives shared by `QuadBatch` and `MeshRenderer`:

| Kind | Fragment behaviour | `params` layout |
|---|---|---|
| `Unlit` | `texture * vertexColor` | unused |
| `Glow` | Unlit + additive emissive; rim lighting on mesh variant | `[intensity, falloff, hdrCap, _]` |
| `Scroll` | UV scrolls over time | `[uSpeed, vSpeed, uTile, vTile]` |
| `Pulse` | Brightness bump, exponential decay from a hit time | `[lastHitTime, decay, peakMult, _]` |
| `Gradient` | Two-colour blend vertical or radial across the quad | `[botR, botG, botB, mode]` (mode 0/1) |
| `Custom` | User-authored fragment shader; batcher builds a pipeline from its `.spv` | meaning is whatever the `.frag` reads |

Push-constant block is 128 B (Vulkan minimum): `mat4 model, vec4 tint, vec4 uvTransform, vec4 params, uint kind, uint[3] pad`. Declared identically in `QuadPushConstants` and `MeshPushConstants` so both batchers can share the same block layout in shaders. Custom fragments must declare the same block.

`ShaderCompiler` handles Custom-kind shader resolution:
- `.frag` (or any non-special extension) → treated as GLSL, compiled via glslc to `<path>.spv` with mtime cache.
- `.spv` → returned as-is (useful for mobile / distribution flows where source isn't shipped).
- `.hlsl` → rejected with `"HLSL not supported yet — please convert to GLSL (.frag)."`. Adding real HLSL support is one line (`glslc -x hlsl`) if someone asks.

glslc discovery order: `$VULKAN_SDK/Bin/glslc[.exe]`, a couple of well-known Windows install paths, then system PATH. Result is cached across calls.

### Pipeline cache (QuadBatch + MeshRenderer)

Built-in kinds get one pipeline each, created during `init()` using `quad.vert` / `mesh.vert` + the matching `quad_<kind>.frag` / `mesh_<kind>.frag`. They all share one `VkPipelineLayout` with two descriptor set layouts (frame UBO, texture sampler) and the 128 B push-constant range.

Custom kinds are built lazily the first time a `Material` with `kind == Custom` hits the draw queue. `getOrBuildCustomPipeline(ctx, fragPath)` resolves via `ShaderCompiler`, then builds a `Pipeline` sharing the same layout. The result is cached in an `unordered_map<string, Pipeline>` keyed by the source path. Batch entries store the resolved `VkPipeline` handle directly so flush dispatch is pipeline-based (not kind-based), making custom and built-in kinds coexist without special cases.

### Material assets on disk

One `.mat` JSON per material under `<project>/assets/materials/`. Shape:

```json
{
  "name": "default_arcaea_playfield_ground",
  "kind": "gradient",
  "tint":   [0.15, 0.15, 0.25, 1],
  "params": [0.05, 0.05, 0.15, 0],
  "texture": "",                     // optional, project-relative
  "shader": "",                      // Custom-kind only, project-relative
  "targetMode": "arcaea",            // compatibility: (mode, slug) pair
  "targetSlot": "playfield_ground"   // empty = "universal"
}
```

Three provenance buckets with structured names:
- `default_<mode>_<slug>` — seeded from the slot table when a mode is used by the project. Editable; edits propagate to every chart that references it.
- `<chartStem>__<slug>` — per-chart override, created only when a chart's inline material differs from the slot default.
- anything else — user-created. Compatibility fields default to empty (shows up in every slot's dropdown) unless the author pins it via the editor.

`materialSlotSlug(slot)` produces the slug: lowercase alphanumeric, group prefix when present, so slots with colliding display names (e.g. "Head" under Hold Note and Slide Note in Cytus) don't collide.

### Chart reference + migration

`ChartData::MaterialData` has:
- `assetName` — reference into the library (new authoritative form).
- legacy `kind`/`tint`/`params`/`texturePath` inline fields — kept so pre-asset-library charts still load.

Loader parses both shapes. `resolveMaterial(md, lib)` prefers the asset if `assetName` is set and the library has it; falls through to inline otherwise. This keeps Android / standalone tools that don't run migration working.

`Engine::openProject` handles migration:
1. `library.loadFromProject(path)` — scans `assets/materials/`, loads all `.mat`, backfills empty target fields when an existing default's name maps to a known slot.
2. For each mode the project actually uses (`detectChartMode` from chart filename), seed `default_<mode>_<slug>.mat` files if missing.
3. For each chart, compare each inline entry to the slot default — if match, point `assetName` at the shared default; otherwise generate `<stem>__<slug>.mat` as a per-chart override.
4. `pruneOldCrypticFiles(chartStems)` removes leftover Phase-A `<stem>_<digit>.mat` files. Only prunes names whose prefix is a known chart stem so unrelated user materials like `effect_v1` are never touched.

### Slot-filtered picker

Each SongEditor slot dropdown lists only the materials whose `(targetMode, targetSlotSlug)` match the slot's `(mode, slug)` — plus any "universal" assets (both targets empty). Implementation: `MaterialAssetLibrary::namesCompatibleWith(mode, slug)`. Editing an Arcaea chart's Click Note slot shows `default_arcaea_click_note` + any `*__click_note` overrides; Bandori's Click Note is a distinct list.

### Editor entry points

- **StartScreen → Properties → Materials tab** — create/edit/delete, kind dropdown including Custom, tint + per-kind params, texture path, Custom shader path (`.frag` or `.spv`), Template... button that drops a boilerplate `.frag` conforming to the push-constant block, Compile button surfacing glslc log, target mode + target slot pickers.
- **StartScreen → Assets panel (bottom)** — `.mat` files render as purple "MAT" tiles alongside textures/audio. Clicking a tile opens the Materials tab with that asset selected (via `m_materialsTabRequested` + `ImGuiTabItemFlags_SetSelected`).
- **SongEditor → game-mode config → Materials** — per-slot dropdown picks from the slot-filtered library. Chart save writes `{slot, asset}` form when assigned, legacy inline otherwise.

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

## GifPlayer made platform-portable (2026-05-03)

`engine/src/ui/GifPlayer` had one tie to the editor: its `load()` method took an `ImGuiLayer&` and called `imgui.addTexture(view, sampler)` per frame to register each decoded GIF frame with ImGui's descriptor pool. That blocked it from being usable in the Android player binary (no `ImGuiLayer`).

The fix replaces the `ImGuiLayer&` parameter with a direct `ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)` call — exactly what `ImGuiLayer::addTexture` was wrapping. Both desktop and Android link `imgui_impl_vulkan.cpp`, so the call is portable. Header-side: dropped `#include "ui/ImGuiLayer.h"`. `GifPlayer.cpp` adds `<imgui.h>` + `<backends/imgui_impl_vulkan.h>`.

`StartScreenView` (the new shared start-screen view at `engine/src/game/screens/StartScreenView.cpp`) now uses GifPlayer for `BgType::Gif` backgrounds and works on both desktop (where `StartScreenEditor` inherits the view) and Android (where `AndroidEngine::m_startView` drives it). All other GifPlayer callers got the matching one-arg-removed update.

Same pattern applies to any future "shared widget that touches Vulkan textures": call `ImGui_ImplVulkan_AddTexture` directly instead of going through ImGuiLayer. The latter is desktop-only and shouldn't appear in the player binary.

## Player views (game-side) — overview pointer (2026-05-03)

Player-facing rendering for the four player screens (start, music selection, gameplay HUD, results) lives at `engine/src/game/screens/` as game-side classes. Each view is a pure renderer with the player-facing state owned in the class; no editor coupling. Both desktop (where editors inherit the views) and Android (where `AndroidEngine` drives them via `AndroidEngineAdapter`) consume the same source files. Detail in `sys8_android.md` Round 7 + `sys3_core_engine.md` "Player-game / editor split (2026-05-03)".

The relevant rendering primitives (`ImDrawList::AddImage`, `AddRectFilled`, `AddRectFilledMultiColor`, `AddImageQuad`, `AddImageRounded`, `AddText`, `AddQuadFilled`, `AddTriangleFilled`, `PushClipRect`) all already worked on Android since Round 6 — Round 7 just stops duplicating the call sites between desktop and Android.

## ArcaeaRenderer — `NoteType::Hold` rendering (2026-05-03)

`ArcaeaRenderer` (3D drop) initially handled only `Tap`/`Flick`/`ArcTap`/`Arc`; `NoteType::Hold` was silently dropped during `onInit`. Charts authored for 3D drop with hold notes (`Aa_drop3d_hard.json` has one) rendered without those holds.

Implementation:
- New `m_holdNotes` (vector of `NoteEvent`) collected in `onInit` alongside taps/arcs.
- Lane auto-expand pass walks `m_holdNotes` so a chart with a lane-N-only hold still expands the playfield.
- Render loop uses the existing `m_tapMesh` (a flat XZ quad at `y = GROUND_Y + hh`, depth `2 × hd ≈ 0.8`). Per hold:
  - `zHead = (note.time - songTime) × SCROLL_SPEED × m_noteSpeedMul`
  - `zTail = zHead + hold.duration × SCROLL_SPEED × m_noteSpeedMul`
  - Cull: `if (zTail < 0 || zHead > 30) continue`.
  - `model = translate({wx, 0, JUDGMENT_Z - 0.5×(zHead+zTail)}) × scale({1, 1, (zTail - zHead) / (2 × kTapHd)})` where `kTapHd = 0.4`. Y stays at the tap mesh's baked `GROUND_Y + hh` because scale Y = 1.
  - Default material: `MaterialKind::Glow` with tint `{0.3, 0.8, 1.0, 0.95}` — visually matches Bandori's inactive hold body so 2D/3D drop hold appearance stays consistent.
- `onShutdown` clears `m_holdNotes` along with the other per-mode collections.

No Arcaea slot was added — chart hold-material overrides for 3D drop are deferred until requested. If/when added, slot 12 is the next free index in the file-local `ArcaeaSlot` enum.

## Combo HUD — offset glow + bold removed (2026-05-03)

`game/screens/GameplayHudView::drawHud` previously rendered an 8-direction offset halo when `HudTextConfig::glow=true` and a +1 px shadow when `bold=true`. The default `comboHud` had `glow=true`. On a high-DPI phone (`dpiScale ≈ 3.5×`) the offsets blurred glyphs into a single saturated blob — the unreadable gold smear over the combo number reported on Android.

Both branches were removed from `drawHud`. The `glow`/`bold` fields stay on `HudTextConfig` (chart JSON round-trip preserved) but are now no-ops. Any future glow effect should go through the post-process bloom path, not multi-pass `AddText` offsets.
