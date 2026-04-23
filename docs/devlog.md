# Music Game Engine — Dev Log

Day-by-day development log, reconstructed from git history and the dated notes in `docs/MEMORY.md`, `docs/sys1_rendering.md` through `docs/sys8_android.md`. From 2026-04-14 forward, entries are written at the end of each working session.

---

## 2026-03-19 — Project kickoff

First commit. The shape of the repo was decided on day one: a C++20 / Vulkan engine split into eight subsystems (rendering, resource management, core engine, input, gameplay, game-mode plugins, editor UI, Android packaging). The overall architecture the project still follows today was laid down here:

- **Two-layer rendering:** a low-level Vulkan backend (`VulkanContext`, `Swapchain`, `RenderPass`, `Pipeline`, `BufferManager`, `DescriptorManager`, `CommandManager`, `SyncObjects`, `TextureManager`) under a batcher layer (`QuadBatch`, `LineBatch`, `MeshRenderer`, `ParticleSystem`, `PostProcess`) owned by a single `Renderer` that exposes `whiteView() / whiteSampler() / descriptors()` to game-mode plugins. Game modes never allocate Vulkan resources directly.
- **Frames-in-flight = 3**, MAX_QUADS = 8192, MAX_LINES = 4096, ring-buffered 2048 particles — sizing picked up front so we'd never have to re-architect batchers when chart density grew.
- **Game mode as a plugin interface:** `GameModeRenderer` with `onInit / onResize / onUpdate / onRender / onShutdown / getCamera / showJudgment`. Engine creates renderers through a `createRenderer(GameModeConfig)` factory, so adding a new rhythm-game flavour is "implement the interface and wire it in the factory."
- **ECS + SceneGraph** for entities, `Transform` with TRS + quaternion rotation, `GameClock` as a header-only wall-clock-with-DSP-override. `NOMINMAX` guard before `<windows.h>` in `Engine.cpp` because min/max macros are a known Windows foot-gun.
- **Third-party dependencies frozen:** VMA for GPU memory, stb_image for textures, GLFW for window/surface, GLM for math, glslc from the Vulkan SDK for shader compilation.

## 2026-03-21 — Perspective projection + rendering pipeline debugging

The initial rendering pass had a few real issues to hunt down before anything looked right on screen:

- **BandoriRenderer and LanotaRenderer switched to perspective projection.** The earlier orthographic approach flattened depth and killed the sense of speed. `Camera` became a unified ortho + perspective header with `makePerspective(fovDeg, aspect, near, far)`. Discovered along the way that BandoriRenderer's eye must be at `z ≥ 8` from the hit zone — any closer and notes pop in with wrong apparent size. This is now written into `sys1_rendering.md` as a permanent rule.
- **`near`/`far` collided with Windows macros.** Classic Win32 gotcha — those names come from `<windows.h>` and silently break any variable that happens to share them. Renamed in `BandoriRenderer` so the preprocessor would leave them alone.
- **Subpass dependency bug.** The render pass only declared `EXTERNAL → 0`, so the post-process pass read stale/undefined data from the scene framebuffer. Added the reverse `0 → EXTERNAL` dependency. Now written into `sys1_rendering.md` as "PostProcess needs BOTH dependencies" so future additions don't repeat it.
- **Vsync toggle** added so we could A/B frame-pacing vs tearing during debug.
- **`RENDERING_PLAN.md`** expanded with per-step implementation notes — the rendering system had enough sharp edges that we wanted a single doc to trace decisions.

## 2026-03-27 — Editor overlay

First pass of an in-engine editor. Unity-style layout overlaid on the scene via ImGui: sidebar + inspector + scene view. This is the scaffold that would eventually become `ProjectHub → StartScreen → MusicSelection → SongEditor`, but on day one it was just "the engine now has an editable mode, not only a fixed test scene."

## 2026-03-29 — Repo housekeeping

Merged an external fork branch; cleaned up the initial-commit state of the git history. Not glamorous, but the remote and the local tree needed to agree before real feature work could resume.

## 2026-04-03 — Editor content pipeline

A content-pipeline sprint. Until this day the editor loaded a single hardcoded chart; this is where the editor became an actual **authoring tool**:

- **Music selection system** — a wheel/list of songs driven by `music_selection.json`, with cover art, artist, and game-mode metadata per entry.
- **Song editor scaffolding** — the DAW-style layout started taking shape: left sidebar for song info / audio / game mode / config / assets, right pane for scene preview + chart timeline + waveform strip + transport.
- **Asset panels** — thumbnails for textures and audio, browsable in the sidebar.
- **Full game-flow preview:** `StartScreen → MusicSelection → SongEditor → gameplay` as a single navigable tree. This is the flow users still see today.

The editor layer is explicitly `EditorLayer: ProjectHub → StartScreen → MusicSelection → SongEditor → (TestGame process)` — each layer is a self-contained ImGui panel. Test Game runs as a **separate process** via `CreateProcessW` so that the editor window stays interactive while gameplay is being tested in a child window.

### Design decisions locked in today

A lot of the day was spent aligning the UI with the reference games the user wanted to emulate. These are the calls that got made and then stayed made:

- **Music selection wheel = Arcaea-style card stacks with perspective tilt**, not flat lists or circular carousels. Implemented with `AddQuadFilled` / `AddImageQuad` plus skew transforms and painter's-order sorting. Reference point: a screenshot from Arcaea's song select.
- **Hierarchy panel = far-right, 70/30 split** — always visible alongside the preview. Rejected alternatives: bottom panel, toggle overlay.
- **Song card content = name + score + achievement badge.** Difficulty lives *under* the cover photo, not on the card itself — the card stays clean and difficulty is a separate interaction.
- **Cover picker = both file dialog AND drag-drop.** The rule is "always provide both Browse + `BeginDragDropTarget`" on any file-path field. Maximum flexibility: quick drag from the asset panel *or* precise file picker.
- **Song editor layout = preview on top, assets (left) + config/audio (right) on the bottom.** Preview is prominent; controls live underneath. Chart paths were removed from properties because charts are edited in the Editor tab, not the inspector.
- **Game mode is per-song, not per-project.** `GameModeConfig` lives inside `SongInfo` and is persisted per-song in `music_selection.json`. This is what lets different songs within the same project use different game styles — a required feature, not a stretch goal.
- **2D vs 3D DropNotes.** Both are *perspective* highways with converging lanes — neither is top-down. The difference is that 3D has **two judge lines** at different heights (ground track + elevated "Sky Input" line, Arcaea-style), while 2D has one. The Sky Input line is the key 3D differentiator. Clarified against an Arcaea screenshot the same day.
- **Test Game = global top-right button on every editor page**, replacing the earlier per-page "Game Preview" tab. "Test Game" means testing the *whole* game end-to-end, not previewing a single page. Rule: transition effects in the preview must read the exact `TransitionEffect` configured in Start Screen Editor properties — hardcoded effects were rejected outright after the user caught the mismatch.
- **All editor pages share the asset system.** Thumbnails, drag-drop, import — consistent across StartScreen, MusicSelection, SongEditor. A single `importAssetsToProject()` in `AssetBrowser.h` routes every import, so assets imported on one page show up on every other page.
- **All "Open File" dialogs default to the "All Files" filter**, not Images or Audio. This came out of a real bug report: the user couldn't find `.mp3` files because the dialog defaulted to an Images filter.

## 2026-04-05 — Charts, DAW layout, Madmom, HUD, Android v1

A long day across multiple systems:

### ChartLoader complete
The unified chart format (`.json` with `"version"` field) landed in full. `ChartLoader` auto-detects between UCF and legacy formats (Bandori JSON, Phigros `.pec`/`.pgr`, Arcaea `.aff`, Cytus `.xml`, Lanota `.lan`). Note variants include `TapData`, `HoldData` (with multi-waypoint paths + transition styles: Straight / Angle90 / Curve / Rhomboid), `FlickData`, `ArcData`, `PhigrosNoteData`, `LanotaRingData`. Holds are modelled primarily as `vector<HoldWaypoint>` with multi-lane paths; a legacy `endLaneX + transition` fallback is preserved. All `findValue` / `getVal` lambdas in the JSON parser have bounds checks on whitespace skips and quote finds — malformed JSON should never read out of bounds.

### DAW-style editor layout
The editor became a proper DAW layout: scene preview on top, draggable splitter, chart timeline underneath with toolbar + difficulty selector + note rows, waveform strip at the bottom, transport bar at the very bottom (Back / Save / Test / Play / Pause). Right-click deletes (note first, marker fallback). Per-difficulty notes/markers via `m_diffNotes` / `m_diffMarkers`. Audio playback controls wired into the nav bar. Waveform always visible regardless of scene/timeline split.

### Madmom beat detection
`AudioAnalyzer.h/.cpp` shells out to `tools/analyze_audio.py`, which runs the Madmom neural-network beat detector. Output feeds an "Analyze Beats" toolbar button that populates `m_bpmChanges` and auto-drops difficulty-appropriate markers — Easy = downbeats, Medium = all beats, Hard = beats + onsets. Exported into UCF as `timing.bpm_changes`.

### Gameplay HUD
`HudTextConfig` per element (score, combo). Position, font, color, glow, bold — all editor-configurable. Rendered via `Engine::renderGameplayHUD` using `ImGui::GetForegroundDrawList` so HUD text always sits on top of the scene.

### Android launch crash fix — round 1 and round 2
The Android APK from earlier in the week was actually **building** but crashing immediately on phone. Four root causes were fixed same day:

1. **Unsupported composite alpha.** `AndroidSwapchain.cpp` hardcoded `VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR`; many Android devices don't support INHERIT. Fix: query `caps.supportedCompositeAlpha` and pick the first supported flag, preferring OPAQUE → INHERIT → PRE_MULTIPLIED → POST_MULTIPLIED.
2. **No exception handling in `android_main()`.** Any Vulkan init failure propagated as a silent native crash with no logcat line. Wrapped the entire `android_main` body in try-catch with a fatal-error log.
3. **Unsupported HDR scene format.** `Renderer::init` hardcoded `VK_FORMAT_R16G16B16A16_SFLOAT` for the offscreen scene framebuffer — not supported as a color attachment on many mobile GPUs. Fix: `vkGetPhysicalDeviceFormatProperties()` and fall back to `VK_FORMAT_R8G8B8A8_UNORM` when needed.
4. **Song list never parsed.** `AndroidEngine::loadProject` read `music_selection.json` but never parsed it, so `m_songs` was always empty and the music-selection screen appeared blank. Implemented a lightweight zero-dependency JSON parser that extracts name, artist, audio, chart paths, and game-mode config.

Plus hardening: null checks on `ANativeWindow*`, try-catch around `m_renderer.init()`, error check on `vkCreateDescriptorPool()` for ImGui.

Then round 2 (still same day):

- **ImGui `DisplaySize` never set on Android.** No ImGui platform backend (no `ImGui_ImplGlfw`) means `ImGui::GetIO().DisplaySize` stays `(0,0)` and nothing draws. Manually set `DisplaySize` + `DeltaTime` before every `ImGui::NewFrame`.
- **Bloom compute shaders crash on mobile.** `PostProcess` unconditionally created bloom compute pipelines; RGBA16F as STORAGE_IMAGE isn't supported on many mobile GPUs, so pipeline handles came back null and the first bloom dispatch crashed. Added an `m_bloomEnabled` flag that checks format support up front; shader load failures are now caught, not thrown.
- **Missing Android permissions + theme.** Manifest had zero `<uses-permission>`. Added `MODIFY_AUDIO_SETTINGS` for miniaudio. Added `android:theme` so the crash dialog can actually render. Set NativeActivity `exported="true"`.
- **Crash dialog system.** New `LauncherActivity.java` as the app launcher. Native code writes crash messages to `crash.txt` on exception; on next launch, LauncherActivity reads the file and shows an AlertDialog with the error before retrying. Pragmatic self-reporting for a platform where getting logs off-device is painful.

## 2026-04-08 — BPM dynamics, Vulkan pool exhaustion, Android round 3

Three unrelated fires on the same day:

### Dynamic BPM detection
Charts can now contain mid-song BPM changes, read from UCF's `timing.bpm_changes` array into multiple `TimingPoint` entries. All timing-dependent code (hold ticks, scan-line period, editor waveform) now honours segmented BPM. `AudioAnalyzer` exports them through the same Madmom pipeline as the initial-beat detection.

### Vulkan descriptor-pool exhaustion
Navigating StartScreen → MusicSelection was crashing with `Invalid VkDescriptorSet 0xCCCC...` — the tell-tale MSVC uninitialized-memory fill pattern. Root cause: the ImGui descriptor pool was sized for only **32** `COMBINED_IMAGE_SAMPLER` sets, and `StartScreenEditor`'s thumbnails exhausted it before `MusicSelectionEditor` could allocate its own. Fix: grew the pool to **256** sets (font atlas, scene texture, ~50 thumbnails per editor layer, cover images, backgrounds — 256 is comfortably above the realistic worst case). Documented in `sys1_rendering.md` so nobody quietly reverts it.

### Android round 3 — APK actually launches
Two more Android fixes — this is the round that finally got the APK past the native-library load step:

- **`undefined symbol: ANativeActivity_onCreate`.** Android's `NativeActivity` resolves this symbol at runtime via `dlsym()`, which means the linker sees it as unused and strips it. Fix: wrap `native_app_glue` with `--whole-archive` in `engine/src/android/CMakeLists.txt` to force-export all symbols.
- **VMA assertion `vulkanApiVersion >= VK_API_VERSION_1_2`.** `BufferManager.cpp` hardcoded `VK_API_VERSION_1_2`, but Android's VMA build was restricted to `VMA_VULKAN_VERSION=1000000` (Android API 24 only guarantees Vulkan 1.0). Fix: `#if` guard on `VMA_VULKAN_VERSION < 4198400` selects `VK_API_VERSION_1_0` for Android, keeps `VK_API_VERSION_1_2` for desktop. Small gotcha: you must use the raw numeric value `4198400` in the `#if`, not the `VK_API_VERSION_1_2` macro, because that macro contains a `(uint32_t)` cast that's invalid in preprocessor context.

## 2026-04-09 — Android rounds 4 and 5

### Round 4 — the VMA fix had regressed
A fresh APK rebuild crashed on launch with **the exact same** VMA assertion Round 3 had supposedly fixed. Inspection of `BufferManager.cpp` showed the `#if` guard was **not actually in the source** — only in the doc. Either it had been reverted, never committed, or only ever documented in the first place. Re-applied the fix in the intended form:

```cpp
#if defined(VMA_VULKAN_VERSION) && VMA_VULKAN_VERSION < 4198400
    ai.vulkanApiVersion = VK_API_VERSION_1_0;
#else
    ai.vulkanApiVersion = VK_API_VERSION_1_2;
#endif
```

Tombstone confirmed the crash chain: `VmaAllocator_T+1124` → `vmaCreateAllocator+204` → `BufferManager::init+128` → `Renderer::init+228` → `AndroidEngine::onWindowInit+456`.

Two important lessons from this round, written into `sys8_android.md` so they don't get forgotten:

1. **The `crash.txt` dialog does NOT catch native aborts.** VMA failure is `assert()` → `abort()` → `SIGABRT`. The try-catch in `android_main.cpp` only catches C++ exceptions, so `crash.txt` is never written and `LauncherActivity` has nothing to display on next launch. To diagnose VMA-class crashes you must use `adb logcat` and look for `tombstoned` lines. A future hardening pass should install `sigaction(SIGABRT/SIGSEGV)` handlers in `android_main.cpp`.
2. **Meta-lesson:** docs claimed Round 3 was complete when the code did not match. Always grep the actual source before trusting a "fixed" status — including inside memory/notes files.

After re-applying the fix the APK launched cleanly on a Samsung Galaxy S23 (Android 15). Native code reached MusicSelection with `Project loaded with 2 songs` → `AndroidEngine initialized` → `APP_CMD_INIT_WINDOW` → AdrenoVK init.

### Round 5 — game-flow gap + landscape orientation
Two on-device issues remained:

**5a. StartScreen was missing from the Android game flow.** `AndroidEngine::GameScreen` only had `MusicSelection / Gameplay / Results` — the desktop flow `StartScreen → MusicSelection → Gameplay` had been reduced to `MusicSelection → ...` on Android, skipping the title screen entirely. Fix: added a `StartScreen` enum value, made it the initial state, wrote `loadStartScreen()` to parse `start_screen.json` for `logo.text` and `tapText` via the existing tiny `jsonString` helper (no external JSON dep). `renderStartScreen()` draws the title at 30% Y in yellow 3.5× scale, the tap prompt at 80% Y in white 2.0× scale, with a full-window `ImGui::InvisibleButton` that advances to MusicSelection on tap.

**5b. NativeActivity ignores manifest `screenOrientation` when launched via Intent.** Because `LauncherActivity` launches `NativeActivity` through an `Intent`, stock `android.app.NativeActivity` doesn't reliably apply manifest `screenOrientation` in that code path — orientation is set *after* the native window already exists, so the swapchain captures portrait dimensions. Fix: new `MainActivity.java` that subclasses `NativeActivity` and calls `setRequestedOrientation(SCREEN_ORIENTATION_SENSOR_LANDSCAPE)` **before** `super.onCreate()`. Also enables `FLAG_KEEP_SCREEN_ON` and immersive sticky fullscreen. Manifest now points at `.MainActivity` with `resizeableActivity="false"`, and `LauncherActivity.launchGame()` targets `MainActivity.class`.

**5c. Window resize handler.** Even with the orientation lock, `INIT_WINDOW` could still fire during the rotation transition. Added `APP_CMD_WINDOW_RESIZED` / `APP_CMD_CONFIG_CHANGED` / `APP_CMD_CONTENT_RECT_CHANGED` handlers in `android_main.cpp`, all routed to a new `AndroidEngine::onWindowResize()` that does `vkDeviceWaitIdle` → `m_renderer.onResize(nullptr)` → `Swapchain::recreate`.

**5d. Surface transform / extent swap — the actual fix for portrait rendering.** This was the non-obvious one. On Android, `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` returns `currentExtent` in the **device's native orientation** (portrait for phones), regardless of activity orientation. It reports `currentTransform = ROTATE_90` or `ROTATE_270` to say "the compositor will rotate this for you." Apps must either (a) render rotated and pass `preTransform = currentTransform` through, or (b) swap the extent to match displayed orientation and pass `preTransform = IDENTITY`. `AndroidSwapchain::chooseExtent` was doing **neither** — it used `caps.currentExtent` raw and passed `caps.currentTransform` through, producing portrait swapchain images that the system then rotated *again*, effectively double-rotating content. Fix: swap `width ↔ height` when `caps.currentTransform & (ROTATE_90 | ROTATE_270)`, force landscape with a second swap if height > width after that, set `preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` when supported. Added `LOGI` lines printing raw extent, transform bits, and final extent so future debugging has ground truth.

The lesson written into the docs: **Android Vulkan surface dimensions are NOT the same as displayed window dimensions** — always check `currentTransform`. The old code's assumption that "swap dimensions only on swapchain recreate" doesn't work because the buffer is always native-orientation regardless.

## 2026-04-10 — Circle mode input wiring + cross-lane holds

Three unrelated-but-important fixes:

### Circle mode input wiring
Two input paths now converge on the same `markNoteHit` + particle-burst feedback, so touch and keyboard score identically:

