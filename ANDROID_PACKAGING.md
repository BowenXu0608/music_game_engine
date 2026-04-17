# System 8 — Android APK Packaging

**Last updated:** 2026-04-08  
**Status:** ✅ Complete (APK builds successfully; launch crash under investigation)

One-click Android APK export from the desktop editor. The Android build is completely isolated from the desktop build — only `ProjectHub.h/.cpp` was modified to add the "Build APK" button. All other existing desktop files remain untouched.

See also: [README.md](README.md) | [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md) | [CORE_ENGINE.md](CORE_ENGINE.md)

---\\
## Architecture Overview

```
Desktop code (UNTOUCHED)
├── engine/src/engine/Engine.cpp          ← GLFW, Win32, desktop main loop
├── engine/src/renderer/vulkan/*          ← GLFW surface creation
├── engine/src/main.cpp                   ← Desktop entry point
└── CMakeLists.txt                        ← Desktop build

Android-only code (NEW)
├── engine/src/android/
│   ├── CMakeLists.txt                    ← Builds libmusicgame.so (arm64-v8a)
│   ├── android_main.cpp                  ← NativeActivity entry, lifecycle, crash log
│   ├── AndroidEngine.h/.cpp              ← Game loop, touch input, ImGui HUD, state machine
│   ├── AndroidVulkanContext.cpp           ← VK_KHR_android_surface (replaces VulkanContext.cpp)
│   ├── AndroidSwapchain.cpp              ← ANativeWindow dimensions (replaces Swapchain.cpp)
│   ├── AndroidFileIO.h/.cpp              ← AAssetManager reads + extract-to-internal-storage
│   └── stubs/GLFW/glfw3.h               ← Stub so shared headers compile without real GLFW

Gradle Project
├── android/
│   ├── app/build.gradle.kts              ← AGP, compileSdk 36, NDK r27c, CMake 3.22.1
│   ├── app/src/main/AndroidManifest.xml  ← NativeActivity + LauncherActivity, Vulkan required
│   ├── app/src/main/java/.../LauncherActivity.java  ← Crash dialog on launch
│   ├── settings.gradle.kts               ← Aliyun mirrors for China network
│   ├── gradle/wrapper/                   ← Gradle 8.7 wrapper
│   └── local.properties                  ← Points to Android Studio SDK

Build Tools
├── tools/build_apk.bat                   ← Bundles assets + shaders, runs Gradle, copies APK
└── tools/setup_android_sdk.bat           ← Downloads SDK/NDK (multiple mirror fallbacks)
```

---

## Key Design Decisions

### 1. GLFW Stub Approach

`engine/src/android/stubs/GLFW/glfw3.h` provides type declarations (`GLFWwindow*`, key constants like `GLFW_PRESS`, window hint defines) so that shared engine headers (`VulkanContext.h`, `Swapchain.h`, `Renderer.h`, `InputManager.h`) compile on Android without the real GLFW library. None of the GLFW functions are actually called on Android.

The stub directory is added to CMake's include path **first**, before any real GLFW path:
```cmake
target_include_directories(musicgame PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs    # GLFW stub (must be first!)
    ...
)
```

### 2. Link-Time Substitution

`AndroidVulkanContext.cpp` and `AndroidSwapchain.cpp` implement the **same classes** (`VulkanContext`, `Swapchain`) with Android-specific internals. The Android CMake compiles these **instead of** the desktop `VulkanContext.cpp` and `Swapchain.cpp`. No `#ifdef ANDROID` in the shared code.

### 3. VMA Vulkan 1.0 Restriction

Android API 24 (minSdk) only guarantees Vulkan 1.0. CMake defines `VMA_VULKAN_VERSION=1000000` to prevent VMA from calling Vulkan 1.1/1.3 functions that don't exist on older devices.

### 4. Asset Pipeline

`build_apk.bat` copies the project's assets + compiled `.spv` shaders into `android/app/src/main/assets/`. At runtime:
- **Shaders and JSON** are read directly from `AAssetManager` via `AndroidFileIO::readString()` / `readBinary()`
- **Audio files** are extracted to internal storage first because miniaudio requires filesystem paths (`AndroidFileIO::extractToInternal()`)
- Extracted files are cached — subsequent calls skip re-extraction

