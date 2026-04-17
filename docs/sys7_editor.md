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

### Scan Line Authoring

Scan-line mode skips the chart timeline; scene fills the full height. In-scene tool row: Tap / Flick / Hold / Slide. All clicks gated by `|mouseY - scanLineY| < 10px`.

**Tap/Flick:** LMB on scan line commits note.

**Hold:** LMB starts head. Mouse wheel extends across sweeps (alternating scroll directions). LMB commits endpoint. Preview shows zigzag body + "+N sweeps" indicator.

**Slide (Cytus-style):** LMB on scan line starts. RMB while LMB held places control-point nodes (straight lines between them). Each node = sample tick point. Release LMB commits. Direction enforcement prevents crossing scan-line turns.

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

Save -> `exportAllCharts()` writes UCF JSON per difficulty. Song open -> loads charts back via `ChartLoader`. Round-trips: notes, scan fields, disk animation, scan speed events, waypoints, sample points, arc data (startX/Y, endX/Y, easeX/Y, color, void), arctap positions.

**Per-(mode, difficulty) chart files (2026-04-12):** Filenames are keyed on both game mode and difficulty: `assets/charts/<song>_<modeKey>_<diff>.json`, where `modeKey ∈ {drop2d, drop3d, circle, scan}`. Each (mode, difficulty) pair owns an independent chart file — switching modes never overwrites or reuses another mode's notes.

- `modeKey(gm)` helper in `SongEditor.cpp` anonymous namespace returns the key from `GameModeType` + `DropDimension`.
- `chartRelPathFor(name, gm, diff)` composes the relative path used by both export and load.
- `reloadChartsForCurrentMode()` clears in-memory `m_diffNotes` / `m_diffMarkers` / disk-FX / scan-speed / BPM state, then loads the three (easy/medium/hard) files for the current `gameMode` from disk (starting empty when a file is absent) and updates `m_song->chartEasy/Medium/Hard` accordingly.
- `loadChartFile(diff, chartRel)` is the extracted single-chart loader used by both `setSong()` and `reloadChartsForCurrentMode()` (replaces the previous inline lambda).
- Mode / Dimension buttons in `renderGameModeConfig()` hook the switch: `exportAllCharts()` saves the old mode's charts, the mode/dimension field is updated, then `reloadChartsForCurrentMode()` pulls the new mode's charts.

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
