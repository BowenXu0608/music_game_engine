---
name: Editor UI System
description: ProjectHub -> StartScreen -> MusicSelection -> SongEditor, all config panels, chart persistence, test game
type: project
originSessionId: d4e6dddd-1cc1-4f7b-8da6-079be9eb81c0
---
# System 7 — Editor UI ✅ COMPLETE

**Files:** `engine/src/ui/`

## Layer Architecture

```
EditorLayer: ProjectHub -> StartScreen -> MusicSelection -> SongEditor -> (TestGame process)
```

Each layer = self-contained ImGui panel. Test Game = separate process via `CreateProcessW`.

## SongEditor — DAW-Style Layout

```
+---------------+-----------------------------------+
| Left Sidebar  | Scene Preview                     |
| (scrollable)  |--- draggable splitter ------------|
|               | Chart Timeline                    |
| - Song Info   | [Toolbar + Difficulty + Notes]    |
| - Audio       |-----------------------------------|
| - Game Mode   | Arc Height Editor (3D only,120px) |
| - Cross-Sect  |-----------------------------------|
|   (3D only)   | Waveform Strip (100px)            |
| - Config      |-----------------------------------|
| - Assets      | Back | Save | Test | Play/Pause   |
+---------------+-----------------------------------+
```

### Config Panels

**Game Mode:** DropNotes/Circle/ScanLine + 2D/3D dimension + track count.

**Camera:** Eye position, look-at target, FOV (20-120 deg).

**HUD:** Score + Combo position/font/color/glow/bold per-element via `HudTextConfig`.

**Score:** Perfect/Good/Bad score values. Achievement FC/AP image pickers.

**Disk Animation (Circle mode):** Keyframed rotate/scale/move events. Per-difficulty storage (`m_diffDiskRot/Move/Scale`). Add/edit/delete UI with easing combo. DiskFX timeline strip.

**Disk Layout (Circle mode, 2026-04-12):** Four sliders in `renderGameModeConfig()` expose per-song disk defaults persisted to `GameModeConfig` (and `music_selection.json`): `diskInnerRadius` (spawn-disk radius, 0.2–3.0), `diskBaseRadius` (hit-ring radius, 1.0–6.0), `diskRingSpacing` (extra-ring gap, 0.1–1.5), `diskInitialScale` (initial scale before keyframes, 0.3–2.0). "Reset disk defaults" button restores the legacy 0.9 / 2.4 / 0.6 / 1.0 values. Each slider marks `m_laneMaskDirty = true` because the reachability predicate reads `diskBaseRadius`. `LanotaRenderer::onInit` seeds its per-instance `INNER_RADIUS / BASE_RADIUS / RING_SPACING / m_diskScale` from these fields.

**Scan Line Speed (ScanLine mode):** `ScanSpeedEvent` keyframes (0.1x-4.0x). Per-difficulty storage (`m_diffScanSpeed`). Phase table rebuilt lazily.

**Judgment Windows:** Perfect/Good/Bad ms thresholds.

### Scan Line Authoring (paginated, 2026-04-17)

Scene shows exactly one "page" at a time. A page = one sweep of the scan line (top->bottom OR bottom->top). Default duration = `240/BPM` seconds (one bar @ 4/4). Page 0 sweeps bottom->top; direction alternates each page to match runtime.

**Scene-embedded toolbar row:** `[Tap] [Flick] [Hold] [Slide] | [Select] | [Analyze Beats] [Clear Markers]`. The beat-analysis buttons call the same `AudioAnalyzer` pipeline as 2D modes and populate `m_diffMarkers` + `m_bpmChanges`; completion flips `m_scanPageTableDirty = true` so page durations re-derive from detected BPM. `Select` is the pointer tool (NoteTool::None) — click-to-select existing notes.

**Navigation strip (top of scene body):** `[<] Page N/M  BPM X  dt Y.YYs` label `[>]` Prev/Next arrows; per-page speed `InputFloat` (0.25x..4x, step 0.25); `[Place All]` button that auto-fills a Tap on every AI beat marker. `PageUp` / `PageDown` keys also navigate.

**Per-page speed:** editing the speed input creates/updates/removes a `ScanPageOverride` for the current page (removed when set back to exactly 1.0). Page table and phase table are rebuilt lazily. Speed 2.0x halves the page's duration, etc.