- **Touch:** `pickNoteAt(screenPx, songTime, dp(48))` projects each note through `m_perspVP` and picks the nearest one within fingertip tolerance (~7.6mm at 160dpi) and a ±0.15s timing window. Then `consumeNoteById` validates timing. Visual feedback fires at the note's exact disk position, not the touch point.
- **Keyboard 1–7:** `checkHit(lane, songTime)` takes the lane-based path. `showJudgment(lane, j)` reverse-maps `targetAngle = PI/2 - lane * (2PI / trackCount)`, calls `findNoteByAngle` to locate the note on the disk, and fires the particle burst at the disk position — so keyboard hits also look like they landed on the note.

### Game-mode factory fix
A latent bug caught during this audit: `GameModeType::Circle` had been wired to `CytusRenderer`, and `ScanLine` to `PhigrosRenderer`. Circle charts were rendering as scan-line, and scan-line charts were running through Phigros's judgment-line code. Fixed the factory so `Circle → LanotaRenderer` and `ScanLine → CytusRenderer`. This had gone unnoticed because end-to-end testing of these two modes was new.

### Test Game button unification
Three different Test Game buttons existed — one on StartScreen, one on MusicSelection, one on SongEditor — and they'd drifted apart. All three now route through `Engine::spawnTestGameProcess()`, which spawns a child process for gameplay. The editor window stays fully interactive because gameplay runs out-of-process.

### Cross-lane holds + Bandori-style sample-tick gating
The big feature of the day. Holds can now span multiple lanes with a per-segment transition style. Data model in `ChartTypes.h`:

- `HoldWaypoint { tOffset, lane, transitionLen, style }` with styles `Straight / Angle90 / Curve / Rhomboid`.
- `HoldSamplePoint { tOffset }` — authored tick checkpoints.
- `evalHoldLaneAt(hold, tOffset)` — inline helper that walks waypoints or falls back to the legacy single-transition path.
- `holdActiveSegment(hold, tOffset)` — returns the active waypoint segment index at a given time.

Input gating: `updateHoldLane` fires on every `SlideMove` gesture. `consumeSampleTicks` fires at each authored sample point, compares `evalHoldLaneAt`'s expected lane against the current touch lane, and scores Perfect on match or Miss on mismatch. Two consecutive missed ticks break the hold and `consumeBrokenHolds()` returns the IDs for touch cleanup.

## 2026-04-11 — Scan Line end-to-end + Android packaging

### Scan Line mode full rebuild
Scan line mode was previously a stub — today it got the full end-to-end treatment in `CytusRenderer`: note layout with x/y scan coordinates, page system, judge line, touch-based hit detection via `pickNoteAt`, spatial picker with ±0.18s timing gate, zigzag hold-body rendering for holds that span page turns, scale/fade approach animation (0.30 → 1.0 scale, 0.25 → 1.0 alpha). Notes are shown only during their sweep page; multi-sweep holds span multiple pages. The editor side grew a companion authoring layout — see the next day for the details that landed during the big push.

### Android APK packaging pipeline finalized
The Build APK flow from `ProjectHub` landed in its final form:

1. User clicks **"Build APK"** next to a project in ProjectHub.
2. A **save dialog** opens, defaulting to Desktop with filename `ProjectName.apk`.
3. User picks a location → build starts in a background thread.
4. Progress UI shows `Building APK...` → `BUILD SUCCESSFUL!` or `BUILD FAILED`.
5. An **Open Folder** button opens Explorer with the APK selected.

The Android build is completely isolated from the desktop build: only `ProjectHub.h/.cpp` is shared. `engine/src/android/` contains Android-only source (`AndroidVulkanContext`, `AndroidSwapchain`, `AndroidEngine`, `AndroidFileIO`, `android_main`, the GLFW stub, and the Android `CMakeLists.txt` that builds `libmusicgame.so`). `android/` contains the Gradle project (AGP 8.5, compileSdk 36, NDK r27c). `tools/setup_android_sdk.bat` downloads SDK/NDK via Windows BITS (because `dl.google.com` is blocked via curl/TLS on this network but works through BITS/PowerShell WebClient); `tools/build_apk.bat` bundles assets + shaders, runs Gradle, and copies the APK to the chosen path.

### Editor expansions
The editor gained scan-line authoring support: a new in-scene tool row (Tap / Flick / Hold / Slide) shown when the project is in Scan Line mode, with all clicks gated by `|mouseY - scanLineY| < 10px`. Tap/Flick = LMB on scan line commits. Hold = LMB starts head, mouse wheel extends across alternating-direction sweeps, LMB commits endpoint. Slide (Cytus-style) = LMB starts, RMB while LMB held places control-point nodes (straight lines between them — each node becomes a sample tick point), LMB release commits. Direction enforcement prevents crossing scan-line turns.

## 2026-04-12 — The "big push" day

A single very long day that shipped a huge bundle of work. Breaking out the changes by subsystem is the only way to make sense of it.

### Circle mode

- **Disk animation.** Keyframed rotate/scale/move via `DiskRotationEvent`, `DiskMoveEvent`, `DiskScaleEvent` with segment-based interpolation and easing (Linear / SineInOut / QuadInOut / CubicInOut). Phase table built in `onInit`, drives `m_diskRotation`, `m_diskScale`, `m_diskCenter` each frame. Camera follows disk center via `rebuildPerspVP`. `reloadDiskAnimation(anim)` re-seeds keyframes from an edited chart so the editor can live-update the preview.
- **Per-song disk defaults.** `INNER_RADIUS`, `BASE_RADIUS`, `RING_SPACING` are no longer `constexpr static` — they're per-instance members seeded from `GameModeConfig::diskInnerRadius / diskBaseRadius / diskRingSpacing` in `onInit`. `m_diskScale` is seeded from `diskInitialScale`. Legacy fallbacks `0.9 / 2.4 / 0.6 / 1.0` are used when the config fields are zero. Four sliders in the `renderGameModeConfig()` sidebar expose them (ranges `0.2–3.0 / 1.0–6.0 / 0.1–1.5 / 0.3–2.0`) plus a "Reset disk defaults" button. Each slider marks `m_laneMaskDirty = true` because the reachability predicate reads `diskBaseRadius`. The values are persisted into `music_selection.json`.
- **Circle hold body rewritten as true arc slices.** `drawHoldBodies` previously built each ribbon step by placing a single centre point and spreading it along a linear tangent vector — which collapsed into radial rectangles for holds whose lane didn't change. It now places two real points on the ring per slice at `(cos(angle ± hA) · radius, sin(angle ± hA) · radius)` so adjacent slices stitch into a curved sector that follows the ring. `HoldBody` now carries `noteId` so the renderer can cross-reference `m_activeHoldIds` and brighten held hold bodies above `1.0` RGB for the bloom pass to pick up.
- **`computeEnabledLanesAt(songTime)`.** Const query returning a bitmask of lanes visible on-screen at a given time, sampling all three disk transforms locally without touching mutable state. This is the runtime counterpart to the editor-side `laneMaskForTransform` that would cause so much trouble later.
- **Lane-mask reachability fix v1.** `laneMaskForTransform` in `SongEditor.cpp` used to hardcode `kBaseRadius = 2.4` and check each lane against `|lx| < 2.85 && |ly| < 2.185` — but a ring of radius 2.4 has top/bottom points at `y = ±2.4`, so those lanes were **always** masked out. Combined with a `tc <= 32` gate in the renderer, this meant bumping track count above 32 was the only way to unlock the hidden lanes. Two fixes landed:
  1. The helper now takes an `outerR` parameter sourced from `gm.diskBaseRadius`, and uses half-extents `max(kFovHalfX, r) + 0.15` / `max(kFovHalfY, r) + 0.15`. It also returns `0xFFFFFFFFu` when `trackCount > 32` so high-count charts are never silently gated.
  2. The **Tracks** slider and all four Disk Layout sliders now set `m_laneMaskDirty = true`. Previously the cached mask for the old track count was held forever.

  (The `max(fov, r)` bound turned out to be self-defeating — see 2026-04-14 for the actual fix.)

### Scan Line

- **Variable-speed scan line.** Base period `T = 240 / BPM`. `ScanSpeedEvent` keyframes drive a precomputed phase-accumulation table built with Simpson's-rule integration. `scanLineFrac(t)` binary-searches the table and converts phase to a triangle wave on `[0,1]`. Falls back to constant-speed `fmod` when the keyframe list is empty.
- **Straight-line slides (Cytus-style).** Control-point nodes placed by the user (LMB start + RMB nodes). Straight lines between nodes. Each node is a sample tick point. `linearPathEval` for gameplay evaluation, `consumeSlideTicks` fires at sample-point times, `slideExpectedPos` returns the expected screen position for visual tracking.
- **Multi-sweep holds.** Holds that cross scan-line direction changes: `ScanNote::holdSweeps` tracks extra sweeps, zigzag body rendering with turn segments, page visibility checks `[notePage, endPage]`, fade logic based on end-page boundaries.

### Arcaea

The full arc pipeline came together end-to-end:

- **ChartLoader arc/arctap field compatibility.** UCF parser now accepts both legacy and new field names so charts round-trip through editor export/import. Easing: reads `easeX` / `easeY` (new, written by SongEditor) or falls back to `curveXEase` / `curveYEase`. Void flag: reads `void` (new) or falls back to `isVoid`. ArcTap position: reads `arcX` / `arcY` into `TapData.laneX` / `scanY` so `ArcaeaRenderer` can resolve the parent arc.
- **Sky-region authoring.** Arc and ArcTap notes now render and place in the **purple sky region** of the timeline (was ground). `arcX` → row mapping is non-inverted. Void auto-parent arcs are hidden from both editor and scene preview. A single click with the Arc tool commits a default 0.5s arc so single clicks always yield a visible note; an ArcTap click auto-spawns a hidden parent arc when none exists at that position, so the diamond appears immediately.
- **Arc/ArcTap editor — 3-panel system.**
  - **Panel 1 — Timeline.** Arc ribbons drawn as 24-sample coloured polylines in the sky region, matching where arcs actually live in gameplay. ArcTap diamonds at parent-arc positions. Click-drag with the Arc tool creates an arc. Selection hit-testing for both types. The `arcXToPixelY` helper maps `arcX` non-inverted (`regionTop + ax * regionH`) to match the track-row convention used by `renderNotes`. Void arcs are skipped here so their ribbons don't overlap the ArcTap diamond.
  - **Panel 2 — Height Curve Editor (120px).** Horizontal axis = time (synced with timeline scroll/zoom). Vertical axis = height `[0,1]`. Per-arc height curves as polylines with draggable circle handles at start/end. Grid at `0, 0.25, 0.5, 0.75, 1.0`.
  - **Panel 3 — Cross-Section Preview.** Front-face view in the left sidebar at the current scrub time. X = horizontal, Y = height. Arc positions as coloured dots, ArcTap as diamonds. Ghost dots for start/end positions. Ground line and grid.
  - **Properties panel.** Arc: position sliders (startX/endX/startY/endY), easing combos (`s / b / si / so / sisi / siso / sosi / soso` matching Arcaea `.aff`), colour radio buttons (cyan = 0 / pink = 1), void checkbox, child ArcTap list with Select buttons. ArcTap: parent reference, computed position, select-parent button.
  - **Parent fixup.** `fixupArcTapParents(deletedIdx)` called on every note deletion (Note Properties panel + timeline right-click) — orphans children of the deleted arc, decrements indices above the deleted index.
- **Chart reimport.** `setSong()` now converts `NoteType::Arc → EditorNoteType::Arc` (extracts startPos, endPos, duration, easing, colour, void) and `NoteType::ArcTap → EditorNoteType::ArcTap`. ArcTap → Arc parent linkage is reconstructed after import by scanning each ArcTap's time and position against all arcs and finding the best positional match. Chart → JSON → chart round-trips now preserve all arc data.
- **ArcaeaRenderer dynamic track count.** Tap lane mapping was previously hardcoded to 5 lanes (`(laneX / 4 - 0.5) * 4`), which pushed notes outside the highway on any other track count. `ArcaeaRenderer` now stores `m_trackCount` from `GameModeConfig` in `onInit` and maps `laneX` across the ground width with `(laneX / (trackCount - 1) - 0.5) * halfW * 2 * 0.85`, so all lanes stay inside the highway regardless of track count. `ArcaeaRenderer.cpp` now includes `ui/ProjectHub.h` to see the full `GameModeConfig` definition.
- **Editor scene preview** skips Arc/ArcTap in the generic lane-rect loop (no ghost tap under the ArcTap diamond); draws arc ribbons + ArcTap diamonds in the 3D scene branch using sky-space world coords.

### Cross-mode polish

