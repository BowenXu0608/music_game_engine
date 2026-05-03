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

**Per-page speed overrides (2026-04-17):** `ScanPageOverride {pageIndex, speed}` entries stored sparsely in `ChartData::scanPageOverrides`. At load time `ChartLoader::expandScanPagesToSpeedEvents` (see `engine/src/game/chart/ScanPageUtils.h`) rebuilds `scanSpeedEvents` from the overrides: one zero-duration step event at each overridden page's start time, plus a return-to-1.0 event at the next page if the next page has no override. Precedence: when a chart contains `scanPages`, overrides are authoritative and any on-disk `scanSpeedEvents` are ignored and replaced at load. BPM changes that fall mid-page are handled by truncating the page at the timing-point boundary (`partialTail=true`), so the scan line stays continuous across BPM boundaries.

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

Arcaea-style 3D highway with a ground lane, an elevated sky band, and arc holds that curve between them. Uses `checkHitPosition` / `beginHoldPosition` for spatial input. Arc holds scored holistically on release via `judgeArc(accuracy)`.

### Playfield geometry — single source of truth (2026-04-17)

All mesh construction references five constants in the header; do **not** introduce parallel magic numbers elsewhere in the file:

```
SCROLL_SPEED    = 8.f    // world units/sec notes scroll toward camera
GROUND_Y        = -2.f   // ground plane (world y)
LANE_HALF_WIDTH = 3.f    // lane near-edge spans x ∈ [-3, +3]
LANE_FAR_Z      = -60.f  // lane back edge (far)
JUDGMENT_Z      = 0.f    // lane front edge / judgment plane
```

The ground mesh, the judgment gate, the tap-lane mapping, and the arc/arctap coordinate transforms all derive from these. Changing lane width means editing *one* line.

### Rectangle judgment gate (2026-04-17)

Four thin coplanar quads at `z = JUDGMENT_Z` whose corners are byte-for-byte identical to the ground mesh's near-edge corners (`±LANE_HALF_WIDTH`, `GROUND_Y`, `JUDGMENT_Z`). Visual hierarchy per Arcaea convention:

- **Bottom bar** — bright warm yellow, thickness 0.08, grown *upward* from `GROUND_Y` so its bottom edge matches the ground's near edge exactly. This is the ground judgment line.
- **Sky bar** — dim cool cyan, thickness 0.035, at `GROUND_Y + skyHeight`. This is the arctap judgment line.
- **Vertical posts** — dim neutral, thickness 0.035, grown *inward* from `±LANE_HALF_WIDTH` so their outer edges stay flush with the lane.

`skyHeight` reads from `GameModeConfig::skyHeight` (editable via the SongEditor slider in the 3D Mode properties).

### Lane mapping — Bandori-style slot spacing (2026-04-17)

`m_laneCount` starts from `config->trackCount` (default 7) and auto-expands as the chart is scanned — mirrors `BandoriRenderer::onInit`. For each `Tap`/`Flick`, if `lround(laneX) >= m_laneCount`, `m_laneCount` is bumped to `lane + 1`.

Tap world-x is slot-centered (not edge-based):

```
laneSpacing = (2 * LANE_HALF_WIDTH) / N        // N equal slots across the lane width
wx = (lane - (N-1)*0.5) * laneSpacing          // lane 0 is half-a-slot inside left edge
```

With `N=12`, `laneSpacing=0.5`, lane 0 center at `-2.75` and lane 11 at `+2.75` — every tap fits inside the lane. The older `(N-1)`-based mapping put lane 0 *on* the edge at `x=-3`, so the tap mesh bled outside.

The tap mesh itself scales to the slot: `hw = min(0.4, slotHalf * 0.9)`. Prevents neighbor-slot overlap at high lane counts.

### Arc coordinate transform (2026-04-17)

Chart arcs author `startX/endX/startY/endY` in **normalized [0,1]** space. The previous renderer treated those as raw world coords, so arcs floated in a random corner away from the lane. `evalArc(arc, t)` now returns world coords:

```
wx = (nx * 2 - 1) * LANE_HALF_WIDTH            // [-LANE_HALF, +LANE_HALF]
wy = GROUND_Y + ny * m_skyHeight               // [GROUND_Y, GROUND_Y + skyHeight]
```

`h=0` → arc sits on the ground judgment line. `h=1` → sits on the sky judgment line. Midway values map linearly. World-y is independent of z: a constant-h arc is a flat horizontal ribbon at that y, regardless of distance.

### Smooth arc clipping via dynamic vertex buffer (2026-04-17)

Previously arcs were built once as a static mesh and just translated; the consumed portion kept rendering past the judgment line, and the old mesh topology made any clipping snap to segment boundaries.