**AI markers on page:** dashed horizontal orange ticks (same color as 2D modes' timeline markers) drawn at each beat's page-Y. Sourced from `m_diffMarkers[currentDiff]`. Clicks snap to the nearest marker within `min(0.06s, 0.15 * page.duration)`; hold `Alt` to place without snapping.

**Tap/Flick:** any LMB click in the page body is valid (no scan-line proximity gate). Time is derived from `scanPageYToTime(pageIdx, yNorm)`.

**Hold:** LMB anywhere in the body starts the head. Mouse wheel extends the span in page units (`m_scanHoldExtraSweeps`). Navigating Prev/Next while in await-end automatically extends the span to cover the visited page. LMB on the target page commits. Preview shows the body across all spanned pages plus cross-page markers.

**Slide:** LMB starts, RMB adds nodes, Prev/Next allowed between RMBs (each node stores its `pageIndex`). On LMB release each node's absolute time is `scanPageYToTime(node.pageIndex, node.y)`; `samplePoints` hold deltas from the head. Per-page monotonicity: within one page the node must lie in the page's time-forward direction (enforced with `ImGuiMouseCursor_NotAllowed`); across pages no constraint beyond non-decreasing page index.

**Cross-page markers:** hold/slide bodies crossing the current page draw a small triangle (`▲` at the start edge, `▼` at the end edge, edge parity based on `page.goingUp`) to hint at the neighbor page.

**Cursor-follow scan line:** when the mouse hovers the page body, an amber horizontal line tracks the cursor Y and a floating `t=M:SS.sss` label shows the projected song time. Lets the author preview where a click would place a note before committing.

**Auto page turning:**
- *During playback:* the page follows `curTime` via `scanPageForTime`; the song-time scan line moves smoothly within the current page and the page snaps to the next one when the playhead crosses a boundary.
- *While idle (not playing):* moving the cursor within `10px` of the page's time-forward edge advances to the next page; moving within `10px` of the start edge returns to the previous page. Armed-once (re-armed when the cursor leaves the edge zone) and gated on actual mouse motion so a parked cursor doesn't flip repeatedly.
- Prev/Next buttons, jump-to-page, edge-flip, and PageUp/PageDown all seek `m_sceneTime` to the new page's start so the auto-sync in `renderSceneView` doesn't snap back. Edge-flip is disabled during playback (the scan-line auto-advance owns the page there).

### Arc Editing (3D DropNotes Mode, redesigned 2026-04-17)

Multi-waypoint arc editor. Only visible/active in DropNotes + ThreeD mode.

**Data model:** `ArcWaypoint` struct: `{time, x, y, easeX, easeY}`. `EditorNote::arcWaypoints` vector stores the ordered path (>=2 waypoints). Legacy 2-endpoint fields (`arcStartX/arcEndX/arcStartY/arcEndY/arcEaseX/arcEaseY`) kept for backward compat; `arcColor` (0=cyan, 1=pink), `arcIsVoid`, `arcTapParent` unchanged. `ensureArcWaypoints()` migrates legacy arcs to waypoint form.

**Workflow — click-to-place:** Select Arc tool → L-click places waypoints one at a time (each must be later in time than previous) → R-click or Enter finishes (needs >=2 waypoints) → Esc cancels. Preview lines and dots drawn during placement. Arc color picked via inline C/P buttons in toolbar.

**Panel 1 — Timeline:** Arc ribbons drawn as 32-sample polyline strips with per-waypoint handles (circles at each waypoint position). Void arcs hidden. ArcTap diamonds at parent arc positions. `evalArcEditor` supports multi-waypoint interpolation (finds segment, lerps within it).

**Panel 2 — Height Curve Editor:** Per-waypoint draggable height handles (not just start/end). Height curves drawn as polylines through all waypoints with per-segment sampling.

**Cross-Section Preview:** Removed (simplified UI).

**Properties panel:** Multi-waypoint arcs show waypoint table with per-segment easing combos (eX/eY), delete buttons for interior waypoints, flatten-to-2-endpoints button. Legacy 2-endpoint arcs show position sliders + easing combos + convert-to-waypoints button. Both show color radio, void checkbox, child ArcTap list.

**Export:** Multi-waypoint arcs decompose into N-1 connected ArcData segments in JSON. Matches real Arcaea .aff format.

**Import (auto-merge):** `loadChartFile` detects consecutive connected arc segments (same color, matching endpoint positions/times within tolerance) and merges into single multi-waypoint EditorNote. ArcTap parent indices updated during merge.

**Parent fixup:** `fixupArcTapParents(deletedIdx)` unchanged.

**Toolbar:** "Arc" (cyan) + color picker (C/P) + "ArcTap" (orange), gated on `is3D`.

**Sky Height:** Configurable via `GameModeConfig::skyHeight` slider in Game Mode Config panel (range -1 to 3, default 1.0). Saved in `music_selection.json`.

### Chart Persistence

Save -> `exportAllCharts()` writes UCF JSON per difficulty. Song open -> loads charts back via `ChartLoader`. Round-trips: notes, scan fields, disk animation, scan speed events, waypoints, sample points, arc data (startX/Y, endX/Y, easeX/Y, color, void), arctap positions, beat markers.

**Per-(mode, difficulty) chart files (2026-04-12):** Filenames are keyed on both game mode and difficulty: `assets/charts/<song>_<modeKey>_<diff>.json`, where `modeKey ∈ {drop2d, drop3d, circle, scan}`. Each (mode, difficulty) pair owns an independent chart file — switching modes never overwrites or reuses another mode's notes.

- `modeKey(gm)` helper in `SongEditor.cpp` anonymous namespace returns the key from `GameModeType` + `DropDimension`.
- `chartRelPathFor(name, gm, diff)` composes the relative path used by both export and load.
- `reloadChartsForCurrentMode()` clears in-memory `m_diffNotes` / `m_diffMarkers` / disk-FX / scan-speed / BPM state, then loads the three (easy/medium/hard) files for the current `gameMode` from disk (starting empty when a file is absent) and updates `m_song->chartEasy/Medium/Hard` accordingly.
- `loadChartFile(diff, chartRel)` is the extracted single-chart loader used by both `setSong()` and `reloadChartsForCurrentMode()` (replaces the previous inline lambda).
- Mode / Dimension buttons in `renderGameModeConfig()` hook the switch: `exportAllCharts()` saves the old mode's charts, the mode/dimension field is updated, then `reloadChartsForCurrentMode()` pulls the new mode's charts.

### Beat marker persistence (2026-04-17)

Beat markers (AI-detected by `AudioAnalyzer` + hand-placed) now round-trip through the chart JSON alongside notes. Previously only `m_diffNotes` survived a save/load; every reopen forced the author to re-run beat analysis.

- `ChartData::markers` is a `std::vector<float>` persisted per (mode, difficulty) chart file. Applies uniformly across every game mode.
- `buildChartFromNotes()` copies the current difficulty's `markers()` into `chart.markers` before emitting.
- `exportAllCharts()` emits a `"markers": [t0, t1, ...]` array in the chart JSON, and now also exports difficulties whose notes are empty but whose markers aren't (so marker-only work survives).
- `ChartLoader::loadUnified()` parses the `"markers"` array into `ChartData::markers`.
- `SongEditor::loadChartFile` hydrates `m_diffMarkers[(int)diff]` from `chart.markers` so the authoring markers appear immediately on reopen.

### Achievement Image Pickers (asset-drag only, 2026-04-12)

FC and AP achievement image pickers use a 96×96 drop-slot widget that accepts only `ASSET_PATH` drag-drop payloads from the Asset Browser. Text input and file-browse buttons were removed so images can only be sourced from project assets. Background image picker still supports drag/browse/text-input.

### Test Game

Spawns `MusicGameEngineTest.exe --test <project_path>` child process. Full flow: StartScreen -> MusicSelection -> Gameplay. Editor window unaffected.

### Other Features

- Per-difficulty notes/markers via `m_diffNotes`/`m_diffMarkers`
- Audio playback controls (Play/Pause/Stop) in nav bar
- Waveform always visible below scene/timeline
- Beat analysis via Madmom (Analyze Beats button)
- Toolbar: Analyze Beats / Clear Markers / Place All / Clear Notes
- Right-click delete (note first, marker fallback)
- BPM Map panel with tempo sections
- Lane-enable mask timeline (Circle mode) rebuilt from disk animation keyframes

### Music Selection — Auto Play toggle (2026-04-12)

`MusicSelectionEditor::renderPlayButton` draws an **AUTO PLAY: ON/OFF** toggle button below the START button (orange when on, grey when off). State persisted in `m_autoPlay` (ephemeral, not saved) and passed through `Engine::launchGameplay(song, diff, projectPath, autoPlay)` → `Engine::m_autoPlay`. Engine::update then drives `HitDetector::autoPlayTick` each frame (see sys5).

### Lane-mask reachability fix (Circle mode, 2026-04-12)

`laneMaskForTransform` in `SongEditor.cpp` used to hardcode `kBaseRadius=2.4` and check each lane against `|lx|<2.85 && |ly|<2.185` — but a ring of radius 2.4 has top/bottom points at `y=±2.4`, so those lanes were **always** masked out. Combined with the `tc <= 32` gate in the renderer, this meant bumping track count above 32 was the only way to unlock the hidden lanes. Two fixes:

1. The helper now takes an `outerR` parameter sourced from `gm.diskBaseRadius`, and its half-extents are `max(kFovHalfX, r) + 0.15` / `max(kFovHalfY, r) + 0.15` — a default ring of radius 2.4 now fits with a small movement margin. It also explicitly returns `0xFFFFFFFFu` when `trackCount > 32` so high-count charts are never silently gated.
2. Changing the **Tracks** slider or any of the four **Disk Layout** sliders now sets `m_laneMaskDirty = true` so the cached mask timeline is rebuilt — previously the mask from the old lane count was held forever, leaving new lanes gated even after the underlying bounds were correct.

### Lane-mask gating actually gates now (Circle mode, 2026-04-14)

Follow-up: the `max(kFovHalfX, r) + 0.15` bound from the 2026-04-12 fix was self-defeating — it scaled the "playable rect" up in lockstep with the disk radius, so enlarging the disk never produced any unreachable lanes. The camera's visible rect at z=0 is fixed at ~±3.0 × ±2.31 world units (FOV_Y=60°, eye z=4), so `laneMaskForTransform` now uses a **fixed** bound of `kFovHalfX + 0.15` / `kFovHalfY + 0.15`. Default ring (`baseR=2.4`, scale 1.0) still fits thanks to the 0.15 margin, but any disk that projects beyond the viewport now correctly gates the off-screen lanes.

Three related fixes shipped with it:

- `rebuildLaneMaskTimeline` now multiplies the sampled keyframe scale by `gm.diskInitialScale`, so the `Initial scale` slider is an actual base multiplier in the editor's reachability sampling (previously the base was hardcoded to 1.0 in `sampleDiskScale`).
- `LanotaRenderer` was doing the same thing at runtime — `onInit` seeded `m_diskScale` from `diskInitialScale`, but `onUpdate` overwrote it every frame with `getDiskScale()` (base 1.0). Now stores `m_diskInitialScale` and applies it as `m_diskScale = m_diskInitialScale * getDiskScale(...)` each frame, so the slider actually enlarges the disk in gameplay.
- Raised the scale slider caps: `Target scale` keyframe slider 3.0 → 5.0, `Initial scale` slider 2.0 → 5.0. Previously you couldn't push the disk far enough past the viewport to see meaningful gating.

### Player Settings page (2026-04-18)

Added `EditorLayer::Settings` as the fourth layer alongside `ProjectHub → StartScreen → MusicSelection → SongEditor`. It hosts the player-facing settings screen shipped in the final Android game; from the engine user's perspective it's a read-and-tweak preview, live-bound to `Engine::playerSettings()`.

**Layer navigation:** `MusicSelectionEditor`'s nav bar gained a **Next: Settings >** button at the bottom-right (mirrors the "Next: Music Selection >" pattern from `StartScreenEditor`). `SettingsEditor::render` just wraps `SettingsPageUI::render` with a host that hands it `engine->audio()` and an `onBack` lambda that calls `engine->applyPlayerSettings()` then `switchLayer(MusicSelection)`.

**Settings exposed (8 only):**

| Setting | Type | Applied via |
|---|---|---|
| Music volume | slider 0–1 | `AudioEngine::setMusicVolume` → `ma_sound_set_volume` |
| Hit-sound volume | slider 0–1 | `AudioEngine::setSfxVolume` (used by `playClickSfx` amplitude) |
| Hit-sound enabled | checkbox | `AudioEngine::setHitSoundEnabled` (early-exit in `playClickSfx`) |
| Audio offset (ms) | slider ±200 + **tap-to-calibrate** wizard | `HitDetector::setAudioOffset` applied across every timing check |
| Note speed | slider 1–10 (5 = 1.0×) | `GameModeRenderer::setNoteSpeedMultiplier` — Bandori/Arcaea multiply `SCROLL_SPEED`, Lanota divides `APPROACH_SECS`. Cytus (Scan Line) + Phigros ignore it |
| Background dim | slider 0–1 | Semi-transparent black overlay via `ImGui::GetBackgroundDrawList()` during gameplay |
| FPS counter | checkbox | Top-left text using `ImGui::GetIO().Framerate` during gameplay |
| Language | combo (en/zh/ja/ko) | **Store-only** — persisted, not wired to a localization system yet |

**Shared UI:** `SettingsPageUI::render(origin, size, PlayerSettings&, Host, readOnly)` in `engine/src/ui/SettingsPageUI.{h,cpp}` — one source for three call sites: `SettingsEditor`, `MusicSelectionEditor`'s Test Game gear modal, and `AndroidEngine::renderSettings` in the shipped game. Layout is a full-screen opaque scrim window + centered 640 px card (via `BeginChild` with `ChildBg`). `ImGui::BringWindowToDisplayFront(window)` is called each frame so the scrim stays above any caller's windows without touching ImGui's active-item state (which `SetNextWindowFocus` would reset and break slider drags).

**Test Game gear button:** In-game music-select screen (the wheel-based `MusicSelectionEditor::renderGamePreview`) got a floating **⚙ Settings** button top-right. It's rendered as its own top-level ImGui window after `##test_musicsel` closes, and the test window uses `ImGuiWindowFlags_NoBringToFrontOnFocus` so clicking a song card doesn't push the gear behind. Clicking the gear sets `m_showSettings = true`, which opens the same `SettingsPageUI` modal bound to `engine->playerSettings()`. Settings are applied every frame while the modal is open so slider drags take effect live — volume is audibly changing as the slider moves, note-speed is visible on the next PLAY.

**Persistence:** `PlayerSettings` struct + JSON I/O lives in `engine/src/game/PlayerSettings.{h,cpp}`, using the same hand-rolled `jsonString / jsonDouble / jsonBool` scanner `AndroidEngine.cpp` already uses for `music_selection.json`. On Android the file is `<internal_storage>/player_settings.json`, loaded during `AndroidEngine::init` and saved on `onBack` from the Settings screen.

### Material system (2026-04-18)

Per-slot visual overrides in charts. A **slot** is a named visual role within a mode (e.g. Bandori "Tap Note", Cytus "Scan Line", Lanota "Disk Surface"); each slot has a default `MaterialKind` (Unlit / Glow / Scroll / Pulse / Gradient / Custom) and the chart can override kind + tint + params + texture per slot.

**SongEditor → Materials panel** (Config sidebar, below BPM Map). Slots listed via `getMaterialSlotsForMode(currentMode)` with group headers (e.g. all Hold Note slots grouped under one header). Per-slot controls: Kind dropdown (asset-picker in Phase 4), tint, 4 param sliders with kind-specific labels, optional texture picker that accepts `ASSET_PATH` drag-drop. "Reset to default" per slot.

**Phase 4 — project-level assets + custom shaders.** Materials promoted to reusable assets (one `.mat` JSON per material under `<project>/assets/materials/`) with automatic migration on `Engine::openProject()`: inline entries matching the slot default become references to shared `default_<mode>_<slug>.mat`; differing entries spill to per-chart override `<chartStem>__<slug>.mat`.

- **StartScreen → Properties → Materials tab** provides full CRUD: add/delete/rename, kind dropdown, tint, params, texture picker, **Target mode** + **Target slot** combos (filter the SongEditor picker to compatible assets — empty target = universal).
- **Custom kind** adds a **Template** button (emits boilerplate `.frag` with the shared push-constant block) and a **Compile** button (invokes `ShaderCompiler`, shows glslc errors inline). Compiled `.spv` cached by source mtime. `.hlsl` rejected with a clear error.
- **Assets panel** gained purple **MAT** tiles; clicking one opens the Materials tab pre-scrolled to that asset.
- **SongEditor slot picker** is now an asset-picker dropdown driven by `MaterialAssetLibrary::namesCompatibleWith(mode, slug)`.

### Autocharter — feature-driven Place All (2026-04-18)

Place All no longer drops a Tap on every beat marker. `AudioAnalyzer` now produces per-marker **strength / sustain / centroid** features (via the extended `tools/analyze_audio.py`), and `SongEditor` uses them to pick type + lane:

- **Type** — Hold if `sustain ≥ holdMin`; else Flick if `strength ≥ flickThreshold` (default: 88th percentile of the song); else Click.
- **Lane** (non-ScanLine modes) — centroid → lane index, with an **anti-jack nudge** that shifts by ±1 if the candidate lane was used within `antiJack` notes back. Per-lane **cooldown** (`laneCooldownMs`) drops the marker if every lane is still busy.
- **ScanLine** — centroid → X position + a global time-gap validator (`scanTimeGapMs`). Lane-less.

New **AI...** gear popup next to Place All exposes: `flickPct`, `holdMin`, `antiJack`, `laneCooldownMs`, `scanTimeGapMs`. Deferred per user: difficulty differentiation by type, Arc generation, sky-note inference — those stay hand-authored.

### Editor Copilot — natural-language chart edits (2026-04-18)

New **Copilot** CollapsingHeader in SongEditor's Properties panel, below BPM Map. Small local-first AI assistant for focused chart edits.

**Endpoint:** defaults to Ollama at `http://localhost:11434/v1` with model `qwen2.5:3b`. OpenAI-compatible `/chat/completions`. Config persists to `%APPDATA%/MusicGameEngineTest/ai_editor_config.json`. `https://` rejected with a clear error (OpenSSL deferred).

**Op vocabulary (6 ops):** `delete_range` / `insert` / `mirror_lanes` / `shift_lanes` / `shift_time` / `convert_type`. No arc / arctap / slide-path / hold-waypoint / disk-keyframe / material ops. Output is a strict JSON envelope `{ explanation, ops: [...] }` — the request sets `response_format: json_object` so Ollama's JSON mode keeps small models honest.

**Undo:** single-level. Apply snapshots `{notes, markers, features}` for the current difficulty; Undo restores.

**UI:** prompt `InputTextMultiline`, Apply + Undo buttons, config gear, last explanation + last-ops preview. Status via the shared `m_statusMsg`/`m_statusTimer`. Worker thread + `pollCopilot()` mirrors the `AudioAnalyzer` pattern; any worker-thread exception converts to a readable error string.

**Files:** `engine/src/editor/{AIEditorConfig, AIEditorClient, ChartEditOps, ChartSnapshot}.{h,cpp}` + `third_party/httplib/` + `third_party/nlohmann/`. SongEditor uses a pimpl `m_copilot` with forward-declared ctor/dtor to keep the header circular-free.

### AI Shader Generator (Materials tab, 2026-04-19)

Natural-language → compiled `.spv` custom-kind shader, in the Materials tab's `kind == Custom` section. Sits next to the existing Template/Compile buttons so hand-editing and AI generation use the same `.frag` path and asset record.

**Flow:** prompt textarea → Generate → worker POSTs to `runChatRequest` → extracts `{"shader": "<glsl>"}` → writes to `<project>/assets/shaders/<material_name>.frag` → calls `compileFragmentToSpv` → on glslc failure, retries (default 3 attempts) with the previous shader + stderr fed back.

**Slot-aware prompt context:** at dispatch time the UI prepends `"This shader is for the '<slot>' slot in the '<mode>' game mode. Tailor the visual to that role..."` when the asset has `targetMode` + `targetSlotSlug` set. Works alongside the asset-level filter (which prevents mis-assignment) to nudge the model toward role-appropriate shaders.

**System prompt = few-shot template.** After several iterations on `qwen2.5:3b`, the shader-gen system prompt embeds a **complete working template shader** and instructs "keep every layout() binding above main() exactly as shown; modify ONLY the body of main()". Small models edit existing code far more reliably than they write from constraints alone. See the 2026-04-19 devlog entry for the evolution (rule-only → forbid-monotonic-decay → color-literal requirement → explicit `ubo.time` scoping → template).

**Files:** `engine/src/editor/{AIChatRequest, ShaderGenClient}.{h,cpp}`. `AIEditorClient` was slimmed to delegate to the shared `runChatRequest` (Copilot behavior unchanged). StartScreenEditor gained a pimpl `m_shaderGen` (same forward-declared-unique_ptr trick as SongEditor's `m_copilot`) to keep `<thread>`/`<atomic>` out of the header.

**Reused infrastructure:** config (`ai_editor_config.json`), endpoint parsing, worker-thread + pollCompletion pattern, and JSON-mode request envelope are all shared with the Copilot via `AIChatRequest`.

**Bug fix as side effect:** on Windows, `_popen` wraps commands in `cmd.exe /c`, which strips the outer `"..."` pair when the command starts with `"` but doesn't end with `"`. This mangled the glslc path into `glslc.exe"` on every invocation. Fixed in `ShaderCompiler.cpp` by wrapping the whole `cmdStr` in an extra outer quote pair on Windows. The hand-written Compile button was latently broken by the same bug and is now fixed.

### Chart Audit (SongEditor Properties, 2026-04-19; overhauled 2026-04-26)

Read-only AI quality review. Point at the current chart, get a structured report with density spikes, jacks, crossovers, dead zones, off-beat notes, difficulty concerns. Complements the Autocharter (generation) and Copilot (edit) — Audit never mutates the chart.

**Hybrid design — local scan + LLM narrate.** Small local models (qwen2.5:3b) hallucinate timestamps when asked to reason over raw note lists, and the full chart often blows the context window. Fix: pre-digest facts in C++; the model only prioritizes + writes prose with the timestamps we already gave it.

- `computeAuditMetrics(notes, duration, markers={})` → concrete violations:
  - **Density hotspots** are scanned twice and unioned: a **sustained** pass (4 s × ≥16 events ≈ 4 NPS sustained) and a **burst** pass (1 s × ≥8 events ≈ 8 NPS spike). Bursts overlapping a sustained run are absorbed into the wider range; non-overlapping bursts append. List sorted by start time. The single 4 s × ≥24 (≈6 NPS) pass it replaced missed brief 10–12 NPS spikes that were clearly authoring problems.
  - **Jacks**: ≥ 3 consecutive same-lane notes, each within 500 ms of the prior.
  - **Crossovers**: adjacent notes with `|dLane| ≥ 3` within 150 ms.
  - **Dead zones**: gaps > 8 s between onsets.
  - **Peak NPS** (2 s sliding window, onset-driven) + **Avg NPS** + type counts.
  - **Marker-only stats** (filled only when `notes.empty()` and `markers` non-empty): `markerCount`, `avgMarkerRate`, `peakMarkerRate2s`, `markerDensityHotspots` (4 s × ≥16), `markerDeadZones`. Used for the "Analyze Beats just finished, no chart yet" workflow so the audit can comment on whether the song's musical density is playable on this difficulty before any notes are placed. As soon as `notes` is non-empty these are zeroed/cleared so every UI / prompt site that gates on `markerCount > 0` auto-hides — by user contract, "evaluation for marks" is hidden once authoring begins.
  - **`unalignedNotes`**: notes whose nearest analyzer marker is > 120 ms away. *Note*-quality observation, not a marker evaluation, so this stays visible whenever it has content. Surfaces as `Off-beat note @t=… lane=… (Δms off)` in the sidebar.
- `describeMetricsForPrompt` caps the text block (12 density / 12 jacks / 12 crossovers / 6 dead zones / 12 unaligned-notes / 12 marker hotspots / 6 marker dead zones) to keep the user message small.
- `parseAuditReport` extracts the first `{..}` envelope (same pattern as `ChartEditOps`) → `{summary, issues:[{severity, time, end_time, category, message}]}`. Categories: `density|jack|crossover|pacing|difficulty|alignment|other`. (Earlier `coverage` category for orphan-marker runs was removed 2026-04-26 — orphan markers were a notes-vs-markers comparison that has no use case under "evaluate markers without notes".)

**UI — right-sidebar Audit tab (since 2026-04-26).** Used to be a floating popup; now hosted as a `BeginTabItem("Audit")` in the right sidebar's `TabBar` alongside `BeginTabItem("Copilot")`. Toolbar Audit button no longer opens a popup — it sets `m_rightSidebarTab = Audit` + `m_rightSidebarTabPending = true` + `m_copilotBarOpen = true`. The tab bar consumes the pending flag once via `ImGuiTabItemFlags_SetSelected` and clears it. **`SetSelected` must be one-shot**, not asserted every frame, otherwise it fights user clicks on the other tab and both tab bodies execute in the same frame (the "two pages overlapping" symptom).

`renderAuditSidebarTab()` renders the metrics summary at the top + a `BeginChild` issue list that fills the rest of the tab (`ImVec2(-1, -1)`). Each issue is a `SmallButton` with the same hover-to-highlight + click-to-pin contract as the Preview Clip overlay (see "Sidebar-driven hover/click-pin overlays" below). Clicking also seeks `m_sceneTime` to the issue start and scrolls the timeline to `max(0, time - 2s)`. The AI-generated issues panel (`renderAuditPanel`) uses the same hover/pin pipeline, so hovering the wrapped message text also paints the band.

**Files:** `engine/src/editor/ChartAudit.{h,cpp}`. SongEditor gained pimpl `m_audit` (`AuditState` in .cpp) alongside `m_copilot`, with the same forward-declared-unique_ptr pattern + `pollAudit()` + `renderAuditPanel()` (LLM-driven) + `renderAuditSidebarTab()` (local-metrics-only) wiring. Duration comes from `m_waveform.durationSeconds` (lazy-loaded) with fallback to last-note time and to last-marker time when notes are empty. Shares `ai_editor_config.json` via `aiConfigPath()`.

### Sidebar-driven hover/click-pin overlays (Preview Clip + Chart Audit, 2026-04-26)

Two waveform overlays share the same UX contract: a translucent colored band with edge lines that paints across `[timeStart, timeEnd]` on the bottom waveform strip, gated by a sidebar interaction.

**Trigger contract:**
- Default: nothing painted.
- Hover the source UI element → overlay appears.
- Move cursor off → overlay disappears.
- Click on the source element → overlay pins (cursor can leave; band stays).
- Click anywhere outside the source → pin cleared, band vanishes.

**Preview Clip** — source is the sidebar "Preview Clip" group (header + Start slider + Auto button + Length slider, wrapped in `BeginGroup`/`EndGroup`). State: `m_showPreviewLabel`, `m_pinPreviewLabel`. After `EndGroup`, `IsItemHovered(AllowWhenBlockedByActiveItem | AllowWhenBlockedByPopup)` reads `hoveringGroup` (the active-item flag keeps it true while a slider is being dragged); on `IsMouseClicked(Left)` press edge, `m_pinPreviewLabel = hoveringGroup`; `m_showPreviewLabel = hoveringGroup || m_pinPreviewLabel`. Waveform painter is a pure consumer — fill + edges + "Preview Clip" label all gated on the same flag.

**Chart Audit** — source is any audit issue button (in the sidebar Audit tab or the AI issues panel). State: `m_auditHover`, `m_auditPin` (each an `AuditHighlight` struct: `active, timeStart, timeEnd, fillColor, edgeColor`). `m_auditHover.active` resets to `false` at the top of `SongEditor::render()`; each issue button asserts it via `if (ImGui::IsItemHovered()) m_auditHover = h;`; click sets `m_auditPin = h`. End-of-frame check `if (IsMouseClicked(Left) && !m_auditHover.active) m_auditPin.active = false;` handles click-outside dismiss. Waveform painter consumes `m_auditHover` if active, else `m_auditPin`, else nothing.

**Color palette** in `auditCategoryColor(cat, fill, edge)` (top of `SongEditor.cpp`): `density`/`marker_density` red-orange, `jack` orange, `crossover` cyan, `pacing`/`dead_zone` gray, `difficulty` amber, `alignment` yellow, `other` neutral. Translucent fill (alpha 60–70) + brighter edge (alpha 200–230). Stable mapping so users learn it by sight.

The audit band paints **under** the preview-clip band so they coexist legibly when both happen to be active.

### Style Transfer (SongEditor Properties, 2026-04-19)

Reference-driven rebalance of note types + lane distribution, with LLM narration of the before/after delta. Preserves note *times* — it redistributes type and lane, not timing.

**Pure-C++ fingerprint + apply, LLM only for prose.** Same design principle as Chart Audit.

- `StyleFingerprint` = `{noteCount, trackCount, durationSec, tap/hold/flick %, avg/peak NPS, laneHist (normalized), meanDLane, sameLaneRepeatRate}`. Tap/Hold/Flick only; Slide/Arc/ArcTap/Drag/Ring ignored in ratios + lane histogram.
- `computeFingerprint(ChartData, trackCount)` walks `NoteEvent` variants via `std::visit`, extracts `laneX` from `Tap/Hold/FlickData` only.
- `enumerateStyleCandidates(projectPath, sets, currentMode, currentSongName, currentDifficulty)` — scans the **in-memory** `std::vector<MusicSetInfo>&` from `MusicSelectionEditor::sets()`, not the on-disk JSON, so newly-added songs appear after Refresh without requiring a prior save. Filters `type` + `dimension` (dropNotes only); trackCount mismatches are allowed — the candidate label's `(Nt)` suffix exposes the difference.
- `applyStyleTransfer`:
  - **Type rebalance**: demote surplus Holds (lowest sustain → Tap) and Flicks (lowest strength → Tap) first, then promote Taps → Hold (highest sustain) and Taps → Flick (highest strength) to hit target counts derived from ref ratios × eligible total.
  - **Lane rebalance**: iterate by time; when `curHist[lane] - targetPerLane[lane] ≥ TOL=2`, pick preferred lane from marker centroid, scan outward if under-filled lane not reached; anti-jack skips `prevLane`. Skips Slide/Arc/ArcTap and cross-lane Holds (single-lane-path-only).
  - **Cross-track-count resampling**: when `ref.laneHist.size() != trackCount`, each ref lane's mass is mapped to the target lane its center falls in (`center = (r+0.5)/refN`, `targetLane = floor(center * trackCount)`). Preserves the distribution's shape for 12→7 transfers.

**LLM narration** — plain prose, not JSON. `AIEditorClient::startRequest` gained a `jsonMode` overload (the 3-arg default kept Copilot + Audit unchanged). System prompt: "2–3 sentences describing what shifted toward the reference...". User message = three `describeFingerprint` blocks (ref / before / after).

**UI** — Collapsing header "Style Transfer" below Chart Audit:
- Refresh + `ImGui::BeginCombo` with `<set> / <song> [Difficulty] (Nt)` labels.
- "Analyze reference" → `ChartLoader::load` + `computeFingerprint(ref, cand.trackCount)` (uses the ref's native lane count, not the target's — a 12-lane ref shows `lanes=12`).
- **Inline disabled-reason hints** below the combo: "Reference has 0 notes" or "Target chart has 0 notes - place notes first (Autocharter Place All)" — both disable Apply.
- "Apply style" → snapshots `{notes, markers, features}` + `m_currentDifficulty`; runs the rebalancer; fires narration request with `jsonMode=false`. Stats line `retyped N / relaned M / skipped K` + narration + after-fingerprint render as they arrive.
- "Undo" — restores the snapshot; refuses with a status warning if the difficulty changed since Apply.

**Files:** `engine/src/editor/ChartStyle.{h,cpp}`. SongEditor gained pimpl `m_style` (`StyleState` in .cpp) as the third AI panel alongside `m_copilot` + `m_audit`; also a new `Engine* m_engineCached` member refreshed each frame in `render()` so the panel can reach `engine->musicSelectionEditor().sets()` without changing `renderProperties()`' signature. `MusicSelectionEditor` gained `const std::vector<MusicSetInfo>& sets() const`.

**Scope** — deliberately minimal: fingerprint is ratios + histograms (no motif / phrasing analysis); candidates are project-scoped (no external-file picker); one target difficulty at a time (no multi-difficulty fan-out). Backlog items 2–5 on the AI-agent list (replay coaching, auto-metadata, lyric sync, voice authoring) are deferred per user direction.

---

## Editor UI polish pass (2026-04-23)

Cross-page work that brings the three non-gameplay layers up to the same quality as SongEditor and reshuffles responsibilities between pages.

### Global palette (`ImGuiLayer.cpp`)

`ImGuiLayer::init` overrides `StyleColorsDark()` with a black-canvas palette: `WindowBg` ≈ 0.03 RGB, `FrameBg` 0.10, cyan primary button (0,0.55,0.85), magenta active (0.95,0.30,0.75), neutral-gray `Header` family (0.22–0.70 RGB, 0.55–0.75 α) so selection hover doesn't read purple, purple-free scrollbar/separator/tab/resize-grip cascade. Rounding bumped (Frame=4, Window=6, Popup=6). Text 0.96 RGB; disabled 0.54. `AndroidEngine.cpp` keeps the stock dark theme — only editor windows see the new palette.

### Project Hub (`ProjectHub.{h,cpp}`)

- `ProjectInfo` gained `lastModified` (formatted `YYYY-MM-DD HH:MM`) + `lastModifiedRaw`. `scanProjects()` walks each project via `fs::recursive_directory_iterator` with `skip_permission_denied`, converts `fs::file_time_type` → `system_clock` via the duration-offset trick (avoids MSVC `clock_cast` availability issues), sorts newest-first.
- Action bar: `InputTextWithHint` search (case-insensitive substring) + `+ Create Game` + `+ Add File`. Rows use `Selectable` with `AllowDoubleClick` — single-click selects (cyan outline + magenta left bar + tinted fill via `ImDrawList`), double-click opens the editor. `Build APK: <name>` magenta button appears in the action bar only when a project is selected; per-row APK buttons removed.
- `importProject(srcPath)` accepts either a folder containing `project.json` or the `project.json` file itself, strips surrounding quotes, validates format, copies into `Projects/` via `fs::copy(recursive | overwrite_existing)`, forces a rescan on success.

### Shared editor preview aspect (`ui/PreviewAspect.h` + `Engine::PreviewAspect`)

`Engine::PreviewAspect { int w, h, presetIdx }` added to `Engine.h` with `previewAspect()` accessor so Start Screen and Music Selection share one ratio state. Header exposes:

- `presets(count)` — 9 landscape-only entries (16:9 1920×1080 / 16:10 / 4:3 / 3:2 / 18:9 / 19.5:9 / 20:9 / 21:9 / 1:1) + `Custom`. Portrait ratios deliberately omitted (game is landscape-only).
- `enforceLandscape(a)` — clamps after every edit: `h = min(w,h)`, also applied at read-time in `fitAndLetterbox` so stale portrait state from older saves can't leak into the rendered rect.
- `renderControls(a)` — preset `Combo` + two `InputInt` boxes + `(landscape only)` hint. Editing an input snaps `presetIdx` to `Custom`.
- `fitAndLetterbox(a, avail, color)` returns `{origin, size}` of the fitted sub-rect and paints the four letterbox bars around it.

Both `StartScreenEditor::renderPreview()` and `MusicSelectionEditor::renderPreview()` call `renderControls` at the top then draw the scene inside `fitAndLetterbox`. `PushClipRect(origin, origin+size)` wraps the scene draw so logo text, wheels, cover art can't bleed into the letterbox bars.

### Start Screen Editor properties

- Italic checkbox removed from Logo. Tap Text section keeps only the Size slider (Text content + Position removed).
- **Per-section `Default` pills** render as the first line inside each `if(open){}` body of every CollapsingHeader (Background / Logo / Tap Text / Transition Effect / Audio). Placing them inside the body avoids both scrollbar clipping and the CollapsingHeader's full-width hit area stealing the click. Load + Reset removed from the bottom nav — per-section Default handles resets.
- **Hard caps enforced every frame** inside `renderProperties()`: `m_logoFontSize ≤ 96`, `m_logoScale ≤ 3.0`, `m_tapTextSize ≤ 72`. Over-sized values from stale saves get clamped on the next frame.
- **Text fit-to-width**: if `CalcTextSizeA(fontSize).x > pw * 0.96`, `fontSize` is scaled by `pw*0.96 / textSize.x` before draw. Applied to both logo and tap text in `renderPreview` and `renderGamePreview` so nothing is clipped.

### Materials relocated — Start Screen → Song Editor

`StartScreenEditor::renderMaterials(Engine*)` and `drawMaterialPreviewAt(MaterialAsset, p0, size)` are now **public**. Start Screen's tab bar is gone — properties pane renders directly. SongEditor's properties pane grew a `Material Builder` CollapsingHeader that calls `engine->startScreenEditor().renderMaterials(engine)`. Per-chart FC/AP Achievements section deleted from SongEditor (moved to Music Selection, one pair per game).

### Assets grid material previews

MAT tiles render live `drawMaterialPreviewAt()` previews instead of the old static "MAT" disc. `drawMaterialPreviewAt()` chooses a shape family from `MaterialAsset::targetSlotSlug` (substring match): NoteTap (rectangle), NoteFlick (rectangle + triangle arrow), HoldBody (tall rect with bright caps), Arc (`AddBezierCubic` with halo), ArcTap (diamond via `AddQuadFilled`), Track (`AddImageQuad` trapezoid), JudgmentBar (thin rect + halo bands), Disk (ring), ScanLine (animated sweep clipped to tile via `PushClipRect`), plus Default. Kind overlays applied on top: Scroll animates diagonal stripes, Pulse modulates brightness via `1 + (peak-1)*exp(-phase*decay)`, Glow adds radial halo rings, Gradient applies vertical color blend. Checker backdrop so alpha is readable. Tiles hover-tooltip at 260×140 with target mode/slot line. Texture tiles hover at 256×256. All tiles flow-wrap via shared `flowNext()` lambda (`GetItemRectMax().x + spacing + tileW ≤ GetWindowPos().x + GetWindowContentRegionMax().x` → `SameLine`, else drop to next row).

### Music Selection Editor

- Cover-path text hidden (both Song and Set panels — thumbnail + Clear remain).
- Assets panel displays materials too, using the public `StartScreenEditor::drawMaterialPreviewAt()`.
- Vertical-layout fix: `ImGui::Dummy(avail)` removed from end of `renderPreview` (was double-reserving height on top of the aspect-controls row, spawning a scrollbar). Center stack (cover + difficulty + play) vertically centered inside `ph`.
- **Page Background + frosted overlay**: `m_pageBackground` persisted as top-level `background` in `music_selection.json`; drop zone in Hierarchy panel. When set, `renderPreview` and `renderGamePreview` paint the background over the whole scene, then layer 5 horizontal bands — left 18% heavy frost (α 190), 18 px gradient heavy→light, middle light frost (α 55), 18 px gradient light→heavy, right 18% heavy frost. 2 px dark vertical shadow lines at each boundary give the frost panels visible depth. (Earlier attempt used bright 1-px highlight lines with `+0.5f` / `-0.5f` sub-pixel offsets — removed because the fractional rasterization made left and right look asymmetric.)
- **Achievement badges (page-level)**: `m_fcImage` / `m_apImage` persisted as top-level `fcImage` / `apImage`. Hierarchy panel shows two 96×96 square drop zones with **aspect-fit display** via `getThumb` → `m_thumbCache` → `Texture.width/height` → `scale = min(zoneSide/imgW, zoneSide/imgH)` centered. Toggle button "Preview Badges in Scene" lives inside the Hierarchy panel.
- Per-difficulty stats added to `SongInfo`: `scoreEasy / scoreMedium / scoreHard` + `achievementEasy / achievementMedium / achievementHard`. Judgement system writes these; UI is read-only (per-difficulty score/achievement editor was added + removed per user feedback — belongs to the runtime, not the author).
- **Song wheel card redesign**: layout = `[cover 25%] [text column] [rhombus pair + padding]`. Rhombus sized first (`rhombusH = min(sh*1.6, cardW*0.20)`, rhombusW = rhombusH, 25% overlap), text column takes leftover width. Two Arcaea-style diamonds to the right of name+score drawn via `AddQuadFilled` (backing: cyan tint for FC unlocked, gold for AP, dark-gray when locked) + `AddImageQuad` (badge image clipped to the diamond's 4 vertices using uv `{0.5,0} {1,0.5} {0.5,1} {0,0.5}`; full alpha when unlocked, 25% when locked) + `AddQuad` outline (white unlocked, gray locked). AP implies FC. Per-card `PushClipRect` using the card's screen bounding rect so long names don't spill. Name pinned to `quadCY - sh*0.30`, score to `quadCY + sh*0.15` — overlap fixed. Preview toggle inside Hierarchy panel force-sets `fcUnlocked = apUnlocked = true` in the wheel so every card's slots light up.

### Audio preview (AI-picked)

- `AudioEngine` gained `playFrom(startSec)` — reads sample rate via `ma_sound_get_data_format`, computes frame = `startSec * sampleRate`, calls `ma_sound_seek_to_pcm_frame` + `ma_sound_start`. Also `durationSeconds()` via `ma_sound_get_length_in_seconds`.
- `SongInfo` gained `previewStart` (default `-1` = auto) + `previewDuration` (default 30 s), persisted as `previewStart` / `previewDuration` keys.
- `MusicSelectionEditor::updateAudioPreview(dt)` called every frame from `render()`: resets dwell on selection change (stops current clip), after 500 ms of dwell loads the song's audio (caches path via `m_previewPath` to avoid re-decoding), calls `ae.playFrom(previewStart)` (falls back to 25% of duration if unset), schedules stop after `previewDuration` seconds. Gated on `engine->isTestMode()` — the editor's authoring preview box stays silent; only full-screen test-game mode and real Android play the clip.
- Default selection set to set 0 / song 0 when the page loads with nothing selected.
- **SongEditor → Audio → Preview Clip section**: `Start (s)` slider (0..duration-previewDuration) + `Length (s)` slider (10–45 s cap) + `Auto-Detect` button. Auto-detect slides a `previewDuration` window over `m_diffMarkers[Hard]` + `m_diffFeatures[Hard]`, picks the window with the highest sum of `MarkerFeature.strength` as the peak-energy region. Falls back to 25% duration when no analysis data exists. Range summary: `X s → Y s (song: Z s)`.

### Marker thinning — `Thin` / `Undo` toolbar buttons (2026-04-26)

Closes the "AI density audit can mutate markers" loop. The Chart Audit's marker-side density block (4 s × ≥16 events) already flagged dense passages of analyzer output; thinning lets the user act on those flags directly.

- **Toolbar layout (`SongEditor.cpp` `renderNoteToolbar`)** — `Marker · Click · Hold · Flick · Analyze · Clr Mrk · Thin · Undo · Place · AI · Clr Note · Audit`. `Thin` is disabled when `markers().empty()`; `Undo` is disabled unless a snapshot exists *for the current difficulty*. `Clr Mrk` also wipes the snapshot so a stale one can't restore over a fresh Analyze Beats.
- **Algorithm (`thinMarkersInDensityHotspots`)** — computes `computeAuditMetrics({}, dur, mk)` (notes intentionally empty so the marker-side density block fires even when the chart already has authored notes — `ChartAudit.cpp` suppresses marker hotspots once notes exist). For each `markerDensityHotspots` range, sorts in-range markers by `MarkerFeature.strength` ascending (no-feature → 0, preferred for removal) and drops the weakest until `count <= floor(0.9 × m_autoMarkerThinNps × width)`. The 0.9 margin matters: the audit reports *peak window count*, not range total, so aiming exactly for the threshold lets surviving markers re-cluster into a tighter 4 s window and re-trip. **Iterates up to 8 passes**, re-auditing each time, stopping when hotspots empty or no marker dropped that pass. Status reports `Thinned N markers in K pass(es) (target X/s). Remaining hotspots: M.`
- **Single-shot undo** — snapshot (`m_thinUndoMarkers`, `m_thinUndoFeatures`, `m_thinUndoDifficulty`) is taken **only on first mutation per session** (i.e. when `snapshotExists == false`). All iterative passes inside one click share the snapshot, and a second `Thin` click after a partial converge does not overwrite the original-marker snapshot. `Undo` restores both arrays atomically and clears the snapshot.
- **Tuning** — `m_autoMarkerThinNps` (default 4 /s, range 1–10) lives in the existing AI tuning popup ("AI" toolbar button → "Thin markers target NPS" slider). Reset-defaults restores 4. Lower the slider and re-click `Thin` to thin further from the already-thinned state — `Undo` still walks all the way back to the original analyze output.

### Auto-save (2026-04-26)

Crash-safe + drag-safe persistence of in-flight editor state.

- **Cadence — 30 s, debounced.** `Engine::tickAutoSave(dt)` runs every frame; `m_autoSaveTimer` increments only while `m_editorDirty == true`. Going clean → dirty resets the timer to 0 (next save fires 30 s after the *first* edit, not relative to the previous save). Dirty-while-dirty is a no-op so a long edit session still saves on schedule.
- **Drag-safe gate** — `if (ImGui::IsAnyMouseDown()) { m_autoSaveTimer = kAutoSaveIntervalSec - 0.5f; return; }` defers the flush until 0.5 s after mouse-up. Covers note placement, hold stretching, splitter drag, slider scrub. Without this the autosave could fire mid-gesture and write a partial-state hold/arc.
- **Layer gate** — never fires during `EditorLayer::GamePlay` (no editor data to persist; disk write would jitter the audio thread).
- **Dirty detection (hybrid)** —
  - **Ambient**: `tickAutoSave` checks `ImGui::IsMouseReleased(0|1|2)` outside Gameplay each frame and calls `markEditorDirty()`. False positives (clicking a tab, resizing a splitter) cost at most one extra cadence-rate disk write.
  - **Explicit `markEditorDirty()` calls** at high-signal mutators: `thinMarkersInDensityHotspots`, the Analyze-Beats result callback. Wider point-instrumentation can be added later — the ambient catch covers everything in the meantime.
- **Flush path — `Engine::performAutoSaveNow(reason)`** wraps `m_songEditor.flushChartsForAutoSave()` (new public delegating to private `exportAllCharts()`) and `m_musicSelectionEditor.save()` in **independent try/catch** so a throw in one editor does not skip the other. Both editors already follow the user-data-writes-fail-loud rule internally — the outer try/catch is the crash-hook safety net. Clears `m_editorDirty`, resets the timer, and posts a 3 s pale-blue toast `Auto-saved (reason)` to the SongEditor status row (distinct from the green manual-save toast).
- **Crash hooks (`Engine::installCrashHooks`)** — installed once after window creation:
  - `glfwSetWindowCloseCallback` → flush before X-button close.
  - `SetUnhandledExceptionFilter` (Win) → flush, then `EXCEPTION_CONTINUE_SEARCH` so debugger / WER still gets the crash.
  - `SetConsoleCtrlHandler` (Win) → catches Ctrl+C and console-window close.
  - `std::set_terminate` → catches unhandled C++ exceptions (e.g. `noexcept` violation), then `std::abort`.
  - `mainLoop` exit → final `if (m_editorDirty) performAutoSaveNow("exit")` as belt-and-suspenders for the clean-shutdown path.
  - All four call into the same `performAutoSaveNow`, which is itself idempotent (clears `m_editorDirty` so the post-loop final flush is a no-op when the close-callback already fired).
- **Static instance pointer** — `Engine::s_autosaveInstance` (set in ctor, cleared in dtor) lets the file-static OS callbacks reach the live engine without capturing `this`. First-instance wins; multi-engine isn't a thing in this project.

### Asset-tile hover tooltips (2026-04-26)

Asset tiles in the Assets strip (SongEditor / MusicSelection / StartScreen) hover-tooltip the **filename** only, never the full relative path. Filenames are then clamped via `shortenForTooltip(s, maxLen=48)` in `engine/src/ui/AssetBrowser.h` — head + `...` + extension — because real-world filenames from WeChat / browser saves can run hundreds of characters and would overflow the screen otherwise. Drag-drop `ASSET_PATH` payloads, deletion targets, and persisted song fields keep using the **full** relative path; only the visible tooltip text changes. Three tooltip families per editor are routed through this helper: image tile, audio tile, and material tile (rich-preview branch + plain-fallback branch).

### Editors compose game-side views (2026-05-03)

`StartScreenEditor` and `MusicSelectionEditor` are no longer the source of truth for player-facing rendering. They each `: public` a game-side `View` class that lives in `engine/src/game/screens/`:

- `class StartScreenEditor : public StartScreenView` — view owns background/logo/tap-prompt state + JSON load/save + `renderGamePreview`. Editor adds asset browser, thumbnail cache, AI shader gen, materials panel, status messages, panel split ratios.
- `class MusicSelectionEditor : public MusicSelectionView` — view owns sets/songs hierarchy + scroll + cover cache + audio preview + wheel/cover/difficulty/play rendering. Editor adds asset browser, hierarchy panel, dialog state, settings overlay.

The reason for *inheritance* over composition was diff size — the editor methods reference `m_logoText`, `m_sets`, etc. directly; inheritance lets them keep working without field-renames. The editor sidebars mutate inherited `protected` fields directly when authoring controls fire.

**Two virtual hooks** let editors extend view behaviour without polluting the views with editor symbols:

- `virtual void StartScreenView::load(const std::string&)` — editor overrides to clear thumbnails before the JSON parse runs.
- `virtual void MusicSelectionView::onSongCardDoubleClick(int)` — view default is no-op; editor overrides to open SongEditor for the double-clicked song.

`SongInfo`, `MusicSetInfo`, and `Difficulty` types moved out of `engine/src/ui/MusicSelectionEditor.h` into `engine/src/game/screens/MusicSelectionView.h`. Existing editor code that uses them keeps compiling because the editor header includes the view header.

`renderGamePreview` signatures changed:

- `StartScreenView::renderGamePreview(ImVec2 origin, ImVec2 size)` — same shape as before; inherited by the editor.
- `MusicSelectionView::renderGamePreview(ImVec2 origin, ImVec2 size, IPlayerEngine* engine)` — extra `engine` arg lets the play button call `engine->launchGameplay(...)` and `engine->isTestMode()`. The editor's full-screen test-mode block in `render()` now passes `engine` through; `GameFlowPreview.cpp` also got the matching arg-update.

Editor-only files that stay desktop-only (and never compile into the Android lib): `ProjectHub.cpp`, `SongEditor.cpp`, `SettingsEditor.cpp`, `GameFlowPreview.cpp`, `AssetBrowser.cpp`, `ImageEditor.cpp`, `SceneViewer.cpp`, `ImGuiLayer.cpp`. The Android target's CMake source list (`engine/src/android/CMakeLists.txt`) only adds `SettingsPageUI.cpp` from `engine/src/ui/` — that's the already-shared player settings page that pre-dates the split.

For future editor screens that want a player-facing twin, the pattern is:
1. Create `XxxView` in `engine/src/game/screens/` with the player rendering and any data the player needs.
2. `class XxxEditor : public XxxView` — the editor adds authoring chrome.
3. Use a `virtual` hook for any editor-only behaviour the view's render path needs to trigger.
4. Sidebar widgets read/write inherited protected fields directly.

The class-name test: if you'd put `Editor` in the name, that file isn't going into the Android lib.