### 5. China Network Workarounds

`dl.google.com` is blocked via curl/TLS in China but works through Windows BITS/PowerShell WebClient. The setup script tries multiple download sources. Gradle uses Aliyun maven mirrors in `settings.gradle.kts`.

---

## Source Files

### `android_main.cpp` — Entry Point

NativeActivity entry point. Handles the Android app lifecycle:
- Routes `APP_CMD_INIT_WINDOW` / `APP_CMD_TERM_WINDOW` to `AndroidEngine`
- Routes `AINPUT_EVENT_TYPE_MOTION` touch events to `AndroidEngine::onTouchEvent()`
- Wraps entire `android_main()` in try-catch; writes crash message to `crash.txt` on exception

### `AndroidEngine.h/.cpp` — Game Loop

Replaces the desktop `Engine` class. Manages three screens via `GameScreen` enum:

| Screen | Description |
|---|---|
| `MusicSelection` | ImGui full-screen song list with Selectable items and PLAY button |
| `Gameplay` | Active game mode renderer + combo/score HUD overlay |
| `Results` | Score, max combo, judgment breakdown, Retry/Back buttons |

Key methods:
- `init()` — Sets up GameClock, AudioEngine, InputManager, GestureRecognizer, loads project
- `onWindowInit()` — Creates Vulkan renderer, extracts shaders, initializes ImGui (no GLFW backend — manually sets `DisplaySize` and `DeltaTime`)
- `mainLoop()` — `ALooper_pollOnce()` event loop; blocks when Vulkan not ready
- `update()` — Lead-in timing, DSP sync, missed-note detection, song-end detection
- `render()` — Scene framebuffer render + ImGui HUD in swapchain pass
- `loadProject()` — Lightweight JSON parser (no nlohmann dependency) for `music_selection.json`
- `startGameplay()` — Loads chart, creates game mode renderer, sets up hit detection/scoring
- `createRenderer()` — Maps `GameModeConfig` to one of 5 renderers (Bandori, Cytus, Phigros, Arcaea, Lanota)

### `AndroidVulkanContext.cpp` — Vulkan Instance & Device

Implements `VulkanContext` for Android:
- Creates instance with `VK_KHR_ANDROID_SURFACE_EXTENSION_NAME`
- Creates surface via `vkCreateAndroidSurfaceKHR()` using a global `ANativeWindow*`
- Uses Vulkan 1.0 API version for maximum compatibility
- Queries and conditionally enables `samplerAnisotropy` (not all mobile GPUs support it)

### `AndroidSwapchain.cpp` — Swapchain

Implements `Swapchain` for Android:
- Gets dimensions from `ANativeWindow_getWidth/Height()` instead of GLFW
- **Composite alpha fix:** Queries `caps.supportedCompositeAlpha` and picks the first supported flag (OPAQUE > INHERIT > PRE_MULTIPLIED > POST_MULTIPLIED) instead of hardcoding
- Prefers SRGB surface format; falls back to `R8G8B8A8_SRGB` which is common on Android

### `AndroidFileIO.h/.cpp` — Asset Access

Namespace with static functions for APK asset access:

| Function | Purpose |
|---|---|
| `init(mgr, path)` | Store AAssetManager pointer and internal storage path |
| `readString(path)` | Read asset as string via `AAsset_read()` |
| `readBinary(path)` | Read asset as `vector<char>` |
| `extractToInternal(path)` | Copy asset to internal storage, return filesystem path (cached) |
| `exists(path)` | Check if asset exists in APK |
| `internalPath()` | Get internal storage base path |

### `stubs/GLFW/glfw3.h` — GLFW Stub

Minimal declarations: `GLFWwindow` struct, action constants (`GLFW_PRESS/RELEASE/REPEAT`), key constants (`GLFW_KEY_0` through `GLFW_KEY_9`, `GLFW_KEY_Q/W/ESCAPE`), and window hint defines. ~40 lines.

### `CMakeLists.txt` — Android Native Build

Builds `libmusicgame.so` as a shared library. Source file groups:

| Group | Contents |
|---|---|
| `ANDROID_SOURCES` | 5 Android-specific `.cpp` files |
| `SHARED_RENDERER_SOURCES` | Renderer, QuadBatch, LineBatch, MeshRenderer, ParticleSystem, PostProcess |
| `SHARED_VULKAN_SOURCES` | Pipeline, BufferManager, TextureManager, DescriptorManager, CommandManager, SyncObjects, RenderPass |
| `SHARED_GAME_SOURCES` | 5 game mode renderers + ChartLoader |
| `SHARED_GAMEPLAY_SOURCES` | HitDetector (JudgmentSystem and ScoreTracker are header-only) |
| `SHARED_CORE_SOURCES` | SceneNode |
| `SHARED_ENGINE_SOURCES` | AudioEngine |
| `SHARED_INPUT_SOURCES` | GestureRecognizer |
| `IMGUI_SOURCES` | ImGui core + Vulkan backend (no GLFW backend) |

Links: `native_app_glue`, `android`, `log`, `vulkan`

---

## Gradle Project

### `app/build.gradle.kts`

```
Package:        com.musicgame.player
compileSdk:     36
minSdk:         24
targetSdk:      34
NDK:            r27c (27.2.12479018)
CMake:          3.22.1
ABI:            arm64-v8a only
C++ Standard:   C++20
STL:            c++_shared
```

### `AndroidManifest.xml`

- **LauncherActivity** (`com.musicgame.player.LauncherActivity`) — entry point, checks for `crash.txt`
- **NativeActivity** (`android.app.NativeActivity`) — game engine, loads `libmusicgame`
- Permissions: `MODIFY_AUDIO_SETTINGS`
- Requires: Vulkan hardware level 0
- Orientation: `sensorLandscape`
- Theme: Material (so crash dialog can render)

### `LauncherActivity.java`

Java activity that acts as the launcher. On `onCreate()`:
1. Checks for `crash.txt` in internal storage (written by native code on exception)
2. If found: shows AlertDialog with crash message, offers "Retry" or "Exit"
3. If not found: immediately launches NativeActivity via Intent

---

## Build Tools

### `tools/build_apk.bat`

Usage: `build_apk.bat <project_path> [output_apk_path]`

5-step pipeline:
1. **Check SDK** — Verifies `local.properties` exists
2. **Bundle assets** — Cleans and copies `project.json`, `start_screen.json`, `music_selection.json`, and the `assets/` folder
3. **Copy shaders** — Copies compiled `.spv` files from `build/shaders/`
4. **Gradle build** — Runs `assembleDebug` via gradle-wrapper.jar
5. **Output** — Reports APK path and size; optionally copies to user-specified location

### `tools/setup_android_sdk.bat`

SDK setup with China-friendly fallbacks:
1. Checks for existing Android Studio SDK at common locations
2. If not found, tries to download command-line tools from `dl.google.com`, then `dl.google.cn`
3. If all downloads fail, prints manual installation instructions
4. Installs: platform-tools, platforms;android-34, build-tools;34.0.0, ndk;27.0.12077973
5. Writes `local.properties` with SDK path (forward slashes for Gradle)

---

## Build APK Flow (from Desktop Editor)

1. User clicks **"Build APK"** next to a project in ProjectHub
2. Save dialog opens (defaults to Desktop, filename = `ProjectName.apk`)
3. User picks save location → build starts in a background thread
4. Progress shown: "Building APK..." → "BUILD SUCCESSFUL!" or "BUILD FAILED"
5. **"Open Folder"** button appears to open Explorer with the APK selected

---

## APK Contents

```
app-debug.apk
├── lib/arm64-v8a/
│   ├── libmusicgame.so        ← Compiled engine (~all C++ code)
│   └── libc++_shared.so       ← C++ runtime
├── assets/
│   ├── shaders/*.spv          ← 10 SPIR-V shaders (quad, line, mesh, composite, bloom)
│   ├── assets/audio/          ← Music files
│   ├── assets/charts/         ← Chart JSON files
│   ├── project.json
│   ├── music_selection.json
│   └── start_screen.json
├── res/                       ← Android resources (strings, theme)
├── AndroidManifest.xml
└── classes.dex                ← LauncherActivity bytecode
```