- **Held-hold bloom.** `HitDetector::activeHoldIds()` returns a `vector<uint32_t>` of every currently-active hold. `GameModeRenderer::m_activeHoldIds` is populated by `setActiveHoldIds()` each frame. Renderers that want held-note glow check `m_activeHoldIds.count(note.id)` and multiply the hold body/head RGB above `1.0` (e.g. `{0.6, 2.4, 3.0, 0.9}`) so the brightness-threshold bloom post-process picks them up. Currently wired into `BandoriRenderer` (hold body + head) and `LanotaRenderer` (hold body).
- **Particle bursts in all modes.** Every renderer's `showJudgment` now emits a `renderer.particles().emitBurst(pos, color, count, 200.f, 8.f, 0.5f)` — Perfect = green/20, Good = blue/14, Bad = orange/10, Miss = skipped. `ArcaeaRenderer` and `CytusRenderer` previously had no particles at all; they now cache a `Renderer*` in `onInit` and fire bursts. Arcaea fires at `((lane + 0.5) / trackCount) × screenWidth, 0.85 × screenHeight`; Cytus fires at the matched `ScanNote::{sx, sy}`.
- **Bandori Slide colour fixed.** Slide was rendering as gold — same as Tap — because both used the default note colour. Slide is now purple `{0.8, 0.4, 1}`. Final per-note colours: Tap = gold `{1, 0.8, 0.2}`, Hold = cyan `{0.2, 0.8, 1}`, Flick = red `{1, 0.3, 0.3}`, Drag = green `{0.6, 1, 0.4}`, Slide = purple.
- **Bandori judgment displays dynamic.** `std::vector<JudgmentDisplay>` sized to `m_laneCount` in `onInit` (was a fixed array of 12).
- **Cross-mode integration audit.** A bug-fix sweep across all four reachable modes produced 10 separate fixes. All four modes were verified end-to-end that day: 2D DropNotes, 3D DropNotes, Circle, ScanLine. (Phigros remains deferred — `PhigrosRenderer` exists but there's no `GameModeType::JudgmentLine` enum value or UI button, so `dynamic_cast<PhigrosRenderer*>` branches in `Engine.cpp`'s gesture dispatch always return null. Circle hold-drift cancellation is also deferred: `CIRCLE_HOLD_DRIFT_DP = 64` is reserved but there's no `SlideMove` case in `handleGestureCircle` yet.)

### Editor infrastructure

- **Per-(mode, difficulty) chart files.** Filenames are now keyed on both game mode and difficulty: `assets/charts/<song>_<modeKey>_<diff>.json`, where `modeKey ∈ {drop2d, drop3d, circle, scan}`. Each (mode, difficulty) pair owns an independent chart file — switching modes no longer overwrites or reuses another mode's notes. `modeKey(gm)` helper in `SongEditor.cpp` returns the key from `GameModeType + DropDimension`. `chartRelPathFor(name, gm, diff)` composes the path for both export and load. `reloadChartsForCurrentMode()` clears `m_diffNotes / m_diffMarkers / disk-FX / scan-speed / BPM` state and loads the three (easy / medium / hard) files for the current `gameMode` from disk. `loadChartFile(diff, chartRel)` is the extracted single-chart loader used by both `setSong()` and `reloadChartsForCurrentMode()` — replaces the previous inline lambda. Mode / Dimension buttons in `renderGameModeConfig()` hook the switch: `exportAllCharts()` saves the old mode's charts, the mode / dimension field is updated, then `reloadChartsForCurrentMode()` pulls the new mode's charts.
- **Auto Play mode.** Watch-the-chart mode — the game auto-triggers Perfect on every note. Toggle lives on the Music Selection screen (`AUTO PLAY: ON/OFF` button below START, orange when on). Flag flows `MusicSelectionEditor::m_autoPlay → Engine::launchGameplay(song, diff, projectPath, autoPlay) → Engine::m_autoPlay`. `HitDetector::autoPlayTick(songTime)` returns `vector<AutoHit>` for every note whose `time <= songTime`: Tap/Flick/Drag/Ring/Phigros emit `HitResult{id, 0.f, type}` and erase; Hold/Slide/Arc push a head hit, move the note into `m_activeHolds`, and keep syncing `currentLane = evalHoldLaneAt(holdData, tOff)` so `consumeSampleTicks` scores every tick Perfect; holds whose `noteStartTime + noteDuration` has elapsed emit a release `HitResult` with `isHoldEnd = true` and are erased. `Engine::update` dispatches every result through the normal `dispatchHitResult` path **before** the miss sweep, so score / combo / HUD / particle bursts behave identically to a human hitting Perfect.
- **Achievement FC/AP image pickers** reduced to asset-drag-only 96×96 drop slots (text input and Browse buttons removed). Only `ASSET_PATH` drag-drop payloads from the Asset Browser are accepted. The background image picker still supports drag / browse / text-input — this change was specifically for the achievement slots.

## 2026-04-14 — Lane-mask gating actually gates

A bug report from the user: "I scale the disk to 2.82× and no lanes turn gray in the chart timeline, even though lanes near the top/bottom are clearly off-screen."

### Finding the bug
The Circle-mode editor is supposed to grey out lanes on the chart timeline when a disk animation keyframe pushes them outside the camera's viewport — you can't place notes in a lane that isn't visible at that point in the song, so the editor should visually gate them. The logic exists, and the 2026-04-12 fix had supposedly made it work. But the user's screenshot showed a 2.82× scale keyframe with zero gating.

I dropped a temporary `std::cerr` into `SongEditor::rebuildLaneMaskTimeline` to print the computed mask at each sampled time. The probe output revealed two things:

1. The **saved chart file** had `target: 1.28` for the scale keyframe, not 2.82 — the user's 2.82 was an unsaved in-memory edit.
2. At `scale = 1.28`, the computed mask was `0x71c`, meaning 6 of 12 lanes were correctly gated past `t = 6s`. So the reachability math was *actually working* for the saved value.

Good news: the editor was gating lanes. Bad news: the 2026-04-12 fix's bound formula was still broken at higher scales.

### The real bug
`laneMaskForTransform`'s half-extents were defined as:

```cpp
const float halfX = std::max(kFovHalfX, r) + 0.15f;
const float halfY = std::max(kFovHalfY, r) + 0.15f;
```

where `r = outerR * scale`. When you scale the disk up, `r` grows and the "playable rect" grows with it **in lockstep**. A lane's hit point at radius `r` is always inside a box of size `≥ r`, so nothing ever gets gated once the disk is larger than the FOV. The 2026-04-12 author's intent had been "don't falsely mask the default ring at scale 1.0" — but `max(fov, r)` is exactly the wrong way to achieve that, because it makes the bound scale-dependent.

The camera's visible rect at `z = 0` is fixed: FOV_Y = 60°, eye at `z = 4`, so the view is `~±3.0 × ±2.31` world units. That rect doesn't move when the disk gets bigger; the disk just projects outside it. The bound should be **fixed** at the FOV rect, with a small margin so the default `baseR = 2.4` ring still fits at scale 1.0.

### The fix
```cpp
const float halfX = kFovHalfX + 0.15f;  // ~3.15
const float halfY = kFovHalfY + 0.15f;  // ~2.46
```

At the default ring (`baseR = 2.4`, scale 1.0), the top lane sits at `(0, 2.4)`, which fits inside `±2.46` thanks to the 0.15 margin. At `scale = 2.82`, `r = 6.77`, lanes project well outside both bounds, and the mask becomes `0` — gray-hatched across all 12 lanes on the timeline past the keyframe's end time. The comment block above the function was rewritten to explain *why* the bound must stay fixed so this doesn't get "fixed" back to the broken version later.

### Three related fixes shipped alongside
While investigating, I found that `diskInitialScale` — the slider for the disk's base scale before any keyframes — didn't actually work in either the editor or the runtime, for symmetric reasons:

- **Editor side:** `SongEditor::rebuildLaneMaskTimeline` sampled scale via `sampleDiskScale(t, diskScale())` which hardcodes the base at `1.0`. Bumping `diskInitialScale` in the UI had no effect on the mask.
- **Runtime side:** `LanotaRenderer::onInit` *did* seed `m_diskScale = config->diskInitialScale`, but `onUpdate` immediately overwrote it every frame with `getDiskScale()` — which also hardcodes base `1.0`. So the initial-scale slider was a write-once value that was destroyed on frame 1.

Fixed both:

- Added `m_diskInitialScale` as a real member on `LanotaRenderer`. `onInit` seeds it from config. `onUpdate` computes `m_diskScale = m_diskInitialScale * getDiskScale(...)` each frame — the initial scale is now a true base multiplier, not a write-once overwrite.
- `rebuildLaneMaskTimeline` multiplies the sampled scale by `gm.diskInitialScale` too, so the editor's lane-mask sampling matches the runtime exactly.

And because "super big" was blocked by slider caps:

- Raised the `Target scale` keyframe slider from `3.0` to `5.0`.
- Raised the `Initial scale` slider from `2.0` to `5.0`.

At the new caps the user can scale the disk far enough beyond the viewport to gate every lane from either direction.

### Docs touched
- New "Lane-mask gating actually gates now" section in `sys7_editor.md` directly under the (now-historical) 2026-04-12 entry.
- Matching bullet in `MEMORY.md`.
- This dev log itself was created retroactively at the end of this session.

## 2026-04-15 — Hold-note rework (Circle + 2D drop), gameplay restart, editor flow polish

A long single-session walk through the entire hold-note visual pipeline, the gameplay restart path, and the test-mode editor flow. Most of the day was iterative: render holds, look at a screenshot from the user (real Lanota, real Bandori), realise the model is wrong, redesign, iterate.

### Circle mode — hold body redesigned to match real Lanota

The user opened with: "the hold note becomes a flat rectangle, not an arc like the click." `LanotaRenderer::drawHoldBodies` was tessellating each time-slice as just two endpoints `(sL, sR)` connected to the next slice as a single quad — a chord across the angular extent. Every iteration of the body fix below was driven by a screenshot from the real game and an "it doesn't look like this" critique.

**Iteration 1 — angular tessellation per slice.** Replaced the 2-point slice with `K=10` angular points + inner/outer radial offsets. Adjacent slices stitched via top/bottom/front faces. Result: looked like a 3D chunky tube with visible edges — wrong.

**Iteration 2 — flat annular sector on the disk.** Dropped the inner/outer offsets entirely. Each slice was a flat tile lying on the disk surface, just like a tap-note tile but stretched along the radial axis. Brightness gradient + halo pass. Closer, but still wide-band.

**Iteration 3 — narrow Lanota beam.** Reference screenshot revealed Lanota holds aren't full-lane bands at all — they're narrow glowing rays from the disk centre to the head, white-cyan. Narrowed `hA` from full lane to `0.09 × laneAngular`, halo to `0.22 × laneAngular`, made the colour bright `(0.85, 1.05, 1.35)` blooming to `(1.6, 2.4, 3.0)` while held. Quadratic gradient `0.20 → 1.0` so the beam reads as light shining outward.

**Iteration 4 — Bezier corners.** User followup: "the turn place has a sharp 90° corner — use a Bezier curve." The shared `evalHoldLaneAt` uses smoothstep gated on `transitionLen`, which has zero derivative at every join — correct for hit detection, but produces a flat-then-curve-then-flat that visibly stair-steps. Added a renderer-local `evalHoldLaneSmoothLanota` that does Hermite cubic interpolation across **the entire segment** between consecutive waypoints (ignoring `transitionLen`) with Catmull-Rom tangents `m_a = ½(L_b − L_{a-1})`, `m_b = ½(L_{a+1} − L_a)`. C1 across all joins — corners arc through instead of flattening. Hit detection is unaffected because it still uses `evalHoldLaneAt`.

**Iteration 5 — track follows the rim, not radial.** A second Lanota screenshot showed a long curved cyan arc bending across the disk, not a radial beam. The body is a 2D track laid on the disk: spine sample points come from the smoothed evaluator, projected to screen, and offset perpendicular to the screen-space tangent (central difference) by constant pixel widths `coreHalfPx=6`, `haloHalfPx=13`. Constant visual width even through tight bends. Gradient drawing: halo first (dim/wide), then bright core on top.

**Iteration 6 — head anchor at the rim end.** Real Lanota has a small lens-shaped marker at the rim where the beam meets the disk edge. Added a head-anchor arc tile (12-segment angularly tessellated annular sector, full lane width) at `spine[0]`. Drawn after the ribbon so it sits on top.

**Iteration 7 — body spawns from the inner disk like a tap.** User pointed out: the hold should fly out from the small inner disk like every other note, not appear at the rim instantly. Restored `radiusForAbsT(absT)` mapping driven by remaining travel time — `absStart = max(headT, songTime)`, `absEnd = min(tailT, songTime + APPROACH_SECS)`, head pins to the rim only after `songTime ≥ headT`. The head anchor uses the same mapping so it travels with the head.

**Particle burst position fix.** The hold-tick fallback path in `showJudgment` was emitting bursts at `INNER_RADIUS * m_diskScale` — the small/far disk — with a stale comment claiming that was where the hold body touched the hit line. Switched to `m_rings.front().radius` so bursts spawn at the outer hit ring where the head actually lives.

### 2D drop mode — hold body fixes (segments, corner morph, flicker, judgement-line, missed-head)

Five separate problems, each surfaced by a user screenshot or playthrough:

**1. Segments popping in at the far plane.** Long Bandori holds were tessellated uniformly across `[0, dur]` with N=12 or N=20 samples and culled per-sample on `wz`. World-space spacing between samples was huge for long holds, so each individual segment popped into view as it crossed the cull line. Fix: invert the `wz → tOff` mapping to derive `[tOffLo, tOffHi]` as the sub-range of the hold actually inside the highway, then tessellate uniformly inside that range. The visible ribbon is now continuous from the moment any part of the hold enters the far plane.

**2. Corner morph as the hold approached.** With the new visible-window tessellation, corner samples were uniform within a window whose bounds drift each frame, so the polyline approximation of the curved corner kept reshaping itself — read as low fps. Fix: every sample point now sits on a fixed chart-time grid (`kGridStep = 0.04 s`), plus chart-time-anchored corner subdivisions (`kInteriorSamples = 8` per transition window). The visible window only decides which samples to render, not where they sit. As the highway scrolls each polyline vertex traces a perfectly straight screen-space line and the corner geometry never reshapes.

**3. Flicker at the judgement line during an active hold.** The visible-window cutoff was padded by `kGridStep` for boundary continuity, but as `songTime` advanced, the grid sample nearest the line crossed `visLo` each frame, snapping in/out of the render set. Fix: add `tOffLo` and `tOffHi` themselves as explicit boundary samples (no padding). Since the active-hold `zNear = 0` makes `tOffLo` always project exactly onto the judgement line, the boundary slice now sits at a stable screen position and the grid samples behind it just enter/leave smoothly.

**4. Past portion still drawn while holding.** During an active hold the body should hide everything past the judgement line. Active-hold `zNear` switches from `+12` (the pre-hold "give the player a frame to react" overshoot) to `0`, so `tOffLo` clamps to `songTime - note.time` and the past portion is never tessellated.

**5. Missed/bad-head holds linger forever.** If the player let the head pass without touching, the entire body was still being drawn as it crossed the screen. Added: `if (!holdActive && songTime > note.time + 0.15) m_hitNotes.insert(note.id); continue;`. Bad-window cutoff is 0.15 s. The whole hold disappears the moment the head is judged missed/bad.

### Gameplay restart — three separate broken pieces

The Restart button in the pause menu was nominally functional but in practice nothing worked. Three sequential bugs:

**Bug 1 — superficial state reset.** The original Restart only reset score, judgment, audio, clock. It skipped the active mode renderer (so `m_hitNotes` from the previous run persisted), the HitDetector (active holds still tracked), and used a hard `setSongTime(0.0)` that bypassed the lead-in / audio-gate logic. Fix: cache the chart and audio path at launch (`m_currentChart`, `m_currentProjectPath`, `m_currentAudioPath`). Add `Engine::restartGameplay()` that re-creates the renderer via `createRenderer(m_gameplayConfig)`, re-runs `setMode()` (which clears HitDetector + judgment + score + touches + keyboardHolds), resets clock to `-leadIn - audioOffset`, clears `m_audioStarted` and `m_showResults` and `m_gameplayPaused`. The pause menu now just calls `restartGameplay()`.

**Bug 2 — clock stayed paused.** After Restart, no notes moved at all. The pause menu had called `m_clock.pause()` which sets `m_running = false`, and `tick()` returns 0 dt while paused. `restartGameplay()` reset `m_gameplayPaused` but never called `m_clock.resume()`. Added the resume call.

**Bug 3 — no music + instant results screen after restart.** After bug 2 was fixed, restarted gameplay had no audio and the results screen popped up after ~3 s. Two problems chained: the lead-in handler clears `m_pendingAudioPath` after the first `loadAudio()`, so on restart it was empty and the handler fell into the silent "no audio" branch. Then the song-end guard (`songT > 2 && !m_audio.isPlaying()`) tripped because audio never started. Fix: `restartGameplay()` restores `m_pendingAudioPath = m_currentAudioPath` so the lead-in handler reloads + replays the same song.

Restart now works for all four game modes because it routes through `createRenderer(m_gameplayConfig) → setMode()` which dispatches to whichever renderer (Bandori / Arcaea / Lanota / Cytus).

### Music playback lag during dense hold sample ticks

The user reported: as a Bandori hold body crosses the judgement line, the music playback stutters. Tracked it to `Engine.cpp:269` where the hold sample-tick loop was calling `m_audio.playClickSfx()` on every successful tick. `playClickSfx()` (`AudioEngine.cpp:81`) **allocates a fresh `ma_audio_buffer` + `ma_sound` per call, registers them with the miniaudio engine** (which has to synchronize with the audio thread), and **leaks both forever** with the comment "leak per click is negligible." Per click maybe — but Bandori cross-lane holds fire sample ticks densely, the leaked source list grows unbounded inside miniaudio's mixer, and the audio thread starts to stutter. The player hears it as music lag every time a hold body crosses the line.

Fix: removed the `playClickSfx` call from the sample-tick loop. The player is already pressing the lane during a hold, so the per-tick click was unnecessary feedback anyway. Tap hits still play it (one shot each), so the leak no longer compounds. The proper long-term fix is a pre-baked pooled SFX in `AudioEngine`, but removing the redundant call site is enough to fix the symptom now.

### Editor flow — Test Game buttons, selection START gate, test-mode Exit

Three small UX changes around the test-game flow:

- **Removed the "Test Game" button** from the top-right of `StartScreenEditor` and `MusicSelectionEditor`. The user only wants Test Game accessible from the song editor.
- **Selection page START button** now serves two contexts. In the editor, the user is designing how the music selection page looks, so clicking the START triangle should NOT drop them into a play session. In test-game mode, the user is actually navigating the game, so it must launch the song. Gated on `m_engine->isTestMode()`. First attempt removed the launch entirely and broke the test-game flow — that taught me the page is reused between editor and runtime, and the difference is the test-mode flag.
- **Pause → Exit in test mode** was calling `exitTestMode()`, which closes the standalone window because `m_testReturnLayer == StartScreen`. The user wants to be sent back to music selection instead so they can pick another song. `exitGameplay()` now does `switchLayer(EditorLayer::MusicSelection)` in test mode; the standalone window only closes via the full `exitTestMode()` path (ESC from the start screen).

### Files touched
- `engine/src/game/modes/LanotaRenderer.cpp` — rewrote `drawHoldBodies`, added `evalHoldLaneSmoothLanota`, fixed particle burst position.
- `engine/src/game/modes/BandoriRenderer.cpp` — visible-window tessellation, chart-time grid, anchored corner samples, boundary-pinned samples, active-hold judgement-line clip, missed-head culling.
- `engine/src/engine/Engine.cpp` + `Engine.h` — `m_currentChart`, `m_currentProjectPath`, `m_currentAudioPath`, `restartGameplay()`, removed `playClickSfx` from sample-tick loop, test-mode `exitGameplay()` returns to MusicSelection.
- `engine/src/ui/StartScreenEditor.cpp` — removed Test Game button.
- `engine/src/ui/MusicSelectionEditor.cpp` — removed Test Game button, gated START launch on `isTestMode()`.
- `docs/devlog.md` (this entry), `docs/MEMORY.md`, `docs/sys5_gameplay.md`, `docs/sys6_game_modes.md`, `docs/sys7_editor.md` (updated downstream).

---

# Appendix — Architecture reference, by system

The daily chronology above is organized around when things landed. This appendix is organized around *what things are*: per-subsystem architectural content that accumulated quietly and isn't tied to any single date. These are the parts a blog reader needs in order to understand the daily entries, but they didn't fit naturally into any one day's narrative.

## System 1 — Rendering

### PostProcess bloom pipeline
Bloom is implemented as a **compute mip-chain** in `PostProcess.cpp`:

1. **Downsample pass** (`bloom_downsample.comp`) reduces the scene image through a chain of progressively smaller mip levels, each dispatch sampling from the previous level.
2. **Upsample pass** (`bloom_upsample.comp`) walks back up the chain, additively combining each level to produce a smooth multi-scale blur.
3. **Composite pass** (`composite.vert` + `composite.frag`) blends the bloom chain onto the final swapchain image as a fullscreen triangle draw.

The scene framebuffer format is preferred as `R16G16B16A16_SFLOAT` on desktop so the bloom chain has HDR headroom for brightness-above-1.0 highlights (which is how held hold notes glow — see sys5/sys6). On Android that format isn't reliably supported, so the Android fixes on 2026-04-05 added both a scene-format fallback to `R8G8B8A8_UNORM` and an `m_bloomEnabled` flag that skips bloom entirely when the GPU doesn't support RGBA16F as a storage image.

### Shader inventory
Six shaders live in `shaders/` and compile to `build/shaders/*.spv` via `glslc`:

| Shader | Purpose |
|---|---|
| `quad.vert` / `quad.frag` | Textured quad rendering — powers `QuadBatch` |
| `line.vert` / `line.frag` | Line rendering (lines expanded CPU-side to quad triangles) |
| `mesh.vert` / `mesh.frag` | 3D mesh rendering with depth test |
| `bloom_downsample.comp` | Bloom compute downsample |
| `bloom_upsample.comp` | Bloom compute upsample |
| `composite.vert` / `composite.frag` | Final bloom composite pass |

### Renderer exposure surface
Game-mode plugins never allocate Vulkan resources directly. Instead `Renderer` exposes exactly three accessors that cover every legitimate need: `whiteView()` (a 1×1 white texture view for untextured quads), `whiteSampler()` (the matching sampler), and `descriptors()` (the shared `DescriptorManager` for set layouts). If a game mode needs anything more than that, the answer is to extend `Renderer` itself, not to reach around it.

### ParticleSystem
Ring-buffered 2048-particle allocator with additive blending. `emitBurst(pos, color, count, ...)` is the single entry point used by every renderer's `showJudgment` — Perfect/Good/Bad bursts all route through the same call with different colours and counts.

### Single-subpass color render pass
`RenderPass.h/.cpp` declares a single-subpass colour render pass. The non-obvious requirement: both `EXTERNAL → 0` **and** `0 → EXTERNAL` subpass dependencies must be declared, otherwise `PostProcess` reads stale scene data. This was the 2026-03-21 bug and is now written into sys1 as a permanent rule.

### Duplicate `ChartTypes.h` — known ODR hazard
There are two `ChartTypes.h` files in the repo: `engine/src/game/chart/ChartTypes.h` (internal, compiled) and `engine/include/MusicGameEngine/ChartTypes.h` (public header, not compiled by anything). Definitions may drift apart over time — if any consumer ever includes *both*, it's a silent ODR violation. Currently harmless because the public header is unused, but it should be resolved before the engine ships as a library.

## System 2 — Resource Management

### Components not yet mentioned in the daily log
- **`AssetBrowser`** (`ui/AssetBrowser.h`) — the shared asset panel used by every editor layer. `importAssetsToProject()` is the single import entry point, intentionally shared so imports on one page appear on every other page.
- **`GifPlayer`** (`ui/GifPlayer`) — animated-GIF playback for background images and thumbnails.

### AudioEngine API surface
miniaudio-backed. Public methods: `load(path) → bool`, `play()`, `pause()`, `resume()`, `stop()`, `positionSeconds()`, `isPlaying()`. Plus one synthesized helper: **`playClickSfx()`** generates a 30ms 1200Hz sine click. As of 2026-04-15 it's only called on tap hits; the original hold-sample-tick call site was removed because each invocation allocates and leaks a fresh `ma_audio_buffer` + `ma_sound`, and dense ticks were stalling the audio thread and lagging music playback. A pooled, pre-baked SFX is the right long-term fix — the current implementation is single-shot only. `load()` returning a bool is load-bearing — `Engine::m_audioStarted` only flips when `load()` succeeds, which is what drives the 2-second lead-in logic in sys5.

### Shared math utility
`catmullRomPathEval(path, u)` is an inline Catmull-Rom spline interpolator that lives next to the chart types and gets reused by every scan-line slide path evaluator.

### Note type inventory
The full list, for reference: `Tap, Hold, Flick, Drag, Arc, ArcTap, Ring, Slide`. Note-data variants: `TapData` (with `scanX/Y`, `duration`, `scanPath`, `samplePoints` for Cytus-style slides), `HoldData` (with `waypoints`, `scanX/Y/EndY/sweeps`), `FlickData` (with `scanX/Y`), `ArcData`, `PhigrosNoteData`, `LanotaRingData`.

## System 3 — Core Engine

### Main loop
`Engine::mainLoop()` polls events, checks for window resize, then runs `update(dt)` followed by `render()`.

**`update(dt)` sequence** — the order matters and is stable across modes:

1. **Lead-in clock advancement** — manually step the clock forward until audio actually starts playing.
2. **DSP sync** — once audio is running, pull the authoritative time from `m_audio.positionSeconds()` rather than trusting `dt` accumulation.
3. **Input update** — hold timeouts, gesture state machine ticks.
4. **Particle update** — ring buffer aged and culled.
5. **Test-mode transition** — check if a child Test Game process should be spawned or torn down.
6. **Gameplay tick** — miss detection, hold sample ticks, Cytus slide ticks, broken-hold cleanup.
7. **Active mode `onUpdate`** — the current game-mode renderer's per-frame logic.
8. **Preview mode `onUpdate`** — editor-scene preview logic, separate from gameplay.
9. **Song-end detection** — trigger the results overlay when audio stops naturally.

**`render()` sequence:**

1. Blit the song background image into the scene framebuffer.
2. Active mode or preview mode `onRender`.
3. ImGui frame (whichever layer is current builds its own panel).
4. ImGui render into the current frame's command buffer.

### Game flow lifecycle
All mode-switching goes through explicit lifecycle calls in `Engine`:

- **`launchGameplay(song, diff, projectPath, autoPlay)`** — loads the chart file, creates a renderer via `createRenderer(gameMode)`, sets mode, stops audio, sets the lead-in clock, and switches the editor layer to `GamePlay`.
- **`launchGameplayDirect(chartData, ...)`** — same flow but takes a pre-built `ChartData` object instead of a file path. This is what the editor's Play button uses so it can run an unsaved in-memory chart.
- **`exitGameplay()`** — stops audio, calls the mode's `onShutdown`, clears touches, clears the background, and returns to the previous editor layer (or exits test mode if we were spawned as a child process).
- **`togglePause()`** — pauses/resumes audio + clock + scene viewer together. They must move in lockstep or the chart time desyncs from audio.
- **Restart (pause menu)** — `stop()` then `play()` audio, reset clock / judgment / score / touches.
- **Results overlay** — triggered when audio stops naturally at song end. Shows score, combo, judgment breakdown, and a rank (S/A/B/C) computed from the final score.

### Build configuration
C++20, Vulkan, GLFW, GLM, VMA, stb, ImGui — all from `third_party/`. Outputs: `MusicGameEngine.lib` (the static library) and `MusicGameEngineTest.exe` (the test harness that wraps the library in a runnable app). Two Windows-specific things to remember: `NOMINMAX` is defined before `<windows.h>` in `Engine.cpp` to prevent the min/max macros from breaking C++ code, and **OLE is initialized in the Engine constructor** because Windows drag-drop and native file dialogs both need an OLE apartment.

## System 4 — Input & Gesture

### Keyboard mapping
Keys `1, 2, 3, 4, 5, 6, 7, 8, 9, 0, Q, W` map to lanes 0–11 — a 12-lane ceiling for keyboard input, which matches the 2D DropNotes maximum track count. The key callback in `Engine.cpp` on press tries `beginHold` first (for hold heads), then `checkHit` (for taps / flicks), and also calls `updateHoldLane` on every active keyboard-originated hold so cross-lane tracking works from the keyboard too. On release it calls `endHold` for the held lane.

### Touch platform support
Three platforms for raw touch input:

- **Desktop (GLFW)** — the mouse is simulated as a single touch with ID `-1`, deliberately out-of-range of any real touch ID so the simulated and real paths can never collide.
- **Android** — JNI bridge pushes touch events into `InputManager` from `android_main.cpp`.
- **iOS** — `UITouch` → `InputManager`. The iOS path exists in `input/TouchTypes.h` and `InputManager.h` even though Android is the only mobile target actively shipped right now.

### GestureRecognizer state machine
Per-finger state machine:

```
Idle ─ TouchDown ─┬─ (hold threshold)     ─ Holding ─ (movement threshold) ─ Sliding
                  ├─ (quick release)      ─ Tap
                  └─ (velocity threshold) ─ Flick
```

Emits `GestureType::{Tap, Flick, HoldBegin, HoldEnd, SlideBegin, SlideMove, SlideEnd}`. `SlideMove` is what drives `updateHoldLane` during a cross-lane hold.

### DPI scaling
`ScreenMetrics::dp(float)` converts density-independent pixels to screen pixels at a reference density of 160 DPI. Every hit-test tolerance is expressed in `dp()` so it behaves the same on a phone and a 4K desktop:

- Lanota + Cytus pick tolerance: `dp(48)` (~7.6mm — the standard fingertip radius Android / iOS use for touch targets).
- Scan-line slide tick tolerance: `dp(64)`.
- Arcaea position-based hit radius: `HitDetector::HIT_RADIUS_PX = 90.0f`.

### Bandori mobile ultrawide layout
On mobile, BanG Dream uses a dedicated ultrawide layout: 50% highway width, raised camera, larger notes. This is the reason `BandoriRenderer` has separate mobile/desktop tunings — the standard desktop camera looks wrong on a landscape phone screen.

## System 5 — Gameplay

### Timing windows
- **Perfect** ±20ms
- **Good** ±60ms
- **Bad** ±100ms
- **Miss** >100ms

`GameModeConfig` stores `perfectScore / goodScore / badScore` so individual game modes can override the values — reserved for per-mode scoring tuning.

### HitDetector method surface
Five entry points for hit detection, one per input style:

- `checkHit(lane, songTime)` — lane-based (Bandori, Lanota keyboard). Handles `TapData`, `HoldData`, `FlickData`, and `LanotaRingData` (via `angleToLane`).
- `consumeNoteById(noteId, songTime)` — id-based (Lanota touch, Cytus touch). The caller picks the note geometrically; the detector only validates timing.
- `checkHitPosition(screenPos, screenSize, songTime)` — Arcaea ground taps.
- `checkHitPhigros(screenPos, lineOrigin, lineRotation, songTime)` — Phigros judgment-line projection.
- `consumeDrags(lane, songTime)` — consumes **all** Drag notes at a lane within ±0.15s. Drags auto-hit on *any* touch — the player never needs to time them individually, they just need to be touching *something*.

### Hold lifecycle
Four entry paths into a hold, unified by the same `ActiveHold` struct:

- `beginHold(lane, songTime)` — lane-based.
- `beginHoldById(noteId, songTime)` — id-based, returns a `HitResult` with timing delta. Accepts both `HoldData` and `TapData` (for `NoteType::Slide`).
- `beginHoldPosition(screenPos, ...)` — Arcaea arc holds.
- `endHold(noteId, releaseTime)` — finalizes the hold and returns a `HitResult`.

All three `beginHold*` methods erase from `m_activeNotes` so the same hold can't spuriously trigger a Miss in the next frame's sweep.

The `ActiveHold` struct itself carries everything needed for both sample-tick scoring and cross-lane tracking: `noteId`, `startTime`, `noteStartTime`, `noteDuration`, `noteType`, `lane`, `currentLane`, `consecutiveMissedTicks`, `broken`, `holdData`, `positionSamples`, `sampleOffsets`, `nextSampleIdx`.

### Engine gesture dispatch table
Each mode has its own gesture handler so input semantics can differ per-mode:

| Mode | Handler | Input path |
|---|---|---|
| 2D Drop (Bandori) | `handleGestureLaneBased` | lane from screen X → `checkHit` / `beginHold` + `consumeDrags` |
| 3D Drop (Arcaea) | `handleGestureArcaea` | `checkHitPosition` / `beginHoldPosition` + `updateSlide` |
| Phigros | `handleGesturePhigros` | `checkHitPhigros` on each active judgment line |
| Circle (Lanota) | `handleGestureCircle` | `pickNoteAt` → `consumeNoteById` / `beginHoldById` |
| Scan Line (Cytus) | `handleGestureScanLine` | `pickNoteAt` → `consumeNoteById` / `beginHoldById` + `updateSlide` |

All hold gesture handlers dispatch head judgments. The flick handler calls `showJudgment` and falls back to a tap for non-flick notes.

### Audio lead-in
There's a **2-second visual lead-in** before audio starts on every song. `loadAudio` returns a bool; `m_audioStarted` only flips true on successful load. `audioOffset` is configurable per song to handle tracks with a long pre-roll. Until `m_audioStarted` is true, the clock advances manually via `dt`; once audio starts, the DSP-sync path in step 2 of the main loop takes over.

### Miss detection and lane rounding
`update(songTime)` removes notes that are more than 0.1s past their scheduled time and returns them as a `vector<MissedNote>`. Missed-note extraction handles every data variant including `LanotaRingData` — if you add a new note variant, this is one of the two places (along with `setTrackCount`) that must learn about it.

**All `laneX` float → int conversions use `std::lround`**, not truncation. This was a deliberate call early on: truncation produces visible lane bias on charts authored with non-integer `laneX` values, whereas rounding is stable.

## System 6 — Game Mode Plugins

### Known deferred items
Two things are intentionally incomplete and worth calling out so a future reader doesn't spend an afternoon trying to make them work:

1. **Phigros mode is unreachable.** There's no `GameModeType::JudgmentLine` enum value, no UI button that selects Phigros, and no mode-factory entry. `PhigrosRenderer` exists as dead code and the `dynamic_cast<PhigrosRenderer*>` branches in `Engine.cpp`'s gesture dispatch always return null. The plan was to finish Phigros as a mode plugin, but it's lower priority than the four modes that ship.
2. **Circle hold-drift cancellation is deferred.** `CIRCLE_HOLD_DRIFT_DP = 64` is reserved as a constant in the Circle gesture handler, but there's no `SlideMove` case in `handleGestureCircle` yet. So a Circle hold doesn't break when the finger drifts off the note's angular column — you can hold a Circle note with wildly inaccurate positioning and still score it. The data path (`consumeSampleTicks`, broken-hold detection) is already in place from the cross-lane hold work — only the `handleGestureCircle` side is missing.

### Per-mode implementation notes
- **BandoriRenderer:** dynamic lane count from `config->trackCount`, configurable camera (eye / target / FOV from `GameModeConfig`), auto lane spacing from FOV + aspect ratio. `JudgmentDisplay` array is sized to `m_laneCount` in `onInit` (was a fixed array of 12 before 2026-04-12). Hold Z clip is `+12` (vs `+2` for taps) so long hold bodies don't clip out when the head is already past the hit zone.
- **LanotaRenderer (Circle):** two concentric disks — inner spawn disk (default `INNER_RADIUS = 0.9`) and outer hit ring (default `BASE_RADIUS = 2.4`). Notes travel radially outward. Lane 0 is at 12 o'clock, clockwise: `angle = π/2 - (lane / trackCount) · 2π`. Max lanes = 36. Per-note `laneSpan` (1–3) for wide notes. `computeEnabledLanesAt(songTime)` is the const query that returns a bitmask of lanes visible on screen — the runtime counterpart to the editor-side lane-mask rebuild. **Spatial picker:** `pickNoteAt(screenPx, songTime, pixelTol)` projects each note's world position to screen via `m_perspVP` and picks the nearest within tolerance + timing window.
- **CytusRenderer (Scan Line):** page-based visibility (notes only show during their sweep page), scale/fade approach animation (0.30 → 1.0 scale, 0.25 → 1.0 alpha). **Spatial picker:** `pickNoteAt` with ±0.18s timing gate. Slide paths hit-tested by vertex proximity.
- **ArcaeaRenderer (3D DropNotes):** dual-layer highway — ground plane for floor notes + elevated arc plane for sky notes. Arcs are 32-segment quad-ribbon meshes built by `buildArcMesh(ArcData)` with `ARC_SEGMENTS = 32` samples. `evalArc(arc, t)` evaluates position with independent X/Y power-eased interpolation. Z-depth = `t * duration * SCROLL_SPEED(8.0)`. Arc width = 0.35 world units. Colours: cyan (0) = `{0.4, 0.8, 1.0}`, pink (1) = `{1.0, 0.4, 0.7}`. Void arcs render at 40% alpha. **Culling:** skip arcs with `zOffset > 30` or `< -duration * SCROLL_SPEED - 2`.
- **PhigrosRenderer (dead code):** would have used rotating judgment lines with a `SceneGraph` hierarchy. `checkHitPhigros` projects a touch onto a rotating line. Notes attached to judgment lines via `JudgmentLineEvent::attachedNotes`. Unreachable until a mode enum and UI button are added.

## System 7 — Editor UI

### Config panels (left sidebar)
The SongEditor sidebar hosts a stack of collapsible config panels, each controlling a `GameModeConfig` field:

- **Game Mode** — style (DropNotes / Circle / ScanLine), dimension (2D / 3D), track count.
- **Camera** — eye position (x, y, z), look-at target (x, y, z), field of view (20°–120°). Each renderer reads these at `onInit` time.
- **HUD** — score and combo position, font, colour, glow, bold — configured per-element via `HudTextConfig`. Rendered by `Engine::renderGameplayHUD` using `ImGui::GetForegroundDrawList` so the HUD always sits on top.
- **Score** — Perfect / Good / Bad score values. FC/AP achievement image pickers (asset-drag only, 96×96 drop slots).
- **Disk Animation (Circle mode)** — keyframed rotate/scale/move events. Per-difficulty storage in `m_diffDiskRot / m_diffDiskMove / m_diffDiskScale`. Add / edit / delete UI with easing combo. DiskFX timeline strip. (Feature landed 2026-04-12; see that day's entry.)
- **Disk Layout (Circle mode)** — four sliders: inner radius, hit ring radius, ring spacing, initial scale, plus a "Reset disk defaults" button. Each slider marks `m_laneMaskDirty = true` because the reachability predicate reads these fields. (Feature landed 2026-04-12.)
- **Scan Line Speed (ScanLine mode)** — `ScanSpeedEvent` keyframes (0.1× – 4.0×). Per-difficulty storage in `m_diffScanSpeed`. Phase table rebuilt lazily on edit.
- **Judgment Windows** — Perfect / Good / Bad millisecond threshold sliders.
- **BPM Map** — tempo sections list. Populated automatically by Madmom beat analysis (2026-04-05) or edited by hand.

### Test Game
Spawns `MusicGameEngineTest.exe --test <project_path>` as a child process via `CreateProcessW`. The child runs the full flow `StartScreen → MusicSelection → Gameplay` while the editor window stays fully interactive. All three Test Game buttons (StartScreen, MusicSelection, SongEditor) route through the same `Engine::spawnTestGameProcess()` helper — this was unified on 2026-04-10.

### Chart persistence and round-trip
Save → `exportAllCharts()` writes UCF JSON per difficulty. Song open → loads charts back via `ChartLoader`. Round-trips preserve: notes, scan fields, disk animation, scan speed events, multi-waypoint holds, sample points, full arc data (startX/Y, endX/Y, easeX/Y, colour, void), and arctap positions. Per-(mode, difficulty) filenames (2026-04-12) mean each mode×difficulty pair owns an independent file and switching modes never stomps another mode's notes.

## System 8 — Android Packaging

### Installed SDK components
The `tools/setup_android_sdk.bat` script installs a specific matrix of versions:

| Component | Version | Path |
|---|---|---|
| Android Studio | 2025.3.3.6 | `C:\Program Files\Android\Android Studio` |
| SDK Platform | 36 | `…\AppData\Local\Android\Sdk` |
| NDK | r27c (27.2.12479018) | `…\Sdk\ndk\27.2.12479018` |
| Build Tools | 37.0.0 | `…\Sdk\build-tools\37.0.0` |
| Platform Tools | latest | `…\Sdk\platform-tools` |
| CMake | 3.22.1 | `…\Sdk\cmake\3.22.1` |

Gradle is pinned to 8.7 via the wrapper; AGP is 8.5. `local.properties` points at the Android Studio SDK install.

### APK contents
Every built APK contains:

- `lib/arm64-v8a/libmusicgame.so` — compiled engine
- `lib/arm64-v8a/libc++_shared.so` — C++ runtime
- `assets/shaders/*.spv` — 10 SPIR-V shaders (six source shaders compile to ten variants)
- `assets/assets/audio/` — music files (extracted to internal storage at runtime because miniaudio needs real file paths)
- `assets/assets/charts/` — chart JSON files
- `assets/project.json`, `music_selection.json`, `start_screen.json` — per-project config

### Key architectural rules
1. **GLFW stub approach.** `engine/src/android/stubs/GLFW/glfw3.h` provides type declarations (`GLFWwindow`, key constants, function stubs) so shared code compiles on Android without ever linking real GLFW. The stub directory is added to the Android CMake include path **first**, before the real GLFW path, so the stub wins.
2. **Link-time substitution.** `AndroidVulkanContext.cpp` and `AndroidSwapchain.cpp` implement the same classes as their desktop counterparts (`VulkanContext`, `Swapchain`) with Android-specific internals. The Android CMake compiles these **instead of** `VulkanContext.cpp` / `Swapchain.cpp` — same type names, different implementation files, chosen by which CMakeLists you're building.
3. **VMA Vulkan 1.0 restriction.** Android API 24 only guarantees Vulkan 1.0. The Android CMake defines `VMA_VULKAN_VERSION=1000000` to prevent VMA from calling any 1.1+ function. This is what drove the `BufferManager.cpp` `#if` guard pain on 2026-04-08 and the regression on 2026-04-09.
4. **Asset pipeline.** `build_apk.bat` copies project assets + compiled `.spv` shaders into `android/app/src/main/assets/`. On first launch, `AndroidEngine` extracts audio files from the APK to internal storage so miniaudio can open them by path.
5. **China network workarounds.** `dl.google.com` is blocked on this network via curl/TLS but works through Windows BITS / PowerShell WebClient, so the SDK downloader uses those instead of curl. Gradle itself uses Aliyun maven mirrors configured in `settings.gradle.kts`.

---

## 2026-04-17 — 3D Drop (Arcaea) rebuild

A long session rebuilding `ArcaeaRenderer` end-to-end after a round of visual bugs. Each "fix" surfaced the next issue, so the entry is in the order the problems came up.

### Arc clipping at the judgment line

Arcs were static meshes translated by a `zOffset`; the consumed portion kept rendering past the judgment line, and any naïve clip would snap to segment boundaries and pop.

- Switched arc vertex buffers to `createDynamicBuffer` (host-mapped). Index buffer stays static (topology is fixed).
- `onUpdate` computes `tClip = (songTime - startTime) / duration` per arc and rewrites the vertex array for `t ∈ [tClip, 1]` via `memcpy` into the mapped pointer. Head vertex pair sits exactly on the judgment plane; `tClip` is continuous float so the trailing edge recedes smoothly.
- Deliberately avoided `MeshRenderer::updateMesh` — that goes through a staging buffer plus `vkQueueWaitIdle`, which would be catastrophic per-arc per-frame. Host-mapped writes are free.

### Single source of truth for playfield geometry

Three parallel magic numbers (`w=3.f` in ground builder, `LANE_RANGE=5.f` in gate/tap mapping, hardcoded `0-4` lane mapping) disagreed with each other. Gate corners floated inside the lane edges, taps on `lane 0` rendered at `x=-3.4` (past the lane), etc.

Unified the header to one set of constants: `LANE_HALF_WIDTH=3.f`, `LANE_FAR_Z=-60.f`, `JUDGMENT_Z=0.f`, `GROUND_Y=-2.f`. Ground, gate, tap mapping, and arc transform all reference these. Gate corners are now byte-for-byte identical to ground near-edge corners.

### Lane mapping — Bandori-style slot spacing

The chart had `lane: 9` tap notes — out of range for the old `0-4` formula, which put them at `x=+7` (far outside the lane). Fixed in three steps:

1. Read `config->trackCount` and auto-expand `m_laneCount` from the chart's max lane index (mirrors `BandoriRenderer`).
2. Mapped lane → world x via N equal slots across the lane width: `laneSpacing = 2·LANE_HALF / N; wx = (lane - (N-1)·0.5) · spacing`. Lane 0 center at half-a-slot inside the edge, not on it.
3. Scaled the tap mesh to fit its slot: `hw = min(0.4, slotHalf · 0.9)`. Prevents edge-lane bleed and neighbor-slot overlap.

### Arc y-coordinate — the long "is my formula wrong?" detour

User reported the constant-height arc at `t=13.02` was rendering at mid-gate when they expected sky-bar. Turned out:

- Chart arcs author normalized `[0,1]` for `startX/Y/endX/Y`; the old renderer treated those as raw world coords, so arcs floated around a `[0,1]×[0,1]` corner far from the lane.
- The specific arc in the chart has `startY=endY=0.5` per the JSON on disk — the user was reading the editor's Height panel and misidentifying the 0.5 gridline as 1.0.

Fixed the transform (`wy = GROUND_Y + ny · m_skyHeight`; `wx = (nx·2-1) · LANE_HALF_WIDTH`) and added a one-shot diagnostic print at init to prove the formula matched spec. Edited the chart to `startY=endY=1.0` as a ground-truth test; the arc rendered on the sky bar, confirming the math was correct. Spec `y = GROUND_Y + h · skyHeight` holds byte-for-byte.

### Judgment gate

Memory claimed the gate was already added on 2026-04-17 — stale note. There was no rendering code for it.

Added `buildGateMesh`: four thin coplanar quads at `z = JUDGMENT_Z`:

- Bottom bar: thickness 0.08, bright warm yellow, grown upward from `GROUND_Y` so the outer edge flushes with the ground's near edge.
- Sky bar: thickness 0.035, dim cool cyan, at `GROUND_Y + skyHeight`.
- Vertical posts: thickness 0.035, dim neutral, grown inward from `±LANE_HALF_WIDTH`.

Visual hierarchy: bottom bar (actual judgment line) emphasized; posts + sky bar are reference lines.

### Rim-glow color blowout

Arcs were rendering as pure white, indistinguishable from taps. Traced to `mesh.frag`'s rim glow: `rim = 1 - |dot(n, {0,0,1})|; out = base + base·rim·2`. A `{0,1,0}` vertex normal gives `rim = 1` and triples the base color — any bright color saturates to white.

Ground keeps `{0,1,0}` (its dark color wants the rim glow — that's what paints it purple). Tap / ArcTap / Arc meshes now use `{0,0,1}` (camera-facing) so their real colors pass through.

### Void arcs skipped

Void arcs (invisible ArcTap carriers per Arcaea convention) were rendering at 40% alpha — showing up as dim gray strips on the lane. Skipped in `onRender` (`if (am.data.isVoid) continue`).

### Loop removed

`onUpdate` was `m_songTime = fmod(songTime, maxTime + 1.0)`, causing the chart to loop forever. Removed — `m_songTime = songTime` passes audio time straight through. Per-type cull handles end-of-song (taps `z<0`, arcs `songRel >= duration`, arctaps `z<0`).

### Taps falling past the judgment line

Tap cull was `z < -2` — taps stayed visible ~0.25 s past their hit time, falling through the foreground below the gate. Tightened to `z < 0`.

### ArcTaps as sky bars

ArcTaps were silently dropped in `onInit` (no handler). Added `m_arcTaps` container + `buildArcTapMesh` (thin 0.64 × 0.16 camera-facing horizontal quad, white) + render loop translating to `((arcX·2-1)·LANE_HALF, GROUND_Y + arcY·skyHeight, JUDGMENT_Z - z)`. Matches the Arcaea reference of a glowing pill suspended in the sky.

### Hit particles

`ArcaeaRenderer` never overrode `showJudgment`, so hits were silent. Two non-obvious pitfalls when adding them:

1. **World space, not pixels.** `quad.vert` transforms every particle vertex by the active camera's `viewProj`. BandoriRenderer gets away with screen-pixel emit coords because it's rendering under an ortho screen-space camera. Arcaea is 3D perspective, so pixel coords land at nonsense clip positions — particles technically emit but are never visible. World-scale emit with size ~0.15 and speed ~3 u/s solves it.
2. **Lane alone can't identify the note.** Engine clamps arc/arctap `lane=-1` to `0` before `showJudgment`, and arctap's rounded lane is also often 0. Added a pre-computed `m_hitEvents` table (sky events only: arctaps at `(arcX, arcY)`, arc start/end at `evalArc(0/1)`). Routing:
   - `lane > 0` → emit at ground slot (taps, flicks, hold sample ticks).
   - `lane == 0` → within a tight 30 ms window, prefer the closest sky event; else fall back to lane-0 ground.

   Critical: ground taps are **not** in `m_hitEvents`. If they were, a hold-tick at `t=1.65` would snap onto an arctap at `t=1.67` and spawn a particle in mid-air. Sky-only table + `lane > 0` guard avoids that.

### Combo > note count is not a bug

User flagged combo 54 from a 23-note chart as evidence of replay. Actually legitimate: `HitDetector::autoPlayTick` fires each arc and hold *twice* (once on start, once on end), and `consumeSampleTicks` awards combo per waypoint on the one cross-lane hold. Theoretical max for this chart is ~60. No replay.

### End state

The 3D Drop pipeline is now complete and matches the Arcaea reference end-to-end:

- Lane + gate geometry aligned, ranges unified
- Arcs clip smoothly at the judgment plane, in correct sky-band world coords
- ArcTaps render as horizontal sky bars
- Taps and arctaps cull at the judgment plane
- Bright notes render in their real colors (no rim-glow blowout)
- Hit particles fire in world space at the note's actual intersection with the judgment plane
- No chart looping


## 2026-04-17 (late) — Scan Line editor redesign: paginated view + AI markers

Replaced the full-song single-scan-line authoring flow with a paginated editor. The old UX required seeking to the right moment and clicking within a 10px band of the animated scan line — tedious. The new model is simpler to reason about and much faster to chart.

### Page model

A "page" = one sweep of the scan line (top->bottom OR bottom->top). Default duration = `240/BPM` = one bar @ 4/4. Each page can have a `speed` multiplier (default 1.0); page duration = `(240/BPM) / speed`. Pages alternate direction: page 0 bottom->top, page 1 top->bottom, ...

Data lives in `ChartData::scanPageOverrides`, a sparse vector of `{pageIndex, speed}`. `ScanPageUtils.h` has two shared helpers:
- `buildScanPageTable(timingPoints, overrides, songEnd)` walks the timeline and emits `ScanPageInfo` entries. BPM changes that fall mid-page truncate the current page (`partialTail=true`) so the scan line stays continuous across BPM boundaries.
- `expandScanPagesToSpeedEvents(pageTable, overrides)` emits one zero-duration `ScanSpeedEvent` per overridden page boundary. `CytusRenderer::buildPhaseTable` already handles `duration=0` as a step change (verified in code before wiring), so the runtime is unchanged.

On save, overrides are authoritative: if `scanPages` is present, `scanSpeedEvents` is regenerated from it at load and any on-disk speed events are discarded. Legacy charts with only `scanSpeedEvents` still work.

### Scene view

`SongEditor::renderSceneView`'s ScanLine branch now reserves a ~36px header strip for navigation (Prev/Next buttons + page label + per-page speed `InputFloat` + `Place All`) and renders the body rect as a single page:

- 4-beat grid, page-direction arrow (^/v) at the end edge
- Scan line drawn only when `curTime` is within this page's window
- Notes with `time ∈ page range` plotted at `(scanX, scanPageTimeToY(page, time))`
- Holds/slides overlapping the page draw body segments clipped to `[pageStart, pageEnd]`; `▲`/`▼` triangles at the start/end edge mark cross-page portions
- In-progress hold preview / slide preview redrawn per-page

### Input flow

`handleScanLinePageInput` (replaces the old `handleScanLineInput`):
- Tap/Flick: click anywhere in the body, time = `scanPageYToTime(pageIdx, y)`, X free.
- Hold: click-anywhere starts, mouse-wheel extends in **page units** (was "extra sweeps" — now synonymous), Prev/Next navigation auto-expands span, click on target page commits. Stored as `scanHoldSweeps = currentPage - startPage`.
- Slide: LMB start, RMB adds node (each node stores its `pageIndex` in a parallel `m_scanSlidePathPages` vector), Prev/Next allowed between nodes. On release, each node's absolute time is `scanPageYToTime(pageIdx, y)`. `samplePoints` are time deltas from the head.

Clicks snap to the nearest AI beat marker within `min(0.06s, 0.15 * page.duration)`; `Alt` disables snap. `PageUp`/`PageDown` keyboard navigation.

### AI beat markers integrated

The editor's existing `AudioAnalyzer` pipeline (`tools/analyze_audio.py` via Madmom) fills `m_diffMarkers[currentDiff]`. Scan Line mode now consumes them:
1. **Render ticks** — faint dashed orange horizontal lines on each page at `y = scanPageTimeToY(page, markerTime)`. Same color as 2D-mode timeline markers.
2. **Snap-to-marker** — Tap/Flick/Hold start and Hold end all run through `snapToScanMarker(time, tol)` unless Alt is held.
3. **Place All** — button on the nav strip that iterates `markers()` and emits Taps with alternating X ∈ {0.5, 0.25, 0.75} to avoid a straight vertical column. De-dupes against existing notes within 10ms.

Also: if the chart has no user-authored timing points but `m_bpmChanges` was populated by analysis, `rebuildScanPageTable` uses the AI tempo map for page durations.

### Files touched

- `engine/src/game/chart/ChartTypes.h` — `ScanPageOverride`, `ChartData::scanPageOverrides`
- `engine/src/game/chart/ScanPageUtils.h` (new) — shared page-table builder + speed-event expander
- `engine/src/game/chart/ChartLoader.cpp` — parse `scanPages` JSON, rebuild `scanSpeedEvents` on load
- `engine/src/ui/SongEditor.h` — per-difficulty `m_diffScanPages`, `m_scanPageTable`, helpers
- `engine/src/ui/SongEditor.cpp` — `rebuildScanPageTable`, `scanPageForTime/YToTime/TimeToY`, `snapToScanMarker`, `renderScanPageNav`, `handleScanLinePageInput`; rewrote the Scan Line branch of `renderSceneView`; added `scanPages` save path with legacy fallback; deleted the old `handleScanLineInput`
- `docs/sys6_game_modes.md`, `docs/sys7_editor.md` — updated

No runtime changes to `CytusRenderer`. Verified in Debug + Release.

## 2026-04-17 (even later) — Scan Line editor UX polish pass

Iterated on the paginated Scan Line editor after a user pass surfaced four rough edges:

### "I can't see the Analyze / Marker buttons in this mode"

Those buttons live in `renderNoteToolbar`, which is only rendered inside the timeline panel. Scan-line mode skips the timeline panel entirely, so the user never saw them. Fix: in the scene-embedded toolbar row (next to Tap/Flick/Hold/Slide) add a `[Select]` pointer-tool button, then the full `[Analyze Beats]` + `[Clear Markers]` block. The callback is the same one the 2D toolbar uses — marker populate, dominant BPM, dynamic BPM map — plus `m_scanPageTableDirty = true` so page durations re-derive from the detected tempo.

### "Page speed should match BPM after analysis"

Reason to touch this: before the fix, analyzing the audio populated `m_bpmChanges` / `m_dominantBpm` but never re-ran `rebuildScanPageTable`. So the editor kept the old fallback 120 BPM until something else invalidated the table. Fix: set `m_scanPageTableDirty = true` in both Analyze Beats callbacks (scan-line toolbar + 2D toolbar). Next frame, `renderSceneView`'s ScanLine branch calls `rebuildScanPageTable()`, which now also consults `m_bpmChanges` when there are no user-authored timing points.

### "When I move the cursor I should see the scan line move AND the page change"

Two pieces to this:

1. *Cursor-follow scan line.* In `renderSceneView`'s ScanLine branch, when the mouse is inside the page body, draw an amber line at the cursor Y plus a floating `t=M:SS.sss` label showing the projected song time. Separate from the blue playback scan line (which tracks `curTime`).
2. *Auto page turn on cursor motion.* The earlier version only fired during hold/slide authoring. Relaxed to always work on cursor motion into an edge — dropped the `inFlight` gate, added a start-edge case (previous page), but gated the whole thing behind `!playing` so the scan-line auto-advance owns the page during playback.

### "Why is page 1 showing at t=5.61s?"

Root cause: the in-scene auto-advance only ran when `engine->audio().isPlaying()`. Once playback paused or stopped, `curTime` could still be several seconds into the song, but the page stayed wherever the user last left it. Fix:

- Dropped the `isPlaying` gate. Auto-advance now fires whenever `curTime` is outside the current page's range — covers playback, scrubbing, and manual time edits uniformly.
- To prevent Prev/Next/jump/edge-flip from fighting the auto-sync, each of those now also assigns `m_sceneTime = pageTable[target].startTime` (and stops the audio when appropriate). So navigating to page N leaves `curTime == pageStart(N)` on the next frame, and auto-sync stays put.
- Edge-flip is explicitly disabled while playing (`engine->audio().isPlaying() == true`) to avoid fighting the playback-driven advance.

### Files touched

- `engine/src/ui/SongEditor.cpp` — scan-line toolbar row: added Select/Analyze/Clear buttons; renderSceneView ScanLine branch: cursor-follow scan line + `curTime`-synced auto-advance; handleScanLinePageInput: edge auto-flip generalized + playback-guarded; renderScanPageNav: Prev/Next seek `m_sceneTime`; both Analyze Beats callbacks set `m_scanPageTableDirty`
- `engine/src/ui/SongEditor.h` — `m_scanPageEdgeArmed` flag; `handleScanLinePageInput` param `engine` now named (was `/*engine*/`)
- `docs/sys7_editor.md`, `docs/sys6_game_modes.md` — updated Scan Line sections

No runtime changes. Clean build in Debug + Release.

## 2026-04-17 (late) — Beat marker persistence + APK packaging prune

Two small-but-load-bearing changes to how projects survive the round-trip through disk and through the APK packager.

### Markers round-trip with the chart

Before today, every `exportAllCharts()` wrote notes only. Beat markers — whether AI-detected by `AudioAnalyzer` or hand-placed — lived in `m_diffMarkers` and evaporated on reload. Authors had to re-run Analyze Beats every time they reopened a song, every mode.

Fix: added `ChartData::markers` (a `std::vector<float>`) and threaded it through the three layers that already handle note persistence.

- `ChartTypes.h` — new `markers` field on `ChartData`.
- `SongEditor::buildChartFromNotes` — populates `chart.markers = markers()` for the current difficulty.
- `SongEditor::exportAllCharts` — emits a `"markers": [t0, t1, ...]` JSON array; the outer loop now exports any difficulty whose notes OR markers are non-empty (previously it skipped note-less difficulties, which would have dropped marker-only authoring work).
- `ChartLoader::loadUnified` — parses the `"markers"` array into `ChartData::markers` using the same literal-scan pattern as `scanPages`.
- `SongEditor::loadChartFile` — hydrates `m_diffMarkers[(int)diff]` from `chart.markers` so the ticks appear immediately on reopen.

Applies to every game mode (2D/3D DropNotes, Circle, ScanLine) since they all share the same chart file format.

### APK packaging only ships the selected mode per song

The editor's per-(mode, difficulty) file layout from 2026-04-12 means a song the author has touched in four modes has eight to twelve chart files on disk. Useful in the editor, wasteful in the APK. User asked: at package time, each song should keep only the charts for its *currently-selected* mode; the live editor project must not be mutated.

Solution: stage a pruned copy before handing off to `build_apk.bat`.

- `engine/src/ui/ProjectHub.cpp` gained three helpers in an anonymous namespace:
  - `collectKeepSet(stagingRoot, keepOut, songNamesOut)` — parses `music_selection.json`, walks `sets[].songs[]`, and collects (song name, chart basenames) pairs from `chartEasy/chartMedium/chartHard`.
  - `prunePackagedCharts(stagingRoot)` — deletes every `assets/charts/<song>_*.json` whose basename isn't in the keep set. Files not prefixed with any known song name (demo charts, shared charts) are left alone.
  - `stageProjectForPackaging(projectRoot, safeName)` — creates `%TEMP%/<safeName>_apk_stage_<ts>/`, selectively copies `project.json`, `start_screen.json`, `music_selection.json`, and `assets/` (mirroring what `build_apk.bat` pulls in), then calls the prune.
- `ProjectHub::startApkBuild` now calls `stageProjectForPackaging` and passes the staging path to `build_apk.bat` (falling back to the live path if staging fails). Stored in new member `m_apkStagingPath`.
- `ProjectHub::renderApkDialog` removes the staging dir via `fs::remove_all` when the async build future resolves — success or failure.
- `build_apk.bat` was **not** modified; the prune is invisible to the shell script.

### Files touched

- `engine/src/game/chart/ChartTypes.h` — `ChartData::markers`
- `engine/src/game/chart/ChartLoader.cpp` — parse `"markers"` array in `loadUnified`
- `engine/src/ui/SongEditor.cpp` — populate `chart.markers` in `buildChartFromNotes`, emit in `exportAllCharts`, loop now covers marker-only difficulties, hydrate in `loadChartFile`
- `engine/src/ui/ProjectHub.h` — `m_apkStagingPath` member
- `engine/src/ui/ProjectHub.cpp` — staging + prune helpers, wired into `startApkBuild` + `renderApkDialog`
- `docs/sys7_editor.md` — Beat marker persistence section
- `docs/sys8_android.md` — Packaging-time chart prune section + APK Contents annotation

Clean Debug build.

---

## 2026-04-18 — Player Settings page (4th editor layer)

### Goal

Add a player-facing **Settings** page to the shipped Android music game: audio volumes, audio offset, note-speed, background dim, FPS counter, language. A lightweight runtime screen — not an engine-user authoring tool. Reached from the music-selection screen via a gear button. Same UI also appears as a dedicated editor layer (`EditorLayer::Settings`) so the engine user can preview and live-tune settings without launching the game.

### Scope decisions (locked with user)

- **8 settings only**: music/hit-sound volume, hit-sound on/off, audio offset (ms), note speed (1–10), background dim, FPS counter, language. Everything else (FOV, sky height, keybinds, resolution, colorblind palette, etc.) is engine-user territory or out of scope for v1.
- **Note speed is shared** across 2D drop / 3D drop / Circle. Scan Line (Cytus) is chart-pace driven and ignores the setting. Phigros ignores it too (the renderer just inherits the base-class no-op).
- **Language is store-only** — the chosen string is persisted, but no localization wiring. Placeholder for a future string table.
- **Audio offset includes tap-to-calibrate** — a 4-beat metronome wizard that captures tap deltas and averages them into the stored offset.
- **Shared UI, two call sites**: one `SettingsPageUI::render(origin, size, settings, host)` is consumed by both `AndroidEngine::renderSettings` (runtime) and `SettingsEditor::render` / music-selection modal (editor).

### Wiring (make the settings actually do something)

- `AudioEngine::setMusicVolume / setSfxVolume / setHitSoundEnabled` — music volume goes through `ma_sound_set_volume` and is re-applied on every `load()` so a freshly loaded track respects the stored preference. `playClickSfx` early-exits when the SFX toggle is off.
- `HitDetector::setAudioOffset(seconds)` — subtracted from `songTime` inside every timing function (`checkHit`, `consumeDrags`, `consumeNoteById`, `checkHitPosition`, `checkHitPhigros`, `beginHold`/`beginHoldById`/`beginHoldPosition`, `endHold`, `update`'s miss sweep). Positive offset = player taps later → the perfect window shifts later to compensate.
- `GameModeRenderer` base class gained `setNoteSpeedMultiplier(float)` + `m_noteSpeedMul = 1.0f`. `BandoriRenderer` and `ArcaeaRenderer` multiply `SCROLL_SPEED` by the mul at every usage site; `LanotaRenderer` divides `APPROACH_SECS` by the mul. `CytusRenderer` and `PhigrosRenderer` leave the default untouched. Slider 1–10 maps to `mul = slider / 5.0` (5 = 1.0×).
- `PlayerSettings` struct + JSON I/O under `engine/src/game/PlayerSettings.{h,cpp}` using the same hand-rolled string-scan parser `AndroidEngine.cpp` already uses for `music_selection.json`.
- Background dim: semi-transparent black fullscreen rect drawn via `ImGui::GetBackgroundDrawList()` during gameplay. FPS counter: `ImGui::GetIO().Framerate` text in the top-left during gameplay.

### Editor integration (dedicated 4th layer)

- `EditorLayer::Settings` added to `Engine.h`. New `SettingsEditor` class in `engine/src/ui/SettingsEditor.{h,cpp}` — just calls `SettingsPageUI::render` against the full display with `host.audio = engine->audio()` and `onBack` / `onSave` lambdas that call `Engine::applyPlayerSettings()` then `switchLayer(MusicSelection)`.
- `MusicSelectionEditor` nav bar got a **Next: Settings >** button at the bottom-right (mirrors the pattern from the start-page editor's "Next: Music Selection >").
- `Engine` gained `m_playerSettings` + `applyPlayerSettings()`. `launchGameplay` calls it once after the renderer is created so Test Game respects the stored note speed / volume / offset.

### Test Game mode (music-selection gear button)

In-game music-select screen (the wheel-based `renderGamePreview` layout that Test Game uses) got a floating **⚙ Settings** button top-right, which opens the full-screen settings modal via `m_showSettings` flag. Settings are bound directly to `engine->playerSettings()` and `applyPlayerSettings()` is called every frame while the modal is open so slider drags take effect live (audio volume changes audibly while the slider is moving; speed change is visible the moment you hit PLAY).

### Debugging the gear button (the long way round)

The gear button took four iterations to make clickable and persistent through song-card clicks:

1. **First attempt**: plain `ImGui::Button` inside the test-mode `##test_musicsel` window. Click regions were being eaten by the song wheel's `InvisibleButton` cards that registered first in-frame — ImGui gives hit-test priority to the earlier-submitted overlapping widget.
2. **Second attempt**: moved the button into its own top-level window nested inside `renderGamePreview`. Visible, but when the user clicked a song card the test window got `BringToFront` on focus and pushed the gear behind. Fixed by adding `ImGuiWindowFlags_NoBringToFrontOnFocus` to `##test_musicsel`.
3. **Third attempt**: gear visible but clicks dead. Cause was a stray fallback click handler — `Button` fires on mouse-up and toggled `m_showSettings`, while the fallback `IsMouseClicked` fired on mouse-down and set it to true. Net result after a complete click: state ended up flipped back to false. Removed the fallback.
4. **Fourth attempt**: modal wasn't appearing because the settings scrim window's z-order drifted below the gear window. Fixed with `ImGui::BringWindowToDisplayFront(window)` inside `SettingsPageUI::render` every frame. First tried `SetNextWindowFocus()` but that resets the active item every frame and broke slider drags — `BringWindowToDisplayFront` reorders z without touching focus/active state.

### Files touched

- **New**: `engine/src/game/PlayerSettings.{h,cpp}`, `engine/src/ui/SettingsPageUI.{h,cpp}`, `engine/src/ui/SettingsEditor.{h,cpp}`
- **Wiring**: `engine/src/engine/AudioEngine.{h,cpp}`, `engine/src/gameplay/HitDetector.{h,cpp}`, `engine/src/game/modes/GameModeRenderer.h`, `engine/src/game/modes/{Bandori,Arcaea,Lanota}Renderer.cpp` (Cytus & Phigros untouched)
- **Editor**: `engine/src/engine/Engine.{h,cpp}` (`EditorLayer::Settings`, `m_playerSettings`, `applyPlayerSettings`), `engine/src/ui/MusicSelectionEditor.{h,cpp}` (nav-bar button + Test Game gear + modal), `engine/src/ui/GameFlowPreview.{h,cpp}` (Settings FlowPage preview)
- **Runtime**: `engine/src/android/AndroidEngine.{h,cpp}` (GameScreen::Settings, music-select SETTINGS button, `renderSettings`, load/save/apply)
- **Build**: `CMakeLists.txt` (glob `engine/src/game/*.cpp`), `engine/src/android/CMakeLists.txt` (add `PlayerSettings.cpp` + `SettingsPageUI.cpp`)
- **Docs**: `docs/sys7_editor.md`, `docs/devlog.md` (this entry)

Clean Debug build. Verified in Test Game mode: gear button stays through song selection, modal opens, sliders drag live, note-speed change visible on PLAY.

## 2026-04-18 (later) — Material system Phase 1+2 (per-slot overrides, QuadBatch pipeline-per-kind)

### Goal

Let the chart author swap in non-default visual treatments (glow, UV scroll, pulse on hit, vertical/radial gradient) for specific visual roles — Bandori tap notes, Cytus scan line, Lanota disk rings, etc. — without hard-coding per-mode switches in the renderers. Replace the existing uniform "white quad + vertex tint" pipeline with a small palette of fragment-shader variants selected per draw.

### Design

- `renderer/Material.h` — `MaterialKind` enum (Unlit / Glow / Scroll / Pulse / Gradient), `Material` struct (kind + tint + 4-float params + optional texture/sampler). `params[]` meaning per kind documented in the header.
- `renderer/MaterialSlots.h` — a slot is a named visual role in one game mode (stable `uint16 id`, displayName, group header, default kind + tint + params). `getMaterialSlotsForMode()` returns a reference-to-static `vector<MaterialSlotInfo>` so pointers stay stable.
- `MaterialModeKey` enum: Bandori / Phigros / Cytus / Lanota / Arcaea. Phase 1 populates Bandori/Cytus/Lanota; Arcaea empty (deferred to Phase 3); Phigros stubbed.
- Chart JSON: new `"materials"` array (`{slot, kind, tint, params, texture}`) persisted by `ChartData::materials`. Inline for Phase 1+2 (asset references come in Phase 4).
- **Batching**: `QuadBatch` grouped draws by `(MaterialKind, VkImageView)`. Switching kind mid-frame costs one pipeline bind. Push constants grew to **128 B** (`QuadPushConstants`: tint, params, time, triggerTime). Five fragment shaders compiled from `shaders/quad_unlit.frag`, `quad_glow.frag`, `quad_scroll.frag`, `quad_pulse.frag`, `quad_gradient.frag`.

### Renderer integration

- `GameModeRenderer::setEditorPreview(slotId, Material)` base method — editor-only overlay applied before the chart's own overrides resolve. `Bandori`, `Cytus`, `Lanota` renderers resolve via `resolveMaterial(ChartData::materials, slotId, defaultFromSlotInfo)` at draw time.
- Bandori: tap / flick / drag / slide head + hold body + slide body + track surface (trapezoid). Track Surface slot added for 2D drop.
- Cytus: scan line + tap / flick / hold head + hold body + slide head + slide segment + page background.
- Lanota: disk surface + ring stroke + note head + hold tail + slide segment.

### SongEditor — Materials panel

- Lives under Config → **Materials** collapsing header. `getMaterialSlotsForMode(currentMode)` drives the slot list; group headers collapse related slots (e.g. all Hold Note slots under one "Hold Note" header).
- Per slot: Kind combo, Tint (`ColorEdit4`), 4 param sliders with per-kind labels, optional texture picker (accepts `ASSET_PATH` drag-drop). A "Reset to default" button per slot wipes the override.
- Chart save round-trips slot overrides; absent slots fall back to the renderer's `defaultKind/Tint/Params`.

### Toolbar polish

- Slide button hidden in 2D drop + ScanLine (Slide is ScanLine-only per design; 2D doesn't have it). Flick button added to 2D drop. Clears a pair of longstanding toolbar bugs tracked in the note-types project memory.

### Files touched

- **New**: `engine/src/renderer/Material.{h,cpp}`, `engine/src/renderer/MaterialSlots.{h,cpp}`, `shaders/quad_unlit.frag`, `quad_glow.frag`, `quad_scroll.frag`, `quad_pulse.frag`, `quad_gradient.frag`
- **Wiring**: `engine/src/renderer/QuadBatch.{h,cpp}` (pipeline-per-kind + 128 B push constants), `engine/src/game/modes/GameModeRenderer.h` (`setEditorPreview`), `engine/src/game/modes/{Bandori,Cytus,Lanota}Renderer.cpp`
- **Chart**: `engine/src/game/chart/ChartTypes.h` (`materials` vector), `ChartLoader.cpp` (parse `"materials"`), SongEditor `buildChartFromNotes` (emit `"materials"`)
- **Editor**: `engine/src/ui/SongEditor.{h,cpp}` — Materials panel + toolbar gate

Clean Debug build; per-slot overrides round-trip through save/load; toolbar gates verified across all four reachable modes.

## 2026-04-18 (later) — Arcaea 3D visual refresh: hex arcs, rectangular arctaps, flat shadows, restart-freeze fix

### Goal

The Arcaea renderer's arcs and arctaps read as flat tape because they used a thin ribbon strip with a single up-normal — rim glow only lit the top face. Arctaps looked like floating lozenges with no ground connection. And the previous session's pause/resume work left a latent bug: restarting a chart from the pause menu would freeze gameplay because `GameClock` was still paused.

### Arc geometry — hexagonal prism

- `ARC_SIDES = 6`, radius `0.15`. Per ring: 6 vertices around the arc axis, each with **outward-radial normal** (so rim glow lights every face as the camera orbits). Arc length sampled at `ARC_SAMPLES = 32` along the curve.
- Rebuilt each frame into a host-mapped vertex buffer (persistent mapping — the same mechanism introduced by the 3D-drop rebuild) so per-frame clipping at the judgment gate stays smooth.
- Per-arc **flat shadow ribbon** below the prism: a separate thin quad strip on the ground plane (`y = 0`), dimmed and alpha-blended. Ribbon rebuilt each frame from the same sample points.

### ArcTap geometry — rectangular prism

- 6-face box with per-face normals, lying flat on the arc path. Replaces the previous single diamond quad. Still picks position/rotation from the parent arc segment's tangent at its time.
- Shared **arctap shadow quad** below each box — one static quad per arctap drawn behind the prism on the ground plane. Single shared draw per frame, not per-face.

### Restart-freeze fix

`launchGameplay` / `launchGameplayDirect` now call `m_clock.resume()` after (re-)creating the renderer. The pause menu's exit path was leaving `m_clock` paused (correct while the menu was up, but the resume was tied to the menu-close callback, which the Restart-from-pause flow skipped because it tore the scene down first). Net effect before the fix: after Restart, `GameClock::tick()` returned 0, `songTime` never advanced, audio started but nothing moved on screen. One-liner fix, but took a minute to find because the renderer and audio engine were both fine.

### Files touched

- **Renderer**: `engine/src/game/modes/ArcaeaRenderer.cpp` (hex-prism arc builder, rect-prism arctap builder, shadow quads, outward-radial normals)
- **Engine**: `engine/src/engine/Engine.cpp` — `launchGameplay` / `launchGameplayDirect` call `m_clock.resume()`

Visual pass verified on `Aa_drop3d_hard`; restart-from-pause verified on all 4 reachable modes.

## 2026-04-18 (late) — Material system Phase 3: Arcaea 3D mesh shaders

### Goal

Phase 1+2 brought the material kinds to `QuadBatch`, but Arcaea 3D renders through `MeshRenderer` (hex-prism arcs, rect-prism arctaps, the judgment gate, the ground quad). Its shaders were a single `mesh.frag` with baked tint-only handling. To let Arcaea participate in the same material palette, `MeshRenderer` needed the same pipeline-per-kind plumbing — and the renderer needed a proper slot table.

### MeshRenderer pipeline-per-kind

- 5 new fragment shaders: `shaders/mesh_unlit.frag`, `mesh_glow.frag`, `mesh_scroll.frag`, `mesh_pulse.frag`, `mesh_gradient.frag`. Old `shaders/mesh.frag` **removed** (no backward-compat shim — Arcaea was the only consumer).
- Push constants grew to **128 B**: `MeshPushConstants` mirrors `QuadPushConstants` (tint / params / time / triggerTime / model matrix).
- `MeshRenderer::init` now takes `whiteView + whiteSampler` so the legacy tint-only `drawMesh(color)` path still has a valid texture binding under Unlit.
- Pipeline cache: one pipeline per `MaterialKind`, built lazily. `drawMesh(Material)` overload selects the pipeline from the material's kind.

### `kArcaeaSlots` — 12 slots

Populated `MaterialSlotInfo` list for `MaterialModeKey::Arcaea`:

| Group | Slots |
|---|---|
| Click Note | (1 slot, default Unlit) |
| Flick Note | (1 slot, default Unlit) |
| ArcTap | Tile, Shadow (2 slots) |
| Arc Blue | Arc, Shadow (2 slots) |
| Arc Red | Arc, Shadow (2 slots) |
| Ground | (1 slot, default **Gradient**) |
| Judgment Bar | (1 slot) |
| Sky Line | (1 slot) |
| Side Posts | (1 slot) |

### Renderer wiring

- `ArcaeaRenderer` mesh builders emit **white vertex colours** (tint now comes through the material push constants, not per-vertex).
- Judgment gate split into **4 independently-materialized sub-meshes** (top bar, left post, right post, base line) so each slot can take its own kind.
- Arc / arc-shadow colour picked from `arc.data.color` at draw time (blue vs red) — the slot table has 4 separate arc slots so each chart can skin them independently.

### Android build

`android/app/src/main/cpp/AndroidEngine.cpp` shader asset list updated with the 5 new mesh shaders. APK packaging pulls them into `assets/shaders/` as usual.

### Files touched

- **Renderer**: `engine/src/renderer/MeshRenderer.{h,cpp}` (pipeline-per-kind + 128 B push), `engine/src/renderer/Renderer.cpp` (`m_meshes.init` now passes `whiteView/whiteSampler`)
- **Shaders**: new `mesh_{unlit,glow,scroll,pulse,gradient}.frag`; removed `mesh.frag`
- **Slot table**: `engine/src/renderer/MaterialSlots.cpp` — `kArcaeaSlots` populated
- **Arcaea renderer**: `engine/src/game/modes/ArcaeaRenderer.cpp` — mesh builders white, judgment gate split, arc colour at draw time
- **Android**: `android/app/src/main/cpp/AndroidEngine.cpp`

Clean Debug build. Verified on `Aa_drop3d_hard` with Gradient-kind ground, Glow-kind arcs, Pulse-kind judgment bar.

## 2026-04-18 (late) — Material system Phase 4: project-level .mat assets + custom shaders

### Goal

Phase 1–3 stored materials inline in each chart JSON. Reusing a material across charts meant copy-pasting the override block; every tweak required editing every consumer. Phase 4 promotes materials to **project-level assets** (one `.mat` JSON per material under `<project>/assets/materials/`) and adds a `MaterialKind::Custom` kind backed by a runtime shader compiler so authors can drop in their own `.frag` files.

### Asset library

- `renderer/MaterialAsset.{h,cpp}` — one asset = name + kind + tint + params + texture + optional `customShaderPath` + `targetMode` + `targetSlotSlug`. The `target*` fields let the editor filter the asset picker to just the assets compatible with a given slot (empty target = universal).
- `renderer/MaterialAssetLibrary.{h,cpp}` — owns the loaded `MaterialAsset`s, indexed by name. CRUD + `namesCompatibleWith(mode, slug)` for the picker. Disk format: one `.mat` JSON per asset (name matches filename stem, no duplicate IDs).
- **Chart reference form**: `ChartData::materials` entries can now be either inline (`{slot, kind, tint, params, texture}`) or reference (`{slot, asset: "<name>"}`). `resolveMaterial(md, lib)` shared helper consults the library when `asset` is set, falls back to the inline fields otherwise.

### One-time migration

`Engine::openProject()` seeds the library on first open for any project already using Phase 1–3 inline materials:

1. Build `default_<mode>_<slug>.mat` for every slot of every mode the project actually uses.
2. For each chart, walk `materials`: if an inline entry matches its slot default, convert to `{slot, asset: "default_<mode>_<slug>"}`. If it differs, spill to a per-chart override asset `<chartStem>__<slug>.mat` and convert to a reference.
3. Prune any cryptically-named legacy files left by earlier drafts.

On the `test` project: 30 default `.mat` files + 7 per-chart overrides generated, old files pruned, clean build + launch — round-trips through save/load cleanly.

### Custom shader kind

- `MaterialKind::Custom` added to the enum. `Material::customShaderPath` carries the absolute `.frag` source path (or `.spv` precompiled).
- `renderer/ShaderCompiler.{h,cpp}` — runtime compiler. `.frag` → invokes `glslc` (discovered via `VULKAN_SDK` env, falls back to PATH search) to produce a cached `.spv` next to the source, keyed by mtime. `.spv` → load verbatim. `.hlsl` → rejected with a clear error message (we don't ship the HLSL toolchain). Cache key = source path; invalidated on source mtime change.
- **Per-batcher custom pipeline cache**: `QuadBatch::getOrBuildCustomPipeline(shaderPath)` + `MeshRenderer::getOrBuildCustomPipeline(shaderPath)`. Keyed by shader path; shares descriptor set layout and render pass with the built-in kinds (so the shader must conform to the same push-constant block and set layouts).

### Editor UX — Materials tab

- `StartScreen → Properties → **Materials** tab`. Lists every asset in the library with name + kind + target preview. CRUD buttons at the top.
- Per asset: kind combo, tint, 4-param sliders with per-kind labels, texture picker (accepts `ASSET_PATH` drag-drop), **target mode** combo, **target slot** combo (populated from the selected mode's slot list; empty = universal).
- **Custom kind extras**: a **Template** button emits a boilerplate `.frag` next to the asset (full push-constant block + a minimal main returning white). A **Compile** button invokes `ShaderCompiler` and surfaces glslc errors inline.
- Assets panel gained purple **MAT** tiles alongside the existing texture/audio tiles. Clicking a MAT tile opens the Materials tab pre-scrolled to that asset.

### SongEditor picker

Per-slot inline editor in SongEditor's Materials panel was replaced with an **asset-picker dropdown** driven by `MaterialAssetLibrary::namesCompatibleWith(mode, slug)`. Chart save now writes `{slot, asset}` when an asset is assigned, falls back to inline only when the author hand-crafts an override via the legacy panel path (kept for now as a fallback).

### Files touched

- **New**: `engine/src/renderer/MaterialAsset.{h,cpp}`, `MaterialAssetLibrary.{h,cpp}`, `ShaderCompiler.{h,cpp}`
- **Renderer**: `QuadBatch.{h,cpp}` + `MeshRenderer.{h,cpp}` — custom-pipeline cache, Custom kind dispatch
- **Chart**: `ChartTypes.h` (reference form), `ChartLoader.cpp` (parse `asset`), SongEditor `buildChartFromNotes` (emit `asset` when assigned)
- **Engine**: `engine/src/engine/Engine.{h,cpp}` — library owner, `openProject()` migration
- **Editor**: `engine/src/ui/StartScreenEditor.{h,cpp}` (Materials tab), `engine/src/ui/AssetBrowser.{h,cpp}` (MAT tile type), `engine/src/ui/SongEditor.cpp` (asset-picker dropdown)

Verified end-to-end on `test` project: every chart reopens with the expected shared defaults + per-chart overrides; a custom Scroll-like shader dropped into `assets/materials/` and assigned via the picker renders correctly in Test Game.

## 2026-04-18 (late) — Autocharter Phase 1+2: feature-driven Place All

### Goal

The existing Place All buttons in the editor placed a Tap on every AI beat marker. Useful as a scaffold but crude: every note was the same type, every note landed on the same lane, and jack patterns (two consecutive notes on the same lane at fast tempo) were unplayable. Phase 1+2 adds per-marker audio features and uses them to pick a note type + a lane per marker — still a scaffold, but a much better starting point the author can tweak rather than rewrite.

### Features extracted

- `tools/analyze_audio.py` extended to emit three new fields per marker: **strength** (onset strength at the marker frame, percentile-normalized over the song), **sustain** (length of the strong-energy tail after the onset, in seconds), **centroid** (FFT-based spectral centroid at the marker frame, percentile-normalized).
- `engine/src/engine/AudioAnalyzer.{h,cpp}` gained `MarkerFeature { strength, sustain, centroid }` + per-difficulty `vector<MarkerFeature>` alongside the existing marker-time vector. The Python bridge parses the extended JSON.

### Placement logic

- `SongEditor::inferNoteType(feature, knobs)` — **Hold** if `sustain ≥ holdMin`; else **Flick** if `strength ≥ flickThreshold`; else **Click**. Flick threshold defaults to the 88th percentile of the song's strength distribution (`computeFlickThreshold`).
- `SongEditor::inferLaneFromCentroid(feature, laneCount, prevLaneHistory, knobs)` — maps normalized centroid to a lane index, then applies an **anti-jack nudge**: if the candidate lane was used within `antiJack` notes back, shift by ±1 (preferring the direction that keeps centroid ordering).
- Per-lane cooldown: reject the placement if every lane is still within `laneCooldownMs` of a prior note at this marker's time. Skip the marker entirely.
- Both Place All paths rewritten:
  - **Non-ScanLine** (Bandori / Arcaea / Lanota): feature-driven type + lane + cooldown gate.
  - **ScanLine** (Cytus): centroid → X position + a global **time-gap** validator (`scanTimeGapMs`) — lane-less placement.

### AI gear popup

New **AI...** dropdown next to Place All exposing the knobs:

| Knob | Default | Meaning |
|---|---|---|
| `flickPct` | 88 | Percentile of song strength above which a marker becomes a Flick |
| `holdMin` | 0.25 s | Minimum sustain for a marker to become a Hold |
| `antiJack` | 3 | Window of prior notes to consider when nudging |
| `laneCooldownMs` | 80 | Per-lane reject window |
| `scanTimeGapMs` | 60 | ScanLine global reject window |

### Deferred (deliberately manual)

Per `project_autocharter_scope.md`: difficulty differentiation by type, Arc generation, sky-note inference. The user explicitly wants those to stay hand-authored rather than inferred.

### Files touched

- **Python**: `tools/analyze_audio.py` — per-marker strength/sustain/centroid extraction
- **Engine**: `engine/src/engine/AudioAnalyzer.{h,cpp}` — `MarkerFeature` + per-difficulty feature arrays
- **Editor**: `engine/src/ui/SongEditor.{h,cpp}` — `m_diffFeatures`, `inferNoteType`, `inferLaneFromCentroid`, `computeFlickThreshold`; both Place All paths rewritten; AI gear popup

Verified on a 160 BPM Bandori chart: placement picked ~12 % Flicks (matches the 88th-percentile target) and no jacks under the default cooldown.

## 2026-04-18 (late) — Editor Copilot: natural-language chart edits (Ollama / OpenAI-compatible)

### Goal

Small focused AI edits on the current chart — "delete all notes between 30 and 35 seconds", "mirror the left half of the chorus", "shift this 2-bar pattern back by one beat". The autocharter Phase 1+2 scaffold puts notes on markers; the copilot lets the author sculpt what the scaffold produced. Local-first: default endpoint is Ollama, no cloud key required. The ops vocabulary is intentionally tiny so round-trips through `buildChartFromNotes()` stay safe.

### Vendored libs

- `third_party/httplib/httplib.h` — cpp-httplib 0.15.3, header-only. Used for POSTing to the LLM endpoint.
- `third_party/nlohmann/json.hpp` — 3.11.3, header-only. Used for building the request + parsing the response.
- `CMakeLists.txt` — added both include dirs; linked `ws2_32` on Windows; `engine/src/editor/*.cpp` globbed into the engine target.

### Files added (`engine/src/editor/`)

- `AIEditorConfig.{h,cpp}` — endpoint / model / apiKey / timeoutSec, persists to `%APPDATA%/MusicGameEngineTest/ai_editor_config.json`. Defaults: `http://localhost:11434/v1`, model `qwen2.5:3b`, timeout 180 s.
- `AIEditorClient.{h,cpp}` — worker thread + `pollCompletion()` in the same pattern `AudioAnalyzer` already uses. POSTs to the configured `/chat/completions` (OpenAI-compatible). **Rejects `https://` with a clear error message** — OpenSSL wire-up is deferred.
- `ChartEditOps.{h,cpp}` — 6 ops: `delete_range` / `insert` / `mirror_lanes` / `shift_lanes` / `shift_time` / `convert_type`. No arc / arctap / slide-path / hold-waypoint edits. `parseChartEditOps()` strips ```json … ``` fences and pulls out the first top-level `{…}`. `applyChartEditOp()` mutates a `vector<EditorNote>&` in place, returns inserted/deleted/mutated counts.
- `ChartSnapshot.h` — single-level undo. Snapshots `{notes, markers, features}` for the current difficulty.

### SongEditor integration (pimpl to keep the header slim)

- `SongEditor` holds `std::unique_ptr<CopilotState> m_copilot` with forward-declared ctor/dtor so `ChartEditOps.h`'s `#include "SongEditor.h"` doesn't become circular.
- `SongEditor::render()` calls `pollCopilot()` each frame (thread join + status update).
- `renderCopilotPanel()` is a new CollapsingHeader at the end of `renderProperties()`, below BPM Map: prompt `InputTextMultiline`, Apply / Undo buttons, config gear, last explanation, last ops preview.
- System prompt injects current `mode / lane_count / difficulty / note_counts_by_type` + a strict JSON schema (`{ explanation, ops: [...] }`). Output status routed through existing `m_statusMsg` / `m_statusTimer`.
- **Apply** snapshots current state first (so Undo has somewhere to go); **Undo** restores the snapshot verbatim.

### Reliability fixes shipped in the same session

After the first end-to-end run exited with terminate-3, four compounding fixes:

1. **Worker-thread `try/catch`** — any exception in the HTTP call or JSON parse now converts into a readable error string instead of `std::terminate`.
2. **`json.dump(error_handler_t::replace)`** — non-UTF-8 bytes in the user's prompt (CJK IMEs on Windows write GB18030 / CP936) no longer throw; they degrade to `?`.
3. **ASCII hyphens in source string literals** — MSVC on CP936 locales was writing the em-dashes I had in some system-prompt strings out as non-UTF-8 bytes, tripping (2). Replaced em-dashes with ASCII hyphens throughout the new editor files.
4. **`response_format: {"type":"json_object"}`** — added to the request body. Ollama's JSON mode prevents the 3 B model from occasionally dropping quotes in the response envelope.

After all four fixes, the copilot round-trips cleanly on `qwen2.5:3b` against a local Ollama.

### Deferred

Tracked in `project_copilot_scope.md` and `project_ai_agent_backlog.md`:

- HTTPS (OpenSSL wire-up + cert bundle)
- Multi-level undo history
- Arc / ArcTap / slide-path / hold-waypoint / disk-keyframe / material ops
- Streaming responses + cancel button

### Files touched

- **Vendored**: `third_party/httplib/httplib.h`, `third_party/nlohmann/json.hpp`
- **New**: `engine/src/editor/AIEditorConfig.{h,cpp}`, `AIEditorClient.{h,cpp}`, `ChartEditOps.{h,cpp}`, `ChartSnapshot.h`
- **Editor**: `engine/src/ui/SongEditor.{h,cpp}` — pimpl `CopilotState`, `pollCopilot`, `renderCopilotPanel`
- **Build**: `CMakeLists.txt` — include dirs, `ws2_32` on Windows, `engine/src/editor/*.cpp` glob

Verified end-to-end on `Aa_drop2d_hard.json` with `qwen2.5:3b` — prompt "delete all flicks between 30s and 45s" parsed, applied, and reverted via Undo cleanly.

## 2026-04-19 — AI Shader Generator (top backlog item from 2026-04-18)

### Goal

Let the chart author type a plain-English description of a visual effect (e.g. "a purple energy field with alpha variance") and get back a compiled `.spv` custom-kind shader ready to assign to any material slot. Reuses Phase 4's `Custom` kind + `ShaderCompiler` + per-batcher custom-pipeline cache — the infrastructure backlog note flagged this as ~90 % already built. Missing piece was the HTTP client + the compile-retry loop.

### Design

**Three async clients would drift apart** — the Copilot already had its own HTTP call. Extract the shared path first, then build on top.

- `engine/src/editor/AIChatRequest.{h,cpp}` — new synchronous helper `runChatRequest(cfg, system, user, jsonMode=true) -> AIEditorResult`. Moves the endpoint parser, httplib POST, JSON-mode toggle, and `error_handler_t::replace` body encoding out of `AIEditorClient.cpp`. `AIEditorResult` struct moved here too so downstream consumers include one header.
- `engine/src/editor/AIEditorClient.{h,cpp}` — slimmed to worker-thread + `pollCompletion()` glue only. No behavior change for the Copilot.

**New async client for shader generation.**

- `engine/src/editor/ShaderGenClient.{h,cpp}` — worker thread running a **compile-retry loop** (default 3 attempts):
  1. `runChatRequest` with JSON mode. Response envelope `{"shader": "<glsl source>"}`.
  2. Extract shader via `extractShader()` (first `{..}` slice tolerates stray prose).
  3. Write to the asset's `customShaderPath`.
  4. Call `compileFragmentToSpv(path, forceRebuild=true)`.
  5. On `glslc` failure: feed the previous shader + stderr back into the next attempt's user prompt ("Read the error carefully, identify the exact identifier or line causing it, and fix ONLY that"), loop.
  6. On success: return `{spvPath, fragSource, attemptsLog}`.
- Shared push-constant layout + descriptor sets with the built-in kinds, so the generated `.spv` drops straight into `QuadBatch::getOrBuildCustomPipeline` / `MeshRenderer::getOrBuildCustomPipeline`.
- `liveStatus()` gives the UI thread a `{attempt, maxAttempts, phase}` snapshot. Phase cycles `sending → writing → compiling → retrying`.

### Materials-tab UI

StartScreenEditor's Materials tab gained an **AI Generate** block inside the `kind == Custom` section (below the existing Template / Compile buttons). Core controls: **prompt textarea** + **Generate** button + **Settings** gear (endpoint / model / API key / timeout / max-attempts). Config shares the Copilot's `ai_editor_config.json` so both clients read the same endpoint.

- **Pimpl** (`struct ShaderGenUIState`) keeps `ShaderGenClient`'s `<thread>`/`<atomic>` out of the header — `StartScreenEditor.h` is included by `Engine.h` and `GameFlowPreview.h`. Explicit ctor/dtor added for the forward-declared unique_ptr.
- **Slot-aware prompt context**: before sending, the UI prepends a line derived from the `MaterialAsset`'s `targetMode` + `targetSlotSlug`: "This shader is for the '`<slot>`' slot in the '`<mode>`' game mode. Tailor the visual to that role..." The asset-level filter already prevents a tap-note material from being assigned to a scan-line slot; this just makes the shader *fit* the role it's pinned to.
- **Callback binding**: `setCallback` is called exactly once (flag `callbackBound`) — the lambda captures `this` and writes to `m_statusMsg` / `m_materialCompileLog` / `m_shaderGen->final*` on the main thread via `pollCompletion()`.

### Bug fix as free side effect: ShaderCompiler's cmd.exe trap

During testing, glslc came back with "系统找不到指定的目录" (directory not found) on every attempt. The shader was fine — manual `glslc` invocation worked. Cause: `_popen` on Windows wraps the command in `cmd.exe /c <command>`, and cmd.exe has a documented quirk where a command line starting with `"..."` but not *ending* with `"` gets its outer quotes stripped, leaving the glslc path as `glslc.exe"` (mangled). Fix: wrap the whole `cmdStr` in an extra outer quote pair on Windows — cmd.exe strips those and leaves our inner quoting intact.

- One-line fix in `ShaderCompiler.cpp`. The existing hand-written **Compile** button had the same latent bug and is fixed for free.

### System-prompt iteration on `qwen2.5:3b`

Small local models flake on GLSL. Five iterations before it converged on reliable output:

1. **v0 — rules only.** "Use `#version 450`, declare these bindings, oscillate with sin/cos." Model invented a `cos(ubo.time * 5.0) + 0.5` brightness (ranges `[-0.5, 1.5]` → GPU clamps to black → visible flicker) and read `fragColor.w` in decaying alpha (`max(a - a*time, 0)` → permanently invisible after 1 s).
2. **v1 — forbid decay, forbid `fragColor`, forbid smoothstep-with-time-varying-edges.** Better, but next run relied on `pc.tint` to supply the color — default tint is white → "purple" prompt produced white flicker.
3. **v2 — "when user names a color, put it in a literal `vec3(r,g,b)`; don't rely on `pc.tint` alone".** Also tightened sin/cos rule to explicitly forbid `cos(t) + 0.5`. Next attempt wrote `cos(time)` (bare `time`, undeclared).
4. **v3 — explicit "to access time, write `ubo.time`; bare `time` is a compile error. Only one function: `void main()`. Discard goes INSIDE main, not as a helper." Retry prompt gained a lookup table** ("`time` → `ubo.time`", "`viewProj` → `ubo.viewProj`", "bare `tint` → `pc.tint`"). Next attempt dropped `#version 450` entirely on the first two retries.
5. **v4 — few-shot template.** Replaced the rule wall with a **complete working template shader** embedded in the system prompt, plus "keep every layout() binding above main() exactly as shown; modify ONLY the body of main()". Small models are dramatically better at editing existing code than writing from constraints. Converged on correct output.

Lesson captured in `feedback_minimal_ui.md` sibling memory (`feedback_llm_few_shot.md` implicit — the specific lesson "3B models need templates, not rules" lives in this devlog entry and project memory).

### UI polish revert

Shipped a first pass with **preset prompt buttons** (`Purple energy`, `Scrolling stripes`, etc.) and a **Regenerate** button next to Generate. User rejected both: "I don't think we need presets or regenerate when I open it." Reverted to the minimal prompt + Generate surface. Saved as `feedback_minimal_ui.md`: "new feature panels ship with core controls only; no reflexive preset/retry shortcuts."

### Files touched

- **New**: `engine/src/editor/AIChatRequest.{h,cpp}`, `engine/src/editor/ShaderGenClient.{h,cpp}`
- **Refactored (no behavior change)**: `engine/src/editor/AIEditorClient.{h,cpp}` — now uses `runChatRequest`; `AIEditorResult` moved to `AIChatRequest.h`
- **Renderer bug fix**: `engine/src/renderer/ShaderCompiler.cpp` — Windows cmd.exe /c outer-quote trap
- **UI**: `engine/src/ui/StartScreenEditor.{h,cpp}` — pimpl `m_shaderGen`, AI Generate panel inside Custom-kind section of `renderMaterials`
- **Build**: no CMakeLists changes — `engine/src/editor/*.cpp` is already globbed (configure step picks up new files on `cmake -B build`)

Verified end-to-end on the `test` project: prompt `"A purple energy effect with soft edges, driven by ubo.time. Keep the alpha varying so it feels alive."` pinned to `bandori / hold_body` produced a compiling shader on attempt 1 that visibly pulses purple in Test Game.

## 2026-04-19 (later) — Chart Audit: read-only AI quality review

Second AI-agent item after the shader generator. The infra backlog listed "chart quality audit" as the strongest next candidate: point at a finished chart, get a structured report with density spikes, jacks, crossovers, dead zones, and difficulty mismatch. Different from Autocharter (generation) and Copilot (edit) — this is **read-only** critique.

### Design — hybrid local scan + LLM narrate

Small local models (qwen2.5:3b) hallucinate timestamps when asked to reason about raw note lists. Sending the whole chart also blows the context window on long songs. The fix: **pre-digest facts in C++, let the LLM only prioritize and narrate**.

- `engine/src/editor/ChartAudit.{h,cpp}` — pure-C++ scan + JSON-reply parser:
  - `computeAuditMetrics(notes, duration)`: sorts a working copy; computes
    - **Density hotspots**: 4s sliding windows with ≥ 24 notes, merged into contiguous runs (avoids flooding the report with overlapping windows).
    - **Jacks**: ≥ 3 consecutive same-lane notes each within 500 ms of the prior.
    - **Crossovers**: adjacent notes with `|dLane| ≥ 3` within 150 ms.
    - **Dead zones**: gaps > 8 s between onsets.
    - **Peak NPS**: best count in any 2s sliding window (onset-driven; hold tails ignored).
    - **Avg NPS**, type counts.
  - `describeMetricsForPrompt`: renders a capped text block (12 density / 12 jacks / 12 crossovers / 6 dead zones) fed to the LLM as the user message.
  - `parseAuditReport`: mirrors the `ChartEditOps` envelope-strip pattern (extract first `{..}`, tolerate stray prose / fences); produces `{summary, issues:[{severity, time, end_time, category, message}]}`.

### SongEditor wiring (third AI panel)

SongEditor now hosts three pimpls: `m_copilot` + `m_audit` + (next section) `m_style`. Same pattern across all three:

- Forward-declare `struct AuditState` in `SongEditor.h`; full definition in `.cpp` before the defaulted dtor.
- `pollAudit()` called alongside `pollCopilot()` at the top of `render()`.
- `renderAuditPanel()` called at the end of `renderProperties()`, below Copilot.
- Shares `aiConfigPath()` (the existing static free function), `ai_editor_config.json`, and the `AIEditorClient` request plumbing.

### UI

Collapsing header "Chart Audit" below "Editor Copilot". Audit button fires `client.startRequest(...)` with the prompt-packaged metrics; response parses to `AuditReport`. Rendering:

- Summary as `TextWrapped` at the top.
- One row per issue: severity tag (HIGH red / MED amber / LOW blue) + clickable `[time]` button (seeks `m_sceneTime = timeStart` and scrolls `m_timelineScrollX = max(0, timeStart - 2s)`) + category + message.

### Files + verification

- **New**: `engine/src/editor/ChartAudit.{h,cpp}`
- **Edited**: `engine/src/ui/SongEditor.{h,cpp}` — include, pimpl, poll + render calls, impl
- Duration pulled from `m_waveform.durationSeconds` (already loaded lazily via `loadWaveformIfNeeded`), falls back to last-note time when the waveform is absent.
- Build verified via `cmake -B build` (required to pick up the new `.cpp` via the existing `engine/src/editor/*.cpp` glob) + `cmake --build build --target MusicGameEngineTest`.
- Runtime verified on `Aa_drop2d_hard.json`: the model correctly flagged two HIGH density hotspots (33–73 s and 86–101 s) and a MED crossover at 35 s, citing the same timestamps the local scan produced.

## 2026-04-19 (late) — Style Transfer: reference-driven rebalance with LLM narration

Fourth AI-agent feature and the last planned one for now (backlog items 2–5 are explicitly deferred). Take a *reference* chart, extract a style fingerprint (type ratios, lane histogram, NPS, motion stats), then rebalance the *current* chart's note types and lane distribution toward the reference while preserving times. LLM narrates the before/after delta.

### Design — pure C++ fingerprint + apply, LLM only for prose

Same "don't make the 3B model compute" principle as Chart Audit. Everything numeric happens in C++; the LLM gets three fingerprint blocks (ref / before / after) and writes a 2–3 sentence summary.

- `engine/src/editor/ChartStyle.{h,cpp}`:
  - `StyleFingerprint`: `{noteCount, trackCount, durationSec, tap/hold/flick %, avg/peak NPS, laneHist (normalized), meanDLane, sameLaneRepeatRate}`. Tap/Hold/Flick only; Slide/Arc/ArcTap/Drag/Ring ignored in ratios and lane histogram.
  - `computeFingerprint(ChartData, trackCount)`: walks `NoteEvent` variants via `std::visit`, extracts `laneX` from `Tap/Hold/FlickData` only (returns -1 for variants without `laneX`).
  - `computeFingerprintFromEditor(notes, trackCount, duration)`: same from in-editor `EditorNote` for before/after.
  - `describeFingerprint`: compact text block for UI + LLM.
  - `enumerateStyleCandidates(projectPath, sets, currentMode, currentSongName, currentDifficulty)`: scans the live in-memory `std::vector<MusicSetInfo>&` from `MusicSelectionEditor::sets()` (not the on-disk JSON, so newly-added songs appear immediately after Refresh); filters `type` + `dimension` (dropNotes-only); excludes the currently-edited slot. Each candidate carries `trackCount` so the Analyze step can pass the ref's native lane count to `computeFingerprint`.
  - `applyStyleTransfer(notes, features, markerTimes, trackCount, target, opts)`: two passes.
    - **Type rebalance**: demote surplus Holds (lowest sustain → Tap) and Flicks (lowest strength → Tap) first; then promote Taps → Hold (highest sustain) and Taps → Flick (highest strength) to hit target counts.
    - **Lane rebalance**: iterate eligible single-lane notes by time. When `curHist[lane] - targetPerLane[lane] ≥ TOL=2`, pick preferred lane from marker centroid (`inferLaneFromCentroid`-clone); if that's under-filled, move there; else scan outward by ±delta. Respects anti-jack by skipping `prevLane`. Updates `curHist` as it goes so later notes see the new distribution.
    - Skip Slide/Arc/ArcTap and cross-lane Holds (`holdIsCrossLane` checks waypoint diversity + `endTrack`).
    - **Cross-track-count resampling**: when `ref.laneHist.size() != trackCount`, each ref lane's mass is mapped to the target lane its center falls in (`center = (r+0.5)/refN`, `targetLane = floor(center * trackCount)`). Preserves the shape of the distribution even with 12→7 lane transfers.

### AIEditorClient jsonMode overload

Narration needs plain prose, not JSON. Added a `startRequest(cfg, sys, user, bool jsonMode)` overload; the 3-arg default keeps Copilot + Audit on jsonMode=true with zero behavior change. The worker thread threads the flag through to `runChatRequest`.

### SongEditor wiring

Third pimpl alongside `m_copilot` + `m_audit`:

- `StyleState`: client, config, candidates, selected, fingerprints (ref / before / after), narration/lastError/rawResponse, undo snapshot + `undoDifficulty`, stats.
- `pollStyle()` at the top of `render()`.
- `renderStylePanel()` at the end of `renderProperties()`, below Chart Audit.
- **New member** `Engine* m_engineCached = nullptr` refreshed each frame in `render()` so the panel can reach `engine->musicSelectionEditor().sets()` without changing `renderProperties()`' signature.
- `MusicSelectionEditor` gained `const std::vector<MusicSetInfo>& sets() const` getter.

### UI

- Refresh button next to the model tag.
- `ImGui::BeginCombo` listing candidates as `<set> / <song> [Difficulty] (Nt)` — the `(Nt)` suffix shows the reference's native trackCount so track-count mismatches are visible up front.
- "Analyze reference" → `ChartLoader::load(cand.absPath)` + `computeFingerprint(ref, cand.trackCount)` → fingerprint printed as wrapped text.
- **Inline guards** (inline warnings below the combo that also disable Apply):
  - "Reference has 0 notes - pick a non-empty chart." when `refFp.noteCount == 0`.
  - "Target chart has 0 notes - place notes first (Autocharter Place All)." when `notes().empty()`.
- "Apply style" → snapshots `{notes, markers, features}` + `(int)m_currentDifficulty`; computes before; runs `applyStyleTransfer`; computes after; fires `client.startRequest(..., jsonMode=false)` with the prose system prompt "2-3 sentences describing what shifted toward the reference...". Stats line `retyped N / relaned M / skipped K` + narration + after-fingerprint rendered as they arrive.
- "Undo" → blocks with a status warning if `m_currentDifficulty` changed since Apply; otherwise restores the snapshot.

### Iteration — three fixes in the same session

1. **Newly-added songs didn't appear after Refresh.** Root cause: v1 parsed `music_selection.json` from disk, and the Add Song dialog doesn't save the file (only mutates `m_sets` in memory). Fix: take `const std::vector<MusicSetInfo>&` directly, reached via `engine->musicSelectionEditor().sets()` through the new `m_engineCached` member.
2. **Reference chart with 12 tracks couldn't be used on a 7-track target.** V1 filtered candidates by exact `trackCount` equality. Dropped that constraint in `matchMode` (kept `type` + `dimension`); the candidate label's `(Nt)` suffix exposes the difference. Added cross-track-count resampling in the apply path so the lane histogram's shape survives the transfer.
3. **Reference fingerprint showed the wrong lane count.** V1 called `computeFingerprint(ref, m_song->gameMode.trackCount)` — i.e. with the *current* song's lane count, which collapsed ref lanes 7–11 into lane 6 for a 12→7 case. `StyleCandidate` now carries the ref's native `trackCount`; Analyze passes that instead. Also added two inline disabled-reason warnings (empty ref / empty target) so the user understands why Apply is greyed out instead of silently doing nothing.

### Files + verification

- **New**: `engine/src/editor/ChartStyle.{h,cpp}`
- **Edited**:
  - `engine/src/editor/AIEditorClient.{h,cpp}` — jsonMode overload (Copilot + Audit behavior unchanged).
  - `engine/src/ui/MusicSelectionEditor.h` — `sets()` const getter.
  - `engine/src/ui/SongEditor.{h,cpp}` — include, `m_engineCached`, pimpl `m_style`, poll + render wiring, `renderStylePanel()` body.
- Build: `cmake -B build` (new `.cpp` via the editor glob) + `cmake --build build --target MusicGameEngineTest`.
- Runtime verified on the `test` project: Ab (7t, dropNotes 2D) ← Aa [Hard] (12t, dropNotes 2D). Refresh populates the candidate; Analyze shows `lanes=12` correctly; Apply resamples to 7 target lanes; narration arrives in ~10 s describing the type/lane shifts in concrete numbers. Undo restores exactly.

### Deferred / left room for the author

- Motif and phrasing analysis (n-gram patterns, repeated-sequence detection) — the fingerprint is ratios + histograms only.
- External-file reference picker — candidates are restricted to charts enumerated from the current project.
- Multi-difficulty fan-out (apply one ref to Easy/Medium/Hard simultaneously).

All items 2–5 on the AI-agent backlog (replay coaching, auto-metadata, lyric sync, voice authoring) are recorded as deferred in `project_ai_agent_backlog.md`.

## 2026-04-23 — Editor UI pass: Hub, Start Screen, Music Selection, SongEditor polish

Single long session that touched every editor page in order. The goal was to bring the three non-gameplay pages (Project Hub, Start Screen, Music Selection) up to the polish level the gameplay page (SongEditor) already had, and to move cross-cutting features to the pages where they actually belong.

### Global editor theme + Project Hub (`engine/src/ui/ImGuiLayer.cpp`, `engine/src/ui/ProjectHub.{h,cpp}`)

- **Black canvas with cyan/magenta accents.** ImGuiLayer overrides the default `StyleColorsDark()` with a hand-tuned palette after init: `WindowBg` = 0.03 RGB, `Button` = cyan (0,0.55,0.85), `ButtonActive` = magenta (0.95,0.30,0.75), `Header` family in neutral gray (0.22–0.70 RGB, 0.55–0.75 alpha) so selection hover doesn't read purple. Frame/scrollbar/separator/tab/text/plot cascades included; rounding bumped (Frame=4, Window=6, Popup=6). AndroidEngine runtime left alone — only the editor gets the new palette.
- **Hub overhaul.** `ProjectInfo` gained `lastModified` (formatted `YYYY-MM-DD HH:MM`) + `lastModifiedRaw`; `scanProjects()` walks each project folder recursively (`std::filesystem::recursive_directory_iterator` with `skip_permission_denied`) and records the newest mtime via a duration-offset conversion from `fs::file_time_type` → `system_clock` to avoid MSVC `clock_cast` availability issues. Project list sorted newest-first. Action bar: search `InputTextWithHint` (case-insensitive substring filter) + `+ Create Game` + `+ Add File`. Rows use `Selectable` with `AllowDoubleClick` — single-click selects (cyan outline + magenta left bar + blue fill overlay drawn via `ImDrawList`), double-click opens the editor. `Build APK` button moves out of every row into a single magenta-tinted button in the action bar that only appears when a project is selected (`Build APK: <name>`). New `Add Project from File` dialog: accepts a path to a folder containing `project.json` (or the `project.json` itself), validates format, copies into `Projects/` via `fs::copy(recursive | overwrite_existing)`, strips surrounding quotes, forces a rescan.

### Start Screen editor (`engine/src/ui/StartScreenEditor.{h,cpp}`, `engine/src/ui/PreviewAspect.h`)

- **Aspect-ratio controls + letterbox.** New `engine/src/engine/Engine.h`: `Engine::PreviewAspect { int w, h, presetIdx }`, accessible via `engine->previewAspect()`. New header `ui/PreviewAspect.h` exposes `previewAspect::presets(count)` (9 landscape-only presets: 16:9 / 16:10 / 4:3 / 3:2 / 18:9 / 19.5:9 / 20:9 / 21:9 / 1:1 + Custom; portrait ratios deliberately omitted), `enforceLandscape(a)` (clamps `h` to `min(w,h)` so custom input can't go portrait), `renderControls(a)` (preset combo + two `InputInt` boxes + "(landscape only)" hint), and `fitAndLetterbox(a, avail, color)` which returns the fitted sub-rect and paints the surrounding bars. Both `StartScreenEditor::renderPreview()` and `MusicSelectionEditor::renderPreview()` call `renderControls` at the top, then draw everything into the fitted rect. `PushClipRect` wraps the entire scene draw in both pages so logo text can't bleed into the letterbox bars.
- **Properties pass.** `renderProperties()`: Italic checkbox removed from Logo. Tap Text section keeps only Size slider — text content + position removed. `Default` pill added as the first line inside each section body (`Background`, `Logo`, `Tap Text`, `Transition Effect`, `Audio`). Lives inside the `if(open){}` block so the click can't be stolen by the CollapsingHeader's full-width hit area or clipped by the scrollbar. Load + Reset buttons removed from the bottom nav (per-section Default handles resets; Save + `Next: Music Selection >` + `< Back` remain). Hard caps enforced every frame: logo font ≤ 96 px, logo scale ≤ 3.0×, tap text ≤ 72 px; over-sized stale saves get clamped. Fit-to-width: if logo/tap text width would exceed `pw * 0.96`, `fontSize` is reduced proportionally via `CalcTextSizeA` rather than clipping.
- **Materials tab moved.** Removed from Start Screen (the tab bar is gone; `renderProperties()` renders directly). `StartScreenEditor::renderMaterials()` + `drawMaterialPreviewAt()` now public; SongEditor's properties pane grew a `Material Builder` CollapsingHeader that delegates to `engine->startScreenEditor().renderMaterials(engine)`. Per-chart Achievements (FC/AP image) section deleted from SongEditor — moved to Music Selection, one pair per game.
- **Material mini-previews in Assets grid.** Assets browser's MAT tiles now render an 80×80 `drawMaterialPreviewAt()` live preview instead of a static "MAT" disc. Hover tooltip gets the same preview at 260×140, plus target mode/slot. Texture tiles got a 256×256 hover preview too. Added a checker-pattern backdrop inside `drawMaterialPreviewAt()` so alpha reads correctly. Shape families inferred from `targetSlotSlug`: NoteTap (rectangle), NoteFlick (rectangle + arrow wedge), HoldBody (tall bar with caps), Arc (bezier with halo), ArcTap (diamond), Track (receding trapezoid via `AddImageQuad`), JudgmentBar (thin bar + halo), Disk (ring), ScanLine (animated sweep — now clipped via `PushClipRect` so the sweep + ghost trail can't paint outside the preview tile), plus Default fallback. Kind-specific overlays: Scroll animates diagonal stripes, Pulse modulates brightness via `exp(-phase*decay)`, Glow adds radial halo, Gradient applies vertical color blend. Tiles wrap to the next row via a shared `flowNext()` lambda that checks `GetItemRectMax().x + spacing + tileW <= GetWindowPos().x + GetWindowContentRegionMax().x` before calling `SameLine`.

### Music Selection editor (`engine/src/ui/MusicSelectionEditor.{h,cpp}`, `engine/src/engine/AudioEngine.{h,cpp}`)

- **Cover path text removed** under both Song and Set properties (thumbnail + Clear remain).
- **Assets panel now displays materials** with the same tile logic as Start Screen, via the newly-public `StartScreenEditor::drawMaterialPreviewAt()`. Added `#include "StartScreenEditor.h"` + `#include "renderer/MaterialAssetLibrary.h"`. Images/GIFs/videos/audio/materials all flow-wrap via `flowNext()`.
- **Letterbox + clip + vertical layout fix.** Same `previewAspect` integration as Start Screen; `ImGui::Dummy(avail)` at the end of `renderPreview` removed (was double-reserving height on top of the aspect-controls row, spawning a scrollbar). Center stack — cover + name + score + difficulty buttons + play button — vertically centered inside the letterboxed rect so the "empty bottom half" look is gone when the aspect is narrower than the panel.
- **Page Background + frosted overlay.** New member `m_pageBackground` persisted as top-level `background` in `music_selection.json`. Drop zone lives at the top of the Hierarchy panel. When set, `renderPreview` and `renderGamePreview` paint the background over the whole scene, then layer 5 horizontal bands: left 18% heavy frost (alpha 190), 18-px gradient heavy→light, middle light frost (alpha 55), 18-px gradient light→heavy, right 18% heavy frost. Dark vertical shadow lines (2 px, alpha 140) at `x = wheelW` and `x = pw - wheelW` mark the panel edges. Top/bottom vignette added in editor preview only. Earlier attempt used bright 1-px highlight lines with `+0.5f` / `-0.5f` sub-pixel offsets — removed because the fractional-pixel rasterization made left and right lines appear asymmetric.
- **Achievement badges (page-level).** Members `m_fcImage`, `m_apImage`, `m_showAchievementPreview` on MusicSelectionEditor; persisted as top-level `fcImage`/`apImage` keys. Hierarchy panel has two square 96×96 drop zones with aspect-fit display (`min(zoneSide/imgW, zoneSide/imgH)` scale, centered) so badges don't stretch — `getThumb` + `m_thumbCache` lookup provides `Texture.width/height`. Per-difficulty `achievementEasy / achievementMedium / achievementHard` + `scoreEasy / scoreMedium / scoreHard` added to `SongInfo` (judgement system writes these; UI read-only in the final cut — earlier iteration had an editor row with score inputs + combo, removed per user feedback). "Preview Badges in Scene" toggle button inside the Hierarchy panel — when on, `fcUnlocked`/`apUnlocked` force-set to true inside `renderSongWheel` so every card's rhombus slots light up with the uploaded images. No popup/modal.
- **Song wheel card redesign.** Layout = `[cover 25%] [text column] [rhombus pair + padding]`. Rhombus sized first (`rhombusH = min(sh*1.6, cardW*0.20)`), text column gets the leftover width. Two Arcaea-style diamond slots drawn behind `[FC] [AP]` to the right of the name+score via `AddQuadFilled` (backing) + `AddImageQuad` (badge image clipped to diamond via `uv0..uv3` at 0.5/0/1/0.5/0.5/1/0/0.5) + `AddQuad` (outline). Unlocked: full alpha + colored backing (cyan for FC, gold for AP). Locked: dimmed image + dark backing. AP achievement implies FC. Long name protected by a per-card `PushClipRect` using the card's screen-space bounding rect. Name and score overlap fixed: name at `quadCY - sh*0.30`, score at `quadCY + sh*0.12`, rhombus pair anchored to `tr.x - padding`.
- **Audio preview via AI analyzer.** `AudioEngine` gained `playFrom(double startSec)` (reads sample rate via `ma_sound_get_data_format`, seeks with `ma_sound_seek_to_pcm_frame`, starts) and `durationSeconds()` (reads `ma_sound_get_length_in_seconds`). `SongInfo` gained `previewStart` (default `-1` = auto) + `previewDuration` (default 30 s), persisted as `previewStart`/`previewDuration` keys per song. `MusicSelectionEditor::updateAudioPreview(dt)` called every frame from `render()`: resets dwell timer on selection change, stops any active clip, after 500 ms of dwell on a song with `audioFile` loads the file (caches via `m_previewPath`) and calls `ae.playFrom(previewStart)` (falls back to 25% of duration if unset), schedules stop after `previewDuration` seconds. Gated on `engine->isTestMode()` — the editor's authoring preview box stays silent; only the full-screen test-game mode and real Android build play the clip. Default selection set to set 0 / song 0 when the page loads with nothing selected. **SongEditor → Audio → Preview Clip** section: `Start (s)` + `Length (s)` sliders (10–45 s cap), `Auto-Detect` button that slides a `previewDuration` window over hardest-difficulty analyzer markers (`m_diffMarkers[Hard]` + `m_diffFeatures[Hard]`), picks the window with the highest sum of `MarkerFeature.strength` as the peak region. Falls back to 25% of duration when no analysis data exists. Range summary: `X s → Y s (song: Z s)`.

### SongEditor (`engine/src/ui/SongEditor.cpp`)

- **Material Builder CollapsingHeader** added to properties pane, directly above the existing per-slot `Materials` assignment section. Delegates to `engine->startScreenEditor().renderMaterials(engine)`.
- **Achievements section removed** — moved to Music Selection (see above).

### Build / verification

CMake glob picks up the new PreviewAspect.h inclusion from editor sources without re-running `cmake -B build`. `cmake --build build --config Debug --target MusicGameEngineTest` succeeds with only the pre-existing `C4819` UTF-8 BOM warnings. Runtime-tested end-to-end on the `test` project across 12+ engine runs today — each iteration followed a tight "build → run → user feedback → adjust" cycle.

### Files touched

- **New:** `engine/src/ui/PreviewAspect.h`
- **Edited:**
  - `engine/src/ui/ImGuiLayer.cpp` — black editor palette
  - `engine/src/ui/ProjectHub.{h,cpp}` — mtime scan, search, single Build APK, Add File, select-vs-open split
  - `engine/src/ui/StartScreenEditor.{h,cpp}` — aspect controls, letterbox clip, properties trim, Default pills, text fit-to-width, material tile preview, MAT hover tooltip, renderMaterials made public, drawMaterialPreviewAt public
  - `engine/src/ui/MusicSelectionEditor.{h,cpp}` — aspect controls, letterbox clip, page background + frosted overlay, FC/AP zone (aspect-fit, square), rhombus slots in wheel card, per-difficulty score/achievement fields, audio preview loop, default selection, assets panel material tiles, cover path hidden, flow-wrap
  - `engine/src/ui/SongEditor.{h,cpp}` — Material Builder, achievements moved out, Preview Clip block
  - `engine/src/engine/Engine.h` — `PreviewAspect` struct + accessor
  - `engine/src/engine/AudioEngine.{h,cpp}` — `playFrom(startSec)`, `durationSeconds()`

### Iteration count

The session ran through roughly 25 user-facing iterations because screenshot-driven feedback found problems layer by layer — e.g., badges went through six placements (pop-up → center overlay → cover corner → song-wheel chip → song-wheel circle → rhombus-behind-title → rhombus-right-of-title) before landing where the user wanted them, and the Default button needed three layout attempts to survive the CollapsingHeader hit area + scrollbar. The final code only keeps the last of each attempt; intermediate decisions and why they were rejected are captured here.
