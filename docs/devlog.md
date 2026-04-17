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