Fix: each arc's vertex buffer is created via `createDynamicBuffer` (host-mapped). In `onUpdate` every frame:

```
tClip = clamp((songTime - startTime) / duration, 0, 1)
writeArcVertices(am, tClip)   // memcpy into mesh.vertexBuffer.mapped
```

`writeArcVertices` samples the arc from `t ∈ [tClip, 1.0]` (not `[0, 1]`) — the first vertex pair sits exactly at the current judgment-line parameter, and since `tClip` is a continuous float, the trailing edge recedes smoothly frame-by-frame with no segment popping. The index buffer stays static (topology never changes).

Staging-based `updateMesh` is **not** used for this path — it calls `vkQueueWaitIdle` per upload and would tank the frame budget. Host-mapped writes are free.

### Void-arc and ArcTap rendering (2026-04-17)

- **Void arcs** (`isVoid=true`) are invisible ArcTap carriers per Arcaea convention. They were previously rendering as dim cyan ribbons at 40% alpha, showing up as "gray strips" on the lane. Now skipped entirely in `onRender`.
- **ArcTaps** were being dropped in `onInit` (no handler). Added `m_arcTaps` container, `buildArcTapMesh` (thin camera-facing horizontal bar, 0.64 × 0.16), and a render loop that translates each to `((arcX·2-1)·LANE_HALF, GROUND_Y + arcY·skyHeight, JUDGMENT_Z - z)` where `z = (noteTime - songTime)·SCROLL_SPEED`. Culled at `z < 0` so they vanish at the judgment plane.

### Rim-glow pitfall — use camera-facing normals for bright meshes (2026-04-17)

The shared `mesh.frag` applies a rim glow: `rim = 1 - |dot(n, {0,0,1})|; outColor = base + base * rim * 2`. A vertex normal of `{0, 1, 0}` (ground-up) gives `rim = 1` and triples the base color, saturating any bright color to pure white.

- **Ground mesh**: keep `{0, 1, 0}` — its base is dark `(0.15, 0.15, 0.25)`, and the rim glow is what makes it read as purple.
- **Tap, ArcTap, Arc meshes**: use `{0, 0, 1}` — these start bright (yellow / white / cyan / pink) and would blow to white under rim glow.

This was the root cause of "all notes look the same color (white)" — not a rendering bug, a normal-vector oversight.

### Hit particles (2026-04-17)

`ArcaeaRenderer` didn't override `showJudgment` — the no-op base class was being called, so no particles fired. Key details when implementing:

1. **Emit in world space, not screen pixels.** `ParticleSystem::flush` runs every particle through `viewProj * vec4(inPos, 0, 1)` in `quad.vert`. BandoriRenderer gets away with pixel coords because it uses a screen-space ortho camera; Arcaea is 3D perspective, so emitting at pixel coords puts particles at nonsense clip positions. Use world-scale size (`0.15`) and velocity (`3 u/s`) as well.
2. **Route by `lane`, then by a sky-event table.** `Engine::dispatchHitResult` clamps arc/arctap lane from `-1` to `0` before calling `showJudgment`, so lane alone doesn't identify the note type. Solution:
   - `lane > 0` → ground slot for that lane (taps, flicks, hold sample ticks).
   - `lane == 0` → consult `m_hitEvents` (pre-computed list of *sky* events only: arctaps at `(arcX, arcY)` world, arc start/end at `evalArc(arc, 0/1)`). Within a tight ~30 ms window, prefer the sky position. Falls back to lane-0 ground otherwise.

The ground table is deliberately empty — adding ground taps there would let a hold-tick at `t=1.65` snap to an arctap event at `t=1.67` and put the particle in the sky.

### No chart looping (2026-04-17)

`onUpdate` used to wrap `m_songTime` with `fmod(songTime, maxTime + 1.0)` so the chart replayed forever. Removed — `m_songTime = songTime` passes audio time straight through. Per-type culling handles end-of-song state: taps cull at `z < 0`, arcs cull at `songRel >= duration`, arctaps cull at `z < 0`.

### Tap cull tightened (2026-04-17)

Was `z < -2` (taps lingered ~0.25 s past the judgment line, falling through the foreground). Now `z < 0` — taps disappear exactly at the judgment plane.

## PhigrosRenderer

Rotating judgment lines with `SceneGraph` hierarchy. Uses `checkHitPhigros` which projects touch onto the rotating line. Notes attached to judgment lines via `JudgmentLineEvent::attachedNotes`.

## Mode Selection Flow

```
SongEditor -> Test Game -> export charts -> spawn child process
-> StartScreen -> MusicSelection -> START
-> Engine::launchGameplay -> ChartLoader::load -> createRenderer -> setMode -> play
```
