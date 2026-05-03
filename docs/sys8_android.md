---
name: Android APK Packaging System
description: Complete APK build pipeline — isolated Android target, GLFW stub, SDK setup, one-click Build APK button in ProjectHub
type: project
---

# Android APK Packaging System (updated 2026-04-09 after Round 5)

**Why:** Engine targets Android phones. Game creators need to export their projects as installable APKs.

**How to apply:** The Android build is completely isolated from the desktop build. Only `ProjectHub.h/.cpp` was modified (Build APK button). All other existing files are untouched.

## Architecture

```
Existing desktop code (UNTOUCHED)
├── engine/src/engine/Engine.cpp        ← GLFW, Win32, unchanged
├── engine/src/renderer/vulkan/*        ← GLFW surface, unchanged
├── engine/src/main.cpp                 ← desktop entry, unchanged
└── CMakeLists.txt                      ← desktop build, unchanged

Android-only code (NEW)
├── engine/src/android/
│   ├── AndroidVulkanContext.cpp         ← VK_KHR_android_surface (replaces VulkanContext.cpp)
│   ├── AndroidSwapchain.cpp            ← ANativeWindow dimensions (replaces Swapchain.cpp)
│   ├── AndroidEngine.h/.cpp            ← Game loop, touch input, ImGui HUD
│   ├── AndroidFileIO.h/.cpp            ← AAssetManager for reading bundled assets
│   ├── android_main.cpp                ← NativeActivity entry point
│   ├── stubs/GLFW/glfw3.h             ← Stub so shared headers compile without real GLFW
│   └── CMakeLists.txt                  ← Builds libmusicgame.so

Gradle Project
├── android/
│   ├── app/build.gradle.kts            ← AGP 8.5, compileSdk 36, NDK r27c, CMake
│   ├── app/src/main/AndroidManifest.xml ← NativeActivity, landscape, Vulkan required
│   ├── settings.gradle.kts             ← Aliyun mirrors for China network
│   ├── gradle/wrapper/                 ← Gradle 8.7 wrapper
│   └── local.properties                ← Points to Android Studio SDK

Build Tools
├── tools/setup_android_sdk.bat         ← Downloads SDK/NDK (BITS for China network)
└── tools/build_apk.bat                ← Bundles assets + shaders, runs Gradle, copies APK
```

## Key Design Decisions

1. **GLFW stub approach:** `engine/src/android/stubs/GLFW/glfw3.h` provides type declarations (`GLFWwindow`, key constants, function stubs) so shared code compiles on Android. The stub directory is added to CMake include path FIRST, before the real GLFW path.

2. **Link-time substitution:** `AndroidVulkanContext.cpp` and `AndroidSwapchain.cpp` implement the SAME classes (`VulkanContext`, `Swapchain`) with Android-specific internals. The Android CMake compiles these INSTEAD of `VulkanContext.cpp` and `Swapchain.cpp`.

3. **VMA Vulkan 1.0 restriction:** Android API 24 only guarantees Vulkan 1.0. CMake defines `VMA_VULKAN_VERSION=1000000` to prevent VMA from calling Vulkan 1.1/1.3 functions.

4. **Asset pipeline:** `build_apk.bat` copies project assets + compiled `.spv` shaders into `android/app/src/main/assets/`. AudioEngine on Android extracts audio files to internal storage (miniaudio needs file paths).

5. **China network:** `dl.google.com` is blocked via curl/TLS but works through Windows BITS/PowerShell WebClient. Gradle uses Aliyun maven mirrors. SDK installed via Android Studio (winget) + manual PowerShell downloads.

## Build APK Flow (from ProjectHub UI)

1. User clicks **"Build APK"** next to a project in ProjectHub
2. **Save dialog** opens (defaults to Desktop, filename = `ProjectName.apk`)
3. User picks save location → build starts in background thread
4. Progress shown: "Building APK..." → "BUILD SUCCESSFUL!" or "BUILD FAILED"
5. **"Open Folder"** button opens Explorer with APK selected

## SDK Components Installed

| Component | Version | Path |
|---|---|---|
| Android Studio | 2025.3.3.6 | C:\Program Files\Android\Android Studio |
| SDK | 36 | C:\Users\wense\AppData\Local\Android\Sdk |
| NDK | r27c (27.2.12479018) | .../Sdk/ndk/27.2.12479018 |
| Build Tools | 37.0.0 | .../Sdk/build-tools/37.0.0 |
| Platform Tools | latest | .../Sdk/platform-tools |
| CMake | 3.22.1 | .../Sdk/cmake/3.22.1 |

## APK Contents

- `lib/arm64-v8a/libmusicgame.so` — compiled engine
- `lib/arm64-v8a/libc++_shared.so` — C++ runtime
- `assets/shaders/*.spv` — 10 SPIR-V shaders
- `assets/assets/audio/` — music files
- `assets/assets/charts/` — chart JSON files (pruned to selected mode — see below)
- `assets/project.json`, `music_selection.json`, `start_screen.json`

## Packaging-time chart prune (2026-04-17)

**Why:** The editor keeps one chart file per (song, mode, difficulty) so the author can freely switch between DropNotes/Circle/ScanLine without losing prior work — `<song>_drop3d_hard.json`, `<song>_scan_hard.json`, etc. all coexist on disk. Shipping every stale mode in the APK bloats the asset bundle. Rule: **in the package, each song keeps only its currently-selected mode** (the files referenced by `music_selection.json::sets[].songs[].chartEasy|Medium|Hard`). The live editor project under `Projects/<name>/` is never modified.

**Where:** `engine/src/ui/ProjectHub.cpp`

- `stageProjectForPackaging(projectRoot, safeName)` creates `%TEMP%/<safeName>_apk_stage_<ts>/`, selectively copies `project.json`, `start_screen.json`, `music_selection.json`, and `assets/` (recursive) into it, then calls `prunePackagedCharts(staging)`.
- `prunePackagedCharts(stagingRoot)` parses `<staging>/music_selection.json`, collects the set of song names + referenced chart basenames, walks `<staging>/assets/charts/`, and deletes any `<songName>_*.json` not in the keep set. Files not prefixed with a known song name (`demo.json`, etc.) are left alone.
- `collectKeepSet` is the helper that reads the three `chartEasy/Medium/Hard` fields per song and normalises paths to basenames.
- `ProjectHub::startApkBuild` now builds the staging path, passes it to `build_apk.bat` in place of the live project path (falls back to the live path if staging fails), and stores it in `m_apkStagingPath`.
- `ProjectHub::renderApkDialog` removes the staging directory via `fs::remove_all` as soon as the async build future resolves — success or failure.
- `build_apk.bat` itself was left unchanged; the prune is invisible to the shell script.

