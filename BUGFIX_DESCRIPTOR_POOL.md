# Bug Fix: Vulkan Descriptor Pool Exhaustion Crash

**Date:** 2026-04-08  
**Status:** Fixed  
**Affected area:** `engine/src/ui/ImGuiLayer.cpp`  
**Symptom:** Users cannot enter the Music Selection page — the application crashes with a segfault immediately after navigating from the Start Screen Editor.

---

## 1. Symptom

After opening a project and clicking **"Next: Music Selection >"** (editor mode) or tapping the start screen (test mode), the application crashes instantly. The Vulkan validation layer reports:

```
vkUpdateDescriptorSets(): pDescriptorWrites[0] Invalid VkDescriptorSet Object 0xcccccccccccccccc.
```

`0xcccccccccccccccc` is MSVC's Debug-mode fill for **uninitialized memory**, confirming the descriptor set was never successfully allocated.

---

## 2. Root Cause

The ImGui Vulkan backend uses a single `VkDescriptorPool` to allocate descriptor sets for every texture displayed in the UI (backgrounds, logos, thumbnails, cover images, etc.). The pool was created with a hard limit:

```cpp
// ImGuiLayer.cpp  (BEFORE fix)
pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 } };
pool_info.maxSets = 32;
```

**32 sets** were consumed as follows:

| Consumer | Approx. sets used |
|---|---|
| ImGui internal (font atlas) | 1 |
| Engine scene texture | 1 |
| StartScreenEditor background | 1 |
| StartScreenEditor logo | 0-1 |
| StartScreenEditor asset thumbnails | ~10-20 (one per image/audio in `assets/`) |
| **Total before navigation** | **~15-25** |

When the user navigates to the MusicSelectionEditor, it scans the same `assets/` directory and starts loading **its own set of thumbnails** via `getThumb()` -> `TextureManager::loadFromFile()` -> `ImGuiLayer::addTexture()`. Each call allocates another descriptor set from the same pool. Once the pool crosses 32 allocations, `vkAllocateDescriptorSets` **fails** and the output `VkDescriptorSet` remains **uninitialized** (`0xCC...`). The subsequent `vkUpdateDescriptorSets` on that garbage handle triggers the validation error and segfault.

### Why it only appeared recently

Earlier project states had fewer asset files in `assets/`. As more textures and audio files were added (background JPG, audio MP3, chart JSONs for set B), the total descriptor count crossed the 32-set threshold right at the MusicSelectionEditor transition.

---

## 3. Investigation Process

1. **Code review of navigation logic** — Traced both navigation paths (editor-mode button click and test-mode transition) through `StartScreenEditor::render()` -> `Engine::switchLayer()` -> `MusicSelectionEditor::render()`. The navigation logic itself was correct.

2. **Added debug logging** — Inserted `std::cout` traces at the navigation trigger, after `load()`, and at the first `render()` of MusicSelectionEditor. All confirmed the navigation executed successfully and data loaded correctly (2 sets, loaded=true).

3. **Ran the application** — The debug output showed successful navigation followed immediately by:
   ```
   [Vulkan] vkUpdateDescriptorSets(): Invalid VkDescriptorSet Object 0xcccccccccccccccc
   Segmentation fault
   ```

4. **Identified the uninitialized pattern** — `0xcccccccccccccccc` is MSVC's debug fill for uninitialized stack/heap memory, pointing to a failed `vkAllocateDescriptorSets` call.

5. **Found the pool limit** — Checked `ImGuiLayer::init()` and found `maxSets = 32`. Counted the descriptor consumers across all editors and confirmed they exceed 32 when both StartScreenEditor and MusicSelectionEditor are active.

---

## 4. Fix

**File:** `engine/src/ui/ImGuiLayer.cpp` (lines 12-19)

```cpp
// BEFORE (broken)
VkDescriptorPoolSize pool_sizes[] = {
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 }
};
pool_info.maxSets = 32;

// AFTER (fixed)
VkDescriptorPoolSize pool_sizes[] = {
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 }
};
pool_info.maxSets = 256;
```

256 sets provides ample headroom for:
- 3 editor screens (StartScreen, MusicSelection, SongEditor), each with up to ~50 thumbnails
- Cover images, backgrounds, logos
- Future growth

---

## 5. Verification

After the fix, the application:
- Navigates from StartScreen to MusicSelection without crashing
- Renders all asset thumbnails, set/song wheels, and cover placeholders correctly
- Remains stable (confirmed via `tasklist` — process running at 510 MB, no crash)

---

## 6. Additional Finding: Duplicate ChartTypes.h

During investigation, discovered two divergent copies of `ChartTypes.h`:

| File | HoldData definition |
|---|---|
| `engine/src/game/chart/ChartTypes.h` (internal, used by all compiled code) | `{ float laneX; float duration; }` |
| `engine/include/MusicGameEngine/ChartTypes.h` (public header, not compiled) | `{ vector<HoldWaypoint> waypoints; ... }` |

This is an **ODR violation waiting to happen** if any external consumer includes the public headers alongside internal code. Currently harmless since the public headers are not included by any compiled translation unit, but should be reconciled in a future refactor.
