---
name: Game Mode Plugin System
description: GameModeRenderer interface + 5 mode plugins (Bandori, Phigros, Arcaea, Cytus, Lanota) with all gameplay features
type: project
originSessionId: d4e6dddd-1cc1-4f7b-8da6-079be9eb81c0
---
# System 6 — Game Mode Plugins ✅ COMPLETE

**Files:** `engine/src/game/modes/`

## GameModeRenderer Interface

```cpp
class GameModeRenderer {
    virtual void onInit(Renderer&, const ChartData&, const GameModeConfig* = nullptr) = 0;
    virtual void onResize(uint32_t w, uint32_t h) = 0;
    virtual void onUpdate(float dt, double songTime) = 0;
    virtual void onRender(Renderer&) = 0;
    virtual void onShutdown(Renderer&) = 0;
    virtual const Camera& getCamera() const = 0;
    virtual void showJudgment(int lane, Judgment judgment) {} // default no-op
};
```

Engine creates renderers via `createRenderer(GameModeConfig)` factory.

## BandoriRenderer (2D Drop Notes)

**Dynamic lane count** from `config->trackCount`. **Configurable camera** (eye, target, FOV from GameModeConfig). **Auto lane spacing** computed from camera FOV + aspect ratio.

**Rendering:** Perspective-projected ground-plane notes scrolling toward hit zone. Hold bodies tessellated as ribbon strips with multi-waypoint cross-lane paths (Straight/Angle90/Curve/Rhomboid transitions). Hold sample-point markers along the ribbon.

**Note colors:** Tap=gold, Hold=cyan, Flick=red, Drag=green `{0.6,1.0,0.4,0.85}`.

**Judgment displays:** `std::vector<JudgmentDisplay>` sized to `m_laneCount` in `onInit` (was fixed array of 12). `showJudgment` emits particle bursts (Perfect=green/20, Good=blue/14, Bad=red/8).

**Hold visibility:** Upper Z clip = +12 (vs +2 for taps) so the whole hold shape stays visible.

## LanotaRenderer (Circle Mode)

**Geometry:** Two concentric disks — inner spawn disk (`INNER_RADIUS=0.9`) and outer hit ring (`BASE_RADIUS=2.4`). Notes travel radially outward. Lane 0 at 12 o'clock, clockwise. `angle = PI/2 - (lane/trackCount) * 2PI`.

**Disk animation (2026-04-12):** Keyframed rotate/scale/move via `DiskRotationEvent`, `DiskMoveEvent`, `DiskScaleEvent`. Segment-based interpolation with easing (Linear/SineInOut/QuadInOut/CubicInOut). Phase table built in `onInit`, drives `m_diskRotation`, `m_diskScale`, `m_diskCenter` each frame. Camera follows disk center via `rebuildPerspVP`.

**reloadDiskAnimation(anim):** Re-seeds keyframes from edited chart. Used by editor for live updates.

**computeEnabledLanesAt(songTime):** Const query returning bitmask of lanes visible on screen at a given time. Samples all three transforms locally without touching mutable state.

**Rendering:** All radius references scaled by `m_diskScale`. Hold bodies tessellated as arc tiles. Per-note `laneSpan` (1-3) for wide notes. Max lanes = 36.

**Spatial picker:** `pickNoteAt(screenPx, songTime, pixelTol)` → `PickResult{noteId, ringIdx, type}`. Projects each note's world position to screen via `m_perspVP`, picks nearest within tolerance + timing window.

## CytusRenderer (Scan Line Mode)

**Variable-speed scan line (2026-04-12):** Base period `T=240/BPM`. `ScanSpeedEvent` keyframes drive a precomputed phase-accumulation table (Simpson's rule integration). `scanLineFrac(t)` binary-searches the table, converts phase to triangle wave [0..1]. Falls back to constant-speed fmod when empty.

**Straight-line slides (Cytus-style, 2026-04-12):** Control-point nodes placed by user (LMB start + RMB nodes). Straight lines between nodes. Each node = sample point. `linearPathEval` for gameplay evaluation. `consumeSlideTicks` fires at sample-point times. `slideExpectedPos` returns expected screen position.

**Multi-sweep holds (2026-04-12):** Holds cross scan-line direction changes. `ScanNote::holdSweeps` tracks extra sweeps. Zigzag body rendering with turn segments. Page visibility checks `[notePage, endPage]` range. Fade logic uses end-page boundaries.

**Page visibility:** Notes shown only during their sweep page. Multi-sweep holds span multiple pages. Scale/fade approach animation (0.30->1.0 scale, 0.25->1.0 alpha).

**Spatial picker:** `pickNoteAt(screenPx, songTime, pixelTol)` with +/-0.18s timing gate. Slide paths checked by vertex proximity.

**Circle mode input wiring (2026-04-10):** Two input paths converge on `markNoteHit` + particle feedback:
- Touch: `pickNoteAt(screenPx, songTime, dp(48))` projects each note via `m_perspVP`, picks nearest within fingertip tolerance + 0.15s timing window. Then `consumeNoteById` validates. Visual feedback at the note's exact disk position.
- Keyboard 1-7: `checkHit(lane, songTime)` lane-based path. `showJudgment(lane, j)` reverse-maps `targetAngle = PI/2 - lane*(2PI/trackCount)`, calls `findNoteByAngle` to locate the note, fires particles at the disk position.

**Game-mode factory fix (2026-04-10):** `Circle` was erroneously wired to `CytusRenderer` and `ScanLine` to `PhigrosRenderer`. Fixed so Circle->LanotaRenderer, ScanLine->CytusRenderer.

**Test Game button unification (2026-04-10):** All three Test Game buttons (StartScreen, MusicSelection, SongEditor) now route through `Engine::spawnTestGameProcess()` — each spawns a child process, editor window stays interactive.

## Known Deferred Items

1. **Phigros mode unreachable:** No `GameModeType::JudgmentLine` enum value or UI button. `PhigrosRenderer` exists but is dead code. The `dynamic_cast<PhigrosRenderer*>` branches in Engine.cpp gesture dispatch always return null.
2. **Circle hold-drift cancellation:** `CIRCLE_HOLD_DRIFT_DP = 64` reserved but no `SlideMove` case in `handleGestureCircle` to break holds when the finger drifts off the note's X column.

## ArcaeaRenderer (3D Drop Notes)

Floor + arc + sky notes with perspective. Uses `checkHitPosition` / `beginHoldPosition` for spatial input. Arc holds scored holistically on release via `judgeArc(accuracy)`.

## PhigrosRenderer

Rotating judgment lines with `SceneGraph` hierarchy. Uses `checkHitPhigros` which projects touch onto the rotating line. Notes attached to judgment lines via `JudgmentLineEvent::attachedNotes`.

## Mode Selection Flow

```
SongEditor -> Test Game -> export charts -> spawn child process
-> StartScreen -> MusicSelection -> START
-> Engine::launchGameplay -> ChartLoader::load -> createRenderer -> setMode -> play
```