## Android Launch Crash Fix (2026-04-05)

APK was built successfully but crashed immediately on phone. Four issues fixed:

### 1. Unsupported composite alpha (primary crash cause)
`AndroidSwapchain.cpp` hardcoded `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR`. Many Android devices don't support INHERIT. **Fix:** query `caps.supportedCompositeAlpha` and pick the first supported flag (prefer OPAQUE → INHERIT → PRE_MULTIPLIED → POST_MULTIPLIED).

### 2. No exception handling in android_main()
`android_main.cpp` had no try-catch. Any Vulkan init failure threw `std::runtime_error` that propagated into native activity runtime — silent crash, no logcat. **Fix:** wrap entire `android_main()` body in try-catch, log fatal errors.

### 3. Unsupported HDR scene format
`Renderer::init()` hardcoded `VK_FORMAT_R16G16B16A16_SFLOAT` for the offscreen scene framebuffer. Many mobile GPUs don't support RGBA16F as color attachment. **Fix:** in `Renderer.cpp`, query `vkGetPhysicalDeviceFormatProperties()` and fall back to `VK_FORMAT_R8G8B8A8_UNORM`.

### 4. Song list never parsed
`AndroidEngine::loadProject()` read `music_selection.json` but never parsed it — `m_songs` was always empty, music selection screen showed nothing. **Fix:** implemented lightweight JSON parser (no nlohmann dependency) that extracts song entries with name, artist, audio, chart paths, and gameMode config.

### Additional hardening in onWindowInit()
- Null check on `ANativeWindow*` parameter
- try-catch around `m_renderer.init()`
- Error check on `vkCreateDescriptorPool()` for ImGui

## Second round of fixes (2026-04-05)

### 5. ImGui DisplaySize never set on Android
No platform backend (no ImGui_ImplGlfw) means `ImGui::GetIO().DisplaySize` stays (0,0). **Fix:** manually set DisplaySize and DeltaTime each frame before `ImGui::NewFrame()`.

### 6. Bloom compute shaders crash on mobile
`PostProcess` unconditionally created compute pipelines for bloom. RGBA16F with STORAGE_IMAGE not supported on many mobile GPUs → null pipeline handles used → crash. **Fix:** check format support before creating bloom resources; added `m_bloomEnabled` flag; skip bloom pass if unavailable. Shader load failures caught instead of thrown.

### 7. Missing permissions & theme
Manifest had zero `<uses-permission>`. Added `MODIFY_AUDIO_SETTINGS` for miniaudio. Added `android:theme` (Material) so crash dialog can render. Set NativeActivity `exported="true"`.

### 8. Crash dialog system
Added `LauncherActivity.java` as the new launcher. Native code writes crash message to `crash.txt` on exception. On next launch, LauncherActivity reads the file and shows AlertDialog with the error before retrying.

### Non-android files also modified
- `engine/src/renderer/Renderer.cpp` — format fallback for scene framebuffer (R16G16B16A16_SFLOAT → R8G8B8A8_UNORM)
- `engine/src/renderer/PostProcess.cpp` + `PostProcess.h` — bloom skip flag, format check, compute pipeline error handling

## Third round of fixes (2026-04-08) — APK now launches

### 9. `undefined symbol: ANativeActivity_onCreate`
Native library failed to load. `ANativeActivity_onCreate` lives in the `native_app_glue` static lib but is only resolved at runtime via `dlsym()` by Android's NativeActivity, so the linker stripped it as unused. **Fix:** wrap `native_app_glue` with `--whole-archive` in `engine/src/android/CMakeLists.txt` to force-export all symbols.

### 10. VMA assertion `vulkanApiVersion >= VK_API_VERSION_1_2`
`BufferManager.cpp` hardcoded `ai.vulkanApiVersion = VK_API_VERSION_1_2`, conflicting with the Android-only define `VMA_VULKAN_VERSION=1000000`. **Fix:** added `#if` guard in `BufferManager.cpp` — passes `VK_API_VERSION_1_0` when `VMA_VULKAN_VERSION` is restricted, `VK_API_VERSION_1_2` otherwise. Must use raw numeric `4198400` in the `#if` (the `VK_API_VERSION_1_2` macro contains a `(uint32_t)` cast invalid in preprocessor).

## Round 4 (2026-04-09) — Round 3 fix #10 had regressed

A fresh `Projects/test/test.apk` rebuilt on 2026-04-09 crashed on launch with the **exact same** VMA assertion as Round 3 #10. Inspection of `engine/src/renderer/vulkan/BufferManager.cpp:12` showed the documented `#if` guard was **not in the source** — only in the doc. Either reverted, never committed, or only ever documented. Re-applied the fix on 2026-04-09 with the form:

```cpp
#if defined(VMA_VULKAN_VERSION) && VMA_VULKAN_VERSION < 4198400
    ai.vulkanApiVersion = VK_API_VERSION_1_0;
#else
    ai.vulkanApiVersion = VK_API_VERSION_1_2;
#endif
```

Tombstone confirmed crash chain: `VmaAllocator_T+1124` → `vmaCreateAllocator+204` → `BufferManager::init+128` → `Renderer::init+228` → `AndroidEngine::onWindowInit+456`.

**Important gotcha — `crash.txt` dialog does NOT catch native aborts:** VMA failure is `assert()` → `abort()` → `SIGABRT`. The try-catch in `android_main.cpp` only catches C++ exceptions, so `crash.txt` is never written and `LauncherActivity` has nothing to display on next launch. To diagnose VMA-class crashes you must use `adb logcat` and look for `tombstoned` lines. Future hardening: add `sigaction(SIGABRT/SIGSEGV)` handler in `android_main.cpp`.

**Meta-lesson:** the doc claimed Round 3 was complete when the code did not match. Always grep the actual source before trusting a "fixed" status — including this memory file. Re-verify `BufferManager.cpp` before assuming the VMA guard is still in place.

### Status: ✅ LAUNCHES SUCCESSFULLY (as of 2026-04-09 after Round 4 re-fix)
Verified on Samsung Galaxy S23 (Android 15). Native code reaches MusicSelection (logcat shows `Project loaded with 2 songs` → `AndroidEngine initialized` → `APP_CMD_INIT_WINDOW` → AdrenoVK init). Full Gameplay → Results flow not yet re-verified after Round 4.

## Round 5 (2026-04-09) — Game flow gap + landscape orientation

After the Round 4 launch unblock, two on-device issues:

### 5a. StartScreen state was missing entirely
`AndroidEngine::GameScreen` only had `MusicSelection / Gameplay / Results`. The desktop flow `StartScreen → MusicSelection → Gameplay` was reduced to `MusicSelection → ...` on Android, skipping the title screen.

**Fix:** added `StartScreen` to the enum, made it the initial state. New `loadStartScreen()` parses `start_screen.json` for `logo.text` and `tapText` via the existing tiny `jsonString` helper (no external JSON dep). New `renderStartScreen()` draws title at 30% Y (yellow, 3.5x), tap prompt at 80% Y (white, 2.0x), full-window `ImGui::InvisibleButton` advances to MusicSelection on tap. Background image / SFX deferred — would need to wire `TextureManager::loadFromFile` through `AndroidFileIO::extractToInternal`.

### 5b. NativeActivity ignores manifest screenOrientation when launched via Intent
LauncherActivity launches NativeActivity via `Intent`. Stock `android.app.NativeActivity` doesn't reliably apply manifest `screenOrientation` in the Intent-launched path — orientation is set *after* the native window exists, so the swapchain captures portrait dimensions.

**Fix:** new `android/app/src/main/java/com/musicgame/player/MainActivity.java` subclasses `NativeActivity`, calls `setRequestedOrientation(SCREEN_ORIENTATION_SENSOR_LANDSCAPE)` **before** `super.onCreate()`. Also enables `FLAG_KEEP_SCREEN_ON` and immersive sticky fullscreen. Manifest now points at `.MainActivity` with `resizeableActivity="false"` and expanded `configChanges`. `LauncherActivity.launchGame()` targets `MainActivity.class`.

### 5c. Window resize handler
Even with the orientation lock, INIT_WINDOW could fire during the rotation transition. Added `APP_CMD_WINDOW_RESIZED` / `APP_CMD_CONFIG_CHANGED` / `APP_CMD_CONTENT_RECT_CHANGED` handlers in `android_main.cpp`, all routing to new `AndroidEngine::onWindowResize()` which does `vkDeviceWaitIdle` → `m_renderer.onResize(nullptr)` → `Swapchain::recreate`.

### 5d. Surface transform / extent swap (the actual fix for portrait rendering)
**This is the critical insight.** On Android, `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` returns `currentExtent` in the **device's native orientation** (portrait for phones), regardless of activity orientation. It reports `currentTransform = ROTATE_90` or `ROTATE_270` to say "the compositor will rotate this for you." Apps must either:

- (a) render rotated and pass `preTransform = currentTransform` through, OR
- (b) swap the extent to match displayed orientation and pass `preTransform = IDENTITY`.

`AndroidSwapchain.cpp::chooseExtent` was doing **neither** — it used `caps.currentExtent` raw and passed `caps.currentTransform` through. Result: portrait swapchain images, which the system then rotated again — effectively double-rotation that displayed portrait content in a landscape window.

**Fix:** in `chooseExtent`, if `caps.currentTransform & (ROTATE_90 | ROTATE_270)`, swap `width` ↔ `height`. Belt-and-braces: if height > width after that, swap again (forced landscape). Set `preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` when supported. Added `LOGI` lines to print raw extent, transform bits, and final extent so future iterations have ground truth.

**Key gotcha to remember:** Android Vulkan surface dimensions are NOT the same as displayed window dimensions. Always check `currentTransform`. The old code's assumption that "swap dimensions only on swapchain recreate" doesn't work because the buffer is always native-orientation regardless.

### Files touched in Round 5
| File | Change |
|---|---|
| `engine/src/android/AndroidEngine.h` | `StartScreen` enum, init state, new method declarations + start screen text fields |
| `engine/src/android/AndroidEngine.cpp` | `loadStartScreen()` / `renderStartScreen()` / `onWindowResize()`, switch case, init() wiring |
| `engine/src/android/android_main.cpp` | New `APP_CMD_WINDOW_RESIZED`/`CONFIG_CHANGED`/`CONTENT_RECT_CHANGED` handlers |
| `engine/src/android/AndroidSwapchain.cpp` | Extent swap on rotation transform, IDENTITY preTransform, diagnostic logging |
| `android/app/src/main/AndroidManifest.xml` | `.MainActivity` replaces stock NativeActivity |
| `android/app/src/main/java/.../MainActivity.java` | NEW — NativeActivity subclass forcing landscape pre-onCreate |
| `android/app/src/main/java/.../LauncherActivity.java` | Intent targets MainActivity.class |

### Status: 🟡 IN PROGRESS (as of 2026-04-09 11:45)
StartScreen renders correctly with title + tap prompt. Orientation lock at OS level confirmed (system bars are landscape). Round 5d swapchain extent fix landed in build but not yet visually validated on device — if portrait persists, the new `LOGI chooseExtent: caps.currentExtent=AxB, currentTransform=0xN` lines will tell us what the Adreno driver actually reports.

Authoritative full doc: `C:/Users/wense/Music_game/ANDROID_PACKAGING.md`.

## Round 6 (2026-05-02) — Material/shader pipeline drift + first-pass visual port

After 3 weeks of desktop work (Material Phase 1–4 + Custom shaders + Editor layout overhaul), the Android target stopped building / launching cleanly. Three regressions surfaced in one APK build attempt — all the same root cause: **new desktop `.cpp` files / shader files / extraction whitelists were never mirrored to the Android pipeline**. Fixed in this order, each unblocking the next:

### 6a. Native link errors — Material/Shader sources missing from Android CMake

The desktop build globs renderer sources via the top-level `CMakeLists.txt`. The Android target lists them explicitly in `engine/src/android/CMakeLists.txt`. When Material Phase 1–4 added five new `.cpp` files to `engine/src/renderer/`, the explicit list was never updated. Result on `gradlew :app:buildCMakeDebug`:

```
ld.lld: error: undefined symbol: compileFragmentToSpv(std::filesystem::path const&, bool)
    >>> referenced by MeshRenderer.cpp:230, QuadBatch.cpp:293
ld.lld: error: undefined symbol: resolveMaterial(ChartData::MaterialData const&, MaterialAssetLibrary const*)
    >>> referenced by Bandori/Arcaea/Lanota/CytusRenderer::onInit
... + parseKind, loadMaterialAsset, saveMaterialAsset, getMaterialSlotsForMode, materialModeName, materialSlotSlug
```

**Fix:** appended five files to `SHARED_RENDERER_SOURCES` in `engine/src/android/CMakeLists.txt`:

