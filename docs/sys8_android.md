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
- `assets/assets/charts/` — chart JSON files
- `assets/project.json`, `music_selection.json`, `start_screen.json`

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
