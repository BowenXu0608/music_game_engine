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

### Chart Audit (SongEditor Properties, 2026-04-19)

Read-only AI quality review. Point at the current chart, get a structured report with density spikes, jacks, crossovers, dead zones, difficulty concerns. Complements the Autocharter (generation) and Copilot (edit) — Audit never mutates the chart.

**Hybrid design — local scan + LLM narrate.** Small local models (qwen2.5:3b) hallucinate timestamps when asked to reason over raw note lists, and the full chart often blows the context window. Fix: pre-digest facts in C++; the model only prioritizes + writes prose with the timestamps we already gave it.

- `computeAuditMetrics(notes, duration)` → concrete violations:
  - **Density hotspots**: 4s sliding windows with ≥ 24 notes, merged into contiguous runs.
  - **Jacks**: ≥ 3 consecutive same-lane notes, each within 500 ms of the prior.
  - **Crossovers**: adjacent notes with `|dLane| ≥ 3` within 150 ms.
  - **Dead zones**: gaps > 8 s between onsets.
  - **Peak NPS** (2 s sliding window, onset-driven) + **Avg NPS** + type counts.
- `describeMetricsForPrompt` caps the text block (12 density / 12 jacks / 12 crossovers / 6 dead zones) to keep the user message small.
- `parseAuditReport` extracts the first `{..}` envelope (same pattern as `ChartEditOps`) → `{summary, issues:[{severity, time, end_time, category, message}]}`.

**UI:** Collapsing header "Chart Audit" below "Editor Copilot". Audit button fires the request; response parses to a report. Rendering: summary at top, one row per issue with severity tag (HIGH red / MED amber / LOW blue) + clickable `[time]` button that seeks `m_sceneTime` and scrolls the timeline to `max(0, time - 2s)` + category + message.

**Files:** `engine/src/editor/ChartAudit.{h,cpp}`. SongEditor gained pimpl `m_audit` (`AuditState` in .cpp) alongside `m_copilot`, with the same forward-declared-unique_ptr pattern + `pollAudit()` + `renderAuditPanel()` wiring. Duration comes from `m_waveform.durationSeconds` (lazy-loaded) with fallback to last-note time. Shares `ai_editor_config.json` via `aiConfigPath()`.

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