```cmake
${ENGINE_SRC}/renderer/ShaderCompiler.cpp
${ENGINE_SRC}/renderer/Material.cpp
${ENGINE_SRC}/renderer/MaterialAsset.cpp
${ENGINE_SRC}/renderer/MaterialAssetLibrary.cpp
${ENGINE_SRC}/renderer/MaterialSlots.cpp
```

**Latent risk worth flagging:** `ShaderCompiler.cpp` calls `glslc.exe` via `_popen`. **glslc does not ship in NDK**. The function is currently only reachable from `QuadBatch::getOrBuildCustomPipeline` / `MeshRenderer::getOrBuildCustomPipeline`, which only fire if a chart selects `MaterialKind::Custom`. Android-shipped material assets must stay on the precompiled kinds (Unlit/Glow/Scroll/Pulse/Gradient) until shader generation moves to runtime SPIR-V (or `spirv-cross` in-process). Known limitation, not a current crash — would crash the moment a Custom material is selected on device.

### 6b. Runtime crash — Android shader-extraction whitelist stale

After 6a linked, the APK crashed at first frame with:
```
Renderer init failed: Cannot open shader: /data/.../files/shaders/quad_unlit.frag.spv
```

Per-kind quad fragment shaders (`quad_unlit/glow/scroll/pulse/gradient.frag.spv`) were added in Material Phase 1+2 alongside the corresponding `mesh_*.frag.spv` files. The `mesh_*` ones were added to `AndroidEngine::onWindowInit`'s `shaderFiles[]` extraction list at the time, but the matching `quad_*` ones were missed. The build script copies all `.spv` files into `assets/shaders/`, so they were *in* the APK — just never extracted to internal storage on first launch. `Pipeline::loadShaderModule` reads via `std::ifstream` from `m_shaderDir`, which is the internal-storage path, so the missing extracts mean missing files at read time.

**Fix:** added the five files to `AndroidEngine::onWindowInit`'s `shaderFiles[]` array (`AndroidEngine.cpp:80`).

**Lesson worth keeping:** the Android extraction whitelist, the desktop CMake glob, and the `tools/build_apk.bat` shader-copy step are three separate gates. Adding any new shader requires touching at least the first two. Consider a future cleanup: replace the explicit whitelist with a pre-build script that emits it from the `build/shaders/` directory.

### 6c. "Tap to Start" dead — touch events never reached ImGui

