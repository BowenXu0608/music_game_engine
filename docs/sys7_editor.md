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
+-------------+-----------------------------------+
| Left Sidebar| Scene Preview                     |
| (scrollable)|--- draggable splitter ------------|
|             | Chart Timeline                    |
| - Song Info | [Toolbar + Difficulty + Notes]    |
| - Audio     |-----------------------------------|
| - Game Mode | Waveform Strip (120px)            |
| - Config    |-----------------------------------|
| - Assets    | Back | Save | Test | Play/Pause   |
+-------------+-----------------------------------+
```

### Config Panels

**Game Mode:** DropNotes/Circle/ScanLine + 2D/3D dimension + track count.

**Camera:** Eye position, look-at target, FOV (20-120 deg).

**HUD:** Score + Combo position/font/color/glow/bold per-element via `HudTextConfig`.

**Score:** Perfect/Good/Bad score values. Achievement FC/AP image pickers.

**Disk Animation (Circle mode):** Keyframed rotate/scale/move events. Per-difficulty storage (`m_diffDiskRot/Move/Scale`). Add/edit/delete UI with easing combo. DiskFX timeline strip.

**Scan Line Speed (ScanLine mode):** `ScanSpeedEvent` keyframes (0.1x-4.0x). Per-difficulty storage (`m_diffScanSpeed`). Phase table rebuilt lazily.

**Judgment Windows:** Perfect/Good/Bad ms thresholds.

### Scan Line Authoring

Scan-line mode skips the chart timeline; scene fills the full height. In-scene tool row: Tap / Flick / Hold / Slide. All clicks gated by `|mouseY - scanLineY| < 10px`.

**Tap/Flick:** LMB on scan line commits note.

**Hold:** LMB starts head. Mouse wheel extends across sweeps (alternating scroll directions). LMB commits endpoint. Preview shows zigzag body + "+N sweeps" indicator.

**Slide (Cytus-style):** LMB on scan line starts. RMB while LMB held places control-point nodes (straight lines between them). Each node = sample tick point. Release LMB commits. Direction enforcement prevents crossing scan-line turns.

### Chart Persistence

Save -> `exportAllCharts()` writes UCF JSON per difficulty. Song open -> loads charts back via `ChartLoader`. Round-trips: notes, scan fields, disk animation, scan speed events, waypoints, sample points.

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