---

## Known Issues and Fixes Applied

### Crash Fix Round 1

| # | Issue | Fix |
|---|---|---|
| 1 | Hardcoded `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR` — unsupported on many devices | Query `supportedCompositeAlpha`, pick first supported flag |
| 2 | No exception handling in `android_main()` | Wrap in try-catch, write `crash.txt` on failure |
| 3 | `VK_FORMAT_R16G16B16A16_SFLOAT` scene framebuffer unsupported on mobile | Fall back to `VK_FORMAT_R8G8B8A8_UNORM` in `Renderer.cpp` |
| 4 | `music_selection.json` read but never parsed | Implemented lightweight JSON parser in `AndroidEngine::loadProject()` |

### Crash Fix Round 2

| # | Issue | Fix |
|---|---|---|
| 5 | `ImGui::GetIO().DisplaySize` stays (0,0) — no GLFW platform backend | Manually set `DisplaySize` and `DeltaTime` each frame |
| 6 | Bloom compute shaders crash — RGBA16F STORAGE_IMAGE unsupported on mobile | Check format support; added `m_bloomEnabled` flag in `PostProcess` |
| 7 | Missing permissions and theme in manifest | Added `MODIFY_AUDIO_SETTINGS`, Material theme, `exported="true"` |
| 8 | No crash visibility | Added `LauncherActivity.java` with crash dialog system |

### Crash Fix Round 3 (2026-04-08)

| # | Issue | Fix |
|---|---|---|
| 9 | `undefined symbol: ANativeActivity_onCreate` — native library fails to load | `ANativeActivity_onCreate` is defined in the `native_app_glue` static library but only called at runtime via `dlsym()` by Android's `NativeActivity`. The linker stripped it as unused. **Fix:** wrap `native_app_glue` with `--whole-archive` in `engine/src/android/CMakeLists.txt` to force all symbols to be exported |
| 10 | VMA assertion: `vulkanApiVersion >= VK_API_VERSION_1_2` but preprocessor macros restrict to 1.0 | `BufferManager.cpp` hardcoded `ai.vulkanApiVersion = VK_API_VERSION_1_2`, conflicting with the Android CMake define `VMA_VULKAN_VERSION=1000000` (Vulkan 1.0). **Fix:** added `#if` guard in `BufferManager.cpp` — passes `VK_API_VERSION_1_0` when `VMA_VULKAN_VERSION` is restricted, `VK_API_VERSION_1_2` otherwise. Uses raw numeric value `4198400` instead of `VK_API_VERSION_1_2` macro in the preprocessor comparison (the macro expands to a `(uint32_t)` cast, invalid in `#if`) |

### Cross-Platform Files Modified

These desktop files were also modified to support Android compatibility:
- `engine/src/renderer/Renderer.cpp` — HDR format fallback (R16G16B16A16_SFLOAT → R8G8B8A8_UNORM)
- `engine/src/renderer/PostProcess.cpp` + `PostProcess.h` — Bloom skip flag, format check, compute pipeline error handling
- `engine/src/renderer/vulkan/BufferManager.cpp` — VMA Vulkan API version conditional (1.0 on Android, 1.2 on desktop)
- `engine/src/android/CMakeLists.txt` — `--whole-archive` for native_app_glue to export `ANativeActivity_onCreate`

### Current Status

APK builds and launches successfully on device (Samsung Galaxy S23, Android 15). No more native crashes. App reaches `NativeActivity` and runs the Vulkan renderer. Minor `AHardwareBuffer` allocation warnings for pixel format 0x3b (driver format probing, non-fatal).

---

## SDK Components

| Component | Version | Path |
|---|---|---|
| Android Studio | 2025.3.3.6 | `C:\Program Files\Android\Android Studio` |
| SDK | 36 | `C:\Users\wense\AppData\Local\Android\Sdk` |
| NDK | r27c (27.2.12479018) | `.../Sdk/ndk/27.2.12479018` |
| Build Tools | 37.0.0 | `.../Sdk/build-tools/37.0.0` |
| Platform Tools | latest | `.../Sdk/platform-tools` |
| CMake | 3.22.1 | `.../Sdk/cmake/3.22.1` |