After 6b, the start screen rendered but tapping did nothing. `AndroidEngine::onTouchEvent` injected events into `m_input` (the engine's gesture system) but never touched `ImGuiIO`. ImGui's `InvisibleButton` hit-test reads `io.MousePos` / `io.MouseDown[0]`, which were both pinned at zero/false forever.

**Fix:** in `onTouchEvent`, mirror the primary pointer into ImGui via the modern queued-event API:
- `ACTION_DOWN/POINTER_DOWN`: `io.AddMousePosEvent(px,py)` + `io.AddMouseButtonEvent(0,true)`
- `ACTION_UP/POINTER_UP`: `io.AddMousePosEvent(px,py)` + `io.AddMouseButtonEvent(0,false)`
- `ACTION_MOVE`: `io.AddMousePosEvent(px,py)`
- `ACTION_CANCEL`: `io.AddMouseButtonEvent(0,false)`

Single-pointer mirror is sufficient because the editor UI flows are mouse-driven by design (the engine's multi-touch path stays connected to gameplay via `m_input`). Thread safety isn't a concern: `ALooper_pollOnce` and `source->process` (which dispatches `app->onInputEvent`) run on the same thread as the render loop in native_app_glue.

### 6d. First-pass visual port — start screen + music selection

After 6a–6c the APK launches and is interactive again, but the visuals diverged drastically from the desktop test-game. Both the start screen and music selection were stub UIs (text-only title, ImGui `Selectable` list of songs). User feedback: **"I want to see the effect like the test game in the real game! The package system needs to pack a complete game!"**

Built a foundation pass that delivers partial visual parity:

**Texture pipeline on Android (`AndroidEngine`)**
- New `loadAssetTexture(assetPath)` helper: lazy `extractToInternal` → `TextureManager::loadFromFile(ctx, bufMgr, fsPath)` → `ImGui_ImplVulkan_AddTexture(sampler, view, layout)` → `ImTextureID`. Cached by asset path; `Texture` structs retained in `m_loadedTextures` for `releaseTextures()` cleanup on `onWindowTerm`. Returns null + logs error on missing/decode failure.
- ImGui descriptor pool bumped from 32 → 128 (background + badges + N song covers).
- `onWindowTerm` now calls `releaseTextures()` before `ImGui_ImplVulkan_Shutdown`.

**Theme (`applyTheme`)**
- Near-black panels, cyan primary (hover), magenta active — matches the desktop editor pass from 2026-04-23.
- Rounded corners (8 px windows, 6 px frames/buttons).

**Start screen — full visual rewrite**
- Background image read from `start_screen.json::background.file`, drawn aspect-fill via `ImGui::GetBackgroundDrawList()->AddImage()` with a 90-alpha darken.
- Title rendered through `ImFont::CalcTextSizeA()` at the JSON-driven `fontSize` × `position{x,y}` (normalized) × `color`.
- Tap prompt at JSON-driven `tapTextPosition` × `tapTextSize`, with a sin-driven alpha pulse for life.
- Full-window `InvisibleButton` (now wired via 6c) advances to MusicSelection.
- New parser fields in `loadStartScreen`: `background.file`, `logo.position{x,y}`, `logo.fontSize`, `logo.color[4]`, `logo.imageFile`, `logo.type` (text/image discriminator), `tapTextPosition{x,y}`, `tapTextSize`. Helper `jsonNumArray` added for the color RGBA.

**Music selection — partial visual rewrite**
- Top-level `background`, `fcImage`, `apImage` paths now parsed in `loadProject`.
- Per song: `coverImage` + strongest-difficulty `achievement{Easy/Medium/Hard}` reduced to single `achievement` field on `SongEntry`.
- Header bar with "Select a Song" title.
- Song cards as a 4-across rectangle grid (NOT the desktop's rhombus carousel — see Open Issue below). Each card: aspect-fill cover image, gradient strip at bottom for name/artist, top-right yellow score, top-left FC/AP badge if earned, cyan ring on selected, tap-to-select via `InvisibleButton`.
- SETTINGS / PLAY in a footer row.

### Files touched in Round 6
| File | Change |
|---|---|
| `engine/src/android/CMakeLists.txt` | +5 sources in SHARED_RENDERER_SOURCES (Material/Shader subsystem) |
| `engine/src/android/AndroidEngine.h` | +ImVec2/ImVec4 start-screen state, +music-selection asset paths, +`m_imguiTextures` cache, +`m_loadedTextures` ownership, +`loadAssetTexture/releaseTextures/applyTheme` declarations, `SongEntry` gains `coverImage`/`achievement` |
| `engine/src/android/AndroidEngine.cpp` | +`<cfloat>/<cmath>/<cstdio>` includes, ImGui touch-event mirror in `onTouchEvent`, +`jsonNumArray`, full rewrite of `loadStartScreen`/`loadProject`/`renderStartScreen`/`renderMusicSelection`, +`loadAssetTexture/releaseTextures/applyTheme/aspectFillUV` helpers, descriptor pool 32→128, theme + texture-cleanup hooks |

### Status — 🟡 IN PROGRESS

What works:
- APK builds and launches.
- Start screen shows the configured background image, title (JSON font/position/color), pulsing tap prompt. Tap advances.
- Music selection shows page background with frosted overlay, song cards with cover art + scores + badges. Tap card → select. PLAY → gameplay.
- Settings page reachable. Gameplay path unchanged from earlier rounds.

What's still drifted from the desktop test-game (= the open Round 7 work):
- **Music selection layout is wrong shape.** Desktop has a 3-pane perspective layout (set wheel left, song wheel right with rhombus skewed cards + painter-sorted z-order, big cover photo center, difficulty buttons under the cover, large PLAY at the bottom of the center column). Android currently shows a flat 4-across rectangle grid.
- **Set hierarchy collapsed.** `m_songs` is a flat list; the desktop uses `m_sets[].songs[]` with separate set + song wheels.
- **No 30s audio preview** on song dwell.
- **Logo glow / bold / image-logo** parsed but not rendered (text-only logo path).
- **No transition fades** between StartScreen → MusicSelection.

### Open: Path A migration to Round 7

User direction: rather than continuing to replicate the editor's render code in `AndroidEngine.cpp` (which drifts every editor change), **share the source files**. Add `StartScreenEditor.cpp` + `MusicSelectionEditor.cpp` to the Android CMake target so `renderGamePreview()` runs identically on both platforms.

Cost estimate: ~2–3 hours, spread across:

1. **Stub `Engine` for Android.** The editors call `m_engine->isTestMode()`, `audioEngine()`, `imguiLayer()`, `musicSelectionEditor()`, `testTransitioning()`, etc. Build a thin adapter — likely `AndroidEngineAdapter` exposing the same surface area, owned by `AndroidEngine`, returning `true`/no-ops where appropriate.
2. **Gate desktop-only deps with `#ifndef ANDROID`** in `StartScreenEditor.cpp` / `MusicSelectionEditor.cpp`: AI clients (`ShaderGenClient`, `AIEditorConfig`), file dialogs (`commdlg.h`, `shellapi.h`), `GLFW/glfw3native.h`. The `_WIN32` blocks already cover most of it; need to verify each `#include` and confirm no top-level body-level calls leak through.
3. **Provide texture upload path the editors expect.** They call `getThumb()` / `getCoverDesc()` which route through `m_textureManager + m_bufferManager`. Already plumbed via `m_renderer.textures()` + `m_renderer.buffers()` on Android — needs an `Engine::textureManager()` adapter accessor.
4. **`ImGuiLayer` optional.** Editors call `m_imgui->getLogoFont(size)` for non-default fonts. Android has no ImGuiLayer. Make `m_imgui` nullable and fall back to `ImGui::GetFont()` when null (already how `renderGamePreview` is written — `m_imgui ? m_imgui->getLogoFont(...) : ImGui::GetFont()`).
5. **Remove first-pass replicas** from `AndroidEngine.cpp` once the shared editors are wired — `renderStartScreen` becomes a thin caller into `StartScreenEditor::renderGamePreview`, `renderMusicSelection` into `MusicSelectionEditor::renderGamePreview`. Keep the texture cache + theme + JSON parse helpers since they're shared.
6. **Wire transitions + audio preview.** Once the renders are shared, the existing `testTransitionTo` / 30s preview logic comes for free — just needs the AndroidEngine adapter to expose `audioEngine()` + the transition state hooks.

Path A is the right architectural answer because it eliminates drift permanently — every future visual change in the editor automatically appears in the APK. Path B (continue replicating in `AndroidEngine.cpp`) was rejected explicitly: "the package system needs to pack a complete game! not a test document!"

**Resume point:** start with step 1 (`AndroidEngineAdapter` shim). The texture-loading plumbing from 6d is the foundation Path A will reuse, so 6d wasn't wasted — it stays as the back-end that the shared editor calls into.

---

## Round 7 (2026-05-03) — Path A migration shipped: structural game/editor split

Path A landed end-to-end across nine sub-phases (A–H) plus a handed-off Phase I (on-device verification). Critically, the user reframed the goal mid-plan: **the Android binary is the shippable phone game, not a port of the editor**. That reframing rejected the Round-6-resume plan's strategy of compiling `StartScreenEditor.cpp` + `MusicSelectionEditor.cpp` into the Android lib with `#ifdef` gates. Class names like `StartScreenEditor` are interface declarations — putting the file into the Android target leaks editor identity into the player binary even if the body is gated. The right cut is **structural**: player-facing rendering lives in **game-side classes** under `engine/src/game/screens/`; editors **compose** (here, *inherit*) those classes.

### Two firm rules (user-stated 2026-05-03)

1. **Strip everything engine-developer-only.** The Android binary contains only what a player uses to play: start screen, music selection, gameplay, results. No editor, no Project Hub, no file dialogs, no AI agents (Copilot/Autocharter/Audit/Style/ShaderGen), no chart editor, no material editor, no test-mode toggles, no developer overlays, no glslc/runtime SPIR-V compiler, no project import.
2. **Everything the game needs ships inside the APK.** Songs, charts, covers, audio, fonts, backgrounds, precompiled SPIR-V shaders, material assets — all bundled at build time. The player never connects to a dev machine, never imports content. Eager unpack at first launch materializes everything from APK assets to internal storage.

### New game-side player views

`engine/src/game/screens/` now hosts four shared classes consumed by both desktop editor wrappers and the Android player loop:

| Class | Responsibility |
|---|---|
| `StartScreenView` | Background (image/GIF/none), text-or-image logo with glow + bold + position + scale, tap prompt, transition state. Owns its own JSON `load`/`save` and Vulkan texture lifecycle. `renderGamePreview(origin, size)`. |
| `MusicSelectionView` | Sets/songs hierarchy (`m_sets`), selection indices, smooth-scroll lerp state, cover descriptor cache, page background, FC/AP badges, audio-preview dwell timer. `update(dt, IPlayerEngine*)` drives lerp + preview. `renderGamePreview(origin, size, IPlayerEngine*)`. Helper render methods (`renderSetWheel`, `renderSongWheel`, `renderCoverPhoto`, `renderDifficultyButtons`, `renderPlayButton`) are `protected` so the editor's editor-side preview pane can reuse them. |
| `GameplayHudView` | Stateless. `render(displaySize, IPlayerEngine&)` reads `score()`, `gameplayConfig()` and draws score/combo panels with HudTextConfig-driven positioning, scale, glow, bold. |
| `ResultsView` | Stateless. `render(displaySize, IPlayerEngine&)` reads `score()` + `judgment()` and draws score / max combo / Perfect-Good-Bad-Miss breakdown / Back button. |

`SongInfo`, `MusicSetInfo`, `Difficulty` types moved from `engine/src/ui/MusicSelectionEditor.h` into `engine/src/game/screens/MusicSelectionView.h` since they're game-side data. `MusicSelectionEditor.h` includes the view header for the types.

### IPlayerEngine — the abstraction point

New `engine/src/engine/IPlayerEngine.h` is the contract between views and engine. Pure-virtual accessors: `audio()`, `renderer()`, `clock()`, `playerSettings()`, `materialLibrary()`, `inputManager()`, `imguiLayer()` (nullable — Android returns `nullptr`), `score()`, `judgment()`, `hitDetector()`, `activeMode()`, `gameplayConfig() const`. State queries: `isTestMode() const`, `isTestTransitioning() const`, `testTransProgress() const`. Player flow: `launchGameplay(SongInfo, Difficulty, projectPath, autoPlay)`, `exitGameplay()`. Two implementations: desktop `Engine : public IPlayerEngine` (existing accessors marked `override`); Android `AndroidEngineAdapter : public IPlayerEngine` (delegates to `AndroidEngine&`).

`Engine::isTestMode` returns `m_testMode` (true when devs play inside the editor); `AndroidEngineAdapter::isTestMode` returns **false** (phone players are not in test mode — the resume doc's `true` was overridden after the user clarified phone is the actual product).

### Editor classes inherit views

`StartScreenEditor : public StartScreenView` and `MusicSelectionEditor : public MusicSelectionView`. The editors keep their authoring chrome (asset browser, thumbnail cache, AI shader gen, materials panel, status messages, dialog state, panel split ratios) but inherit the player-facing fields as `protected`. Editor sidebars mutate inherited fields directly — no field renames in editor methods. Two `virtual` hooks let editors extend view behaviour without bleeding editor symbols into the view: `StartScreenView::load` (editor overrides to clear thumbnails first); `MusicSelectionView::onSongCardDoubleClick` (editor overrides to open SongEditor; default is no-op).

`MusicSelectionEditor.cpp` shrunk from 2240 → ~1175 lines. `StartScreenEditor.cpp` lost its `renderGamePreview` body and texture loaders to the view.

### AndroidEngineAdapter

`engine/src/android/AndroidEngineAdapter.{h,cpp}` is a `final` `IPlayerEngine` implementer wrapping `AndroidEngine&`. Accessors delegate (`m_engine.m_audio`, `m_engine.m_renderer`, `m_engine.m_score`, etc.). `imguiLayer()` returns `nullptr`. `launchGameplay(SongInfo, Difficulty, projectPath, autoPlay)` matches the song against `AndroidEngine::m_songs` by name + audio file and dispatches through the existing index-based `startGameplay(int)` (acceptable interim — the duplicate `m_songs` will retire once on-device verification passes). `exitGameplay` delegates. Transition state stored locally on the adapter. AndroidEngine declares `friend class AndroidEngineAdapter` so the adapter can reach private members. The adapter `.cpp` is in `engine/src/android/CMakeLists.txt::ANDROID_SOURCES`.

### AndroidEngine wiring

AndroidEngine.h gained four view members (`m_startView`, `m_musicView`, `m_hudView`, `m_resultsView`) plus `AndroidEngineAdapter m_adapter{*this}` and a `bool m_viewsReady` lazy-init flag. New `MaterialAssetLibrary m_materialLibrary` member (default-constructed; populated later if needed). The four `render*Screen`/`render*HUD` bodies are now thin wrappers:

- `renderStartScreen()` — first call: `m_startView.initVulkan(m_renderer.context(), m_renderer.buffers(), nullptr)` + `m_startView.load(m_assetsPath)`. Every call: opens an `##startscreen` no-decoration window, calls `m_startView.renderGamePreview(origin, displaySz)`, then a full-window `InvisibleButton("##startTap")` advances `m_screen = GameScreen::MusicSelection`.
- `renderMusicSelection()` — same lazy-init pattern, then `m_musicView.update(dt, &m_adapter)` (smooth-scroll lerp + default-selection + audio preview dwell), then `m_musicView.renderGamePreview(origin, displaySz, &m_adapter)`.
- `renderGameplayHUD()` — keeps the Android-only background-dim overlay + FPS counter (`m_playerSettings.fpsCounter`), then `m_hudView.render(displaySz, m_adapter)`.
- `renderResultsHUD()` — `m_resultsView.render(ImGui::GetIO().DisplaySize, m_adapter)`. Retry button is a Phase G+ todo (needs `IPlayerEngine::requestRetry`).

The Round 6 first-pass replicas (~180 lines of bg + title + tap + 4-across grid + footer buttons) are deleted.

### Eager asset unpack

`AndroidEngine::init` ends with an eager-unpack block (after `loadStartScreen` / `loadProject` / `loadPlayerSettingsFile`):

1. `AndroidFileIO::extractToInternal("music_selection.json")` and `extractToInternal("start_screen.json")` so the JSON files themselves are on disk where `View::load` can `std::ifstream` them.
2. A small string-walking lambda scans every `"key": "value"` pair in both JSONs, treats values that look like relative file paths (contain `/`, contain `.`, no `\`, exist via `AndroidFileIO::exists`) as assets, and calls `extractToInternal` on each.
3. `m_assetsPath = AndroidFileIO::internalPath()` (trailing slash trimmed) so the views' lazy `load(m_assetsPath)` finds the JSONs.

`extractToInternal` is idempotent — re-launches stat-and-skip files that already exist.

### Phone rendering polish

**DPI scaling (Phase H.1).** `AndroidEngine::onWindowInit`, after `applyTheme()`: `AConfiguration_getDensity(m_app->config)` → `dpiScale = density / 160.0f` (clamped `[1.0, 4.0]` — floor at mdpi, sanity cap at xxxhdpi+). ImGui font atlas rebuilt with `cfg.SizePixels = 13.0f * dpiScale`. `ImGui::GetStyle().ScaleAllSizes(dpiScale)` scales padding/rounding/scrollbar/grab. Logged.

**Touch slop (Phase H.2).** `engine/src/input/TouchTypes.h::TAP_SLOP_PX` and `SLIDE_SLOP_PX` converted from `inline constexpr float` to `inline float` so they're runtime-mutable. New `inline void scaleByDpi(float)` multiplies them in-place. `AndroidEngine::onWindowInit` calls `TouchThresholds::scaleByDpi(dpiScale)`. The `*_S` and `*_VELOCITY` constants stay `constexpr` (dimensionally density-independent).

### Decisions logged separately

- **`GameplayLauncher` extraction (Phase E) descoped.** `Engine::launchGameplay` and `AndroidEngine::startGameplay` only *look* duplicate; they actually diverge for valid platform reasons (`openProject`/`m_sceneViewer`/`EditorLayer` vs. `extractToInternal`/`GameScreen`). The shared sequence (chart load → renderer create → judgment/score reset → clock setup → applyPlayerSettings) would need ~10 currently-private methods plumbed through `IPlayerEngine` to lift; not worth it for a desktop+phone pair. `IPlayerEngine::launchGameplay` is the cleavage point. See memory `project_gameplay_launcher_deferred.md`.

### Files touched in Round 7

| File | Change |
|---|---|
| `engine/src/engine/IPlayerEngine.h` | NEW — pure-virtual interface for player-facing engine surface. |
| `engine/src/engine/Engine.h` | `: public IPlayerEngine`; new accessors (score/judgment/hitDetector/activeMode/gameplayConfig/imguiLayer); `launchGameplay`/`exitGameplay` `override`; `m_hudView`/`m_resultsView` members. |
| `engine/src/engine/Engine.cpp` | `renderGameplayHUD` reduced to scene-texture compositing + `m_hudView.render(...)` + pause/results dispatch; `renderResultsOverlay` reduced to `m_resultsView.render(...)`. |
| `engine/src/game/screens/StartScreenView.{h,cpp}` | NEW — extracted from `StartScreenEditor`. |
| `engine/src/game/screens/MusicSelectionView.{h,cpp}` | NEW — extracted from `MusicSelectionEditor`; types `SongInfo`/`MusicSetInfo`/`Difficulty` moved here. |
| `engine/src/game/screens/GameplayHudView.{h,cpp}` | NEW — extracted from `Engine::renderGameplayHUD`. |
| `engine/src/game/screens/ResultsView.{h,cpp}` | NEW — extracted from `Engine::renderResultsOverlay`. |
| `engine/src/ui/StartScreenEditor.{h,cpp}` | `: public StartScreenView`; player-facing fields + `renderGamePreview` body removed; `load()` overrides view's; `loadBackground/loadLogoImage` calls renamed to `reloadBackground/reloadLogoImage`. |
| `engine/src/ui/MusicSelectionEditor.{h,cpp}` | `: public MusicSelectionView`; ~1065 lines of player-facing code removed; `load()` overrides view's; `render(Engine*)` calls `update(dt, engine)`; `onSongCardDoubleClick(int)` override opens SongEditor. |
| `engine/src/ui/GameFlowPreview.cpp` | `renderGamePreview(origin, size)` → `renderGamePreview(origin, size, engine)` for the music-selection preview. |
| `engine/src/ui/GifPlayer.{h,cpp}` | `load()` signature drops `ImGuiLayer&`; uses `ImGui_ImplVulkan_AddTexture` directly. |
| `engine/src/input/TouchTypes.h` | `TAP_SLOP_PX`/`SLIDE_SLOP_PX` runtime-mutable + `scaleByDpi(float)`. |
| `engine/src/android/AndroidEngine.{h,cpp}` | View + adapter members; eager-unpack block in `init`; DPI scaling + touch slop scaling in `onWindowInit`; four `render*` bodies rewritten as view delegations; `MaterialAssetLibrary m_materialLibrary` member; legacy `m_songs` etc. kept as fallback for adapter's `launchGameplay`. |
| `engine/src/android/AndroidEngineAdapter.{h,cpp}` | NEW — `final` `IPlayerEngine` implementer wrapping `AndroidEngine&`. |
| `engine/src/android/CMakeLists.txt` | `AndroidEngineAdapter.cpp` added to `ANDROID_SOURCES`. |
| `CMakeLists.txt` | Root: `engine/src/game/screens/*.cpp` added to the `GLOB_RECURSE` list. |

### Status — 🟡 IN PROGRESS (handed off for on-device verification)

What landed (verified by desktop build + smoke test, exit 0 each phase):
- All four player views compile cleanly.
- Editor inheritance preserves desktop UX — start screen + music selection visually identical pre/post extraction.
- `IPlayerEngine` virtual dispatch works for both `Engine` (desktop) and (statically) `AndroidEngineAdapter`.
- DPI scaling logic is in place; touch slop adjusts at init.
- Eager unpack walks both JSONs and extracts referenced assets.

What needs the user's hands (Phase I):
1. Build APK: `cd android && ./gradlew assembleRelease`. May surface NDK-side compile errors I couldn't catch from this Windows shell.
2. Symbol audit: `nm -D libmusicgame.so | grep -E '(StartScreenEditor|MusicSelectionEditor|SongEditor|SettingsEditor|ProjectHub|browseFile|httplib|ShaderGen|AIEditor|glslc|_popen)'` must be empty. Any match means an editor symbol leaked into the player binary.
3. `adb install -r app/build/outputs/apk/release/app-release.apk` and walk the player loop end-to-end: start → tap → music selection → set+song wheels → 30 s preview on dwell → difficulty cycles → play → HUD shows score+combo → results panel → back returns to selection.

### Resume point for next session (Round 8 — cleanup)

Once on-device verification passes:

1. **Delete the dead Android fallback fields**: `m_songs`, `m_startBgPath`, `m_startTitleText`, `m_startTapText`, all the `m_start*Pos`/`m_start*Px`/`m_start*Color`, `m_musicBgPath`, `m_fcImagePath`, `m_apImagePath`. Shrink `loadProject` / `loadStartScreen` to just the eager-unpack walker. Adapter's `launchGameplay` should dispatch via `m_musicView.sets()` lookup instead of `m_songs`.
2. **`requestRetry` virtual on `IPlayerEngine`** so `ResultsView` can re-add the Retry button on Android. Engine implements via `restartGameplay`; AndroidEngineAdapter via re-running `startGameplay` for the current selection.
3. **Drag→wheel translation in `onTouchEvent`** so song wheels scroll under finger drag. Currently they only respond to mouse-wheel events.
4. **Transition fades** between StartScreen → MusicSelection: wire `AndroidEngineAdapter::testTransitionTo` + alpha overlay during state changes.
5. **Bloom quality flag (Phase H.3 backlog)** — `PlayerSettings::bloomQuality { Off, Low, High }` + corresponding mip-count switch in `PostProcess.cpp`. Default Low on Android.
6. **ASTC texture path (Phase H.4 backlog)** — extension-detection in `TextureManager::loadFromFile` + APK build script asset compression step.

The Round 6 work (texture cache, theme, JSON parsing helpers, descriptor pool 128, ImGui touch-event mirror) all stays in place — it's the back-end the Round 7 views call into.

## Round 7.1 — gameplay-loop fixes (2026-05-03)

First on-device pass after Round 7 surfaced three player-side bugs. None were structural — the split held — but the new Android engine loop was missing pieces that the desktop loop already had.

### 1. Combo HUD bloom (offset glow + bold)

`game/screens/GameplayHudView.cpp::drawHud` still rendered an 8-direction halo (`HudTextConfig::glow=true`) and a +1 px shadow (`bold=true`) using offset multi-pass `AddText`. On a phone with `dpiScale ≈ 3.5×` the offsets blur into one saturated blob — the unreadable gold smear over the combo number in the user's screenshot. Default `comboHud` had `glow=true`, so this fired every frame.

Fix: drop both branches; render one crisp pass. Fields stay on `HudTextConfig` (chart JSON round-trip preserved); the lambda just ignores them. Real bold needs a bold font face; real glow needs the post-process bloom path.

### 2. 3D-drop hold rendering (`ArcaeaRenderer`)

`onInit` only collected `Tap`/`Flick`/`ArcTap`/`Arc`. `NoteType::Hold` was silently dropped, so `Aa_drop3d_hard.json`'s lane-2 hold (and the lane auto-expand pass) saw nothing.

Added:
- `ArcaeaRenderer::m_holdNotes` (vector of `NoteEvent`), populated alongside taps in `onInit`.
- Lane auto-expand walks `m_holdNotes` too.
- Render loop in `onRender`: each hold uses the existing `m_tapMesh` translated to the lane center and Z-scaled by `len / (2 × hd)` (hd = `0.4`, the tap mesh's z half-depth) so it spans `[-zTail, -zHead]`. Cull: `if (zTail < 0 || zHead > 30) continue`.
- Default material: `MaterialKind::Glow`, tint `{0.3, 0.8, 1.0, 0.95}` — matches Bandori's inactive hold color.
- `onShutdown` clears `m_holdNotes`.

No new material slot was added; `Aa_drop3d_hard.json` doesn't reference one. If chart authors want per-hold materials in 3D drop, slot 12 is the next free index in `ArcaeaSlot`.

### 3. Android autoplay + active-hold sync (the dominant "holds invisible" bug)

The user's actual gameplay was 2D drop (`Aa_drop2d_hard.json`), not 3D — so the 3D fix above didn't address what they saw. The real culprit was two omissions in `AndroidEngine::update`:

**(a) `setActiveHoldIds` never called.** Without `m_activeMode->setActiveHoldIds(m_hitDetector.activeHoldIds())` each tick, `BandoriRenderer::m_activeHoldIds` stays empty. The renderer's stale-hold cull then kicks in 0.15 s past every hold's head: `if (!holdActive && m_songTime > note.time + kBadWindow) { m_hitNotes.insert(note.id); continue; }`. Every hold disappears from view.

**(b) Autoplay ignored.** `AndroidEngineAdapter::launchGameplay`'s signature was `bool /*autoPlay*/` — the parameter was discarded. `AndroidEngine` had no `m_autoPlay` field and no call to `HitDetector::autoPlayTick`. The MusicSelectionView's AUTO PLAY toggle was a no-op on Android.

The two compound: with autoplay broken, the player can't hold the line manually fast enough during the 2 s lead-in + first beats, so every hold misses its head and the cull fires; even if the user *did* press, the missing `setActiveHoldIds` call means `holdActive=false` anyway and the renderer still culls.

Fix:

- `engine/src/android/AndroidEngine.h` — `bool m_autoPlay = false`; `startGameplay(int)` → `startGameplay(int, bool autoPlay = false)`.
- `engine/src/android/AndroidEngine.cpp::startGameplay` — assigns `m_autoPlay = autoPlay`.
- `engine/src/android/AndroidEngine.cpp::update` (gameplay branch) — runs `m_hitDetector.autoPlayTick(songT)` when `m_autoPlay`, dispatches each `AutoHit` as Perfect via `m_judgment`/`m_score`/`m_activeMode->showJudgment`. After `update` + `consumeSampleTicks`, calls `m_activeMode->setActiveHoldIds(m_hitDetector.activeHoldIds())` unconditionally. Mirrors `Engine::update` line-for-line.
- `engine/src/android/AndroidEngine.cpp::restartGameplay` — captures `m_autoPlay` before `exitGameplay`, restores it via `startGameplay(idx, ap)`.
- `engine/src/android/AndroidEngineAdapter.cpp::launchGameplay` — un-discards `autoPlay`, threads it into `startGameplay(idx, autoPlay)`.

### Verification

- Desktop builds (Debug/MusicGameEngineTest) clean.
- APK: `gradlew assembleRelease` → `BUILD SUCCESSFUL`. Zipalign + apksigner with debug keystore. `adb install -r` → `Success`.
- Symbol audit re-run: zero matches for editor/HTTP/glslc symbols. Round 7 split still intact.
- App launches into start screen on emulator-5554. Player-loop on-device walk-through pending the user's manual test.

### Open from this round

- HUD bloom currently disabled at draw time but `HudTextConfig::glow` defaults stay `true` in `ProjectHub.h`. If we want a future glow path, switch to a separate post-process bloom render pass instead of resurrecting the offset halo.
- 3D-drop hold material override slot — not added; revisit when chart authors ask for per-mode hold tinting.
- `AndroidEngine::update` and `Engine::update` are now near-duplicates for the post-`activeMode` portion (autoplay tick → update sweep → sample ticks → broken holds → setActiveHoldIds → onUpdate). Worth folding into a shared helper if a third platform ever appears; for two it's fine.
