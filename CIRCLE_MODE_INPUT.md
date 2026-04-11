# Circle (Lanota) Mode — Input System Wiring

**Date:** 2026-04-10
**Status:** Implemented
**Affected areas:** `engine/src/game/modes/LanotaRenderer.{h,cpp}`, `engine/src/gameplay/HitDetector.{h,cpp}`, `engine/src/engine/Engine.{h,cpp}`

---

## 1. Context

After fixing the game-mode factory mismatch (Circle was launching `CytusRenderer` instead of `LanotaRenderer`) and adding a lane→ring fallback so Drop-Notes charts work in Circle mode, the Lanota disk *rendered* correctly but **touch input was broken**: the engine's gesture dispatch fell through to `handleGestureLaneBased`, which divides the screen into horizontal slices by `screenX / trackCount`. That mapping has nothing to do with the disk geometry — taps registered hits by accident, not by tapping the visible note.

**Goal.** On phone, tapping the disk *where a note appears* at the right time registers a hit and gives visual feedback at that note. On PC, keyboard `1`..`7` keeps working as the test path; each key already maps to the lane the fallback synthesizer placed at angle `lane * 2π/trackCount − π/2`, so it just needs visual feedback that lands on the right note.

---

## 2. Approach

Two paths converge on the same `markNoteHit` + particle-burst feedback in `LanotaRenderer`:

| Path | Picker | Detector call |
|---|---|---|
| **Touch / mouse** | `LanotaRenderer::pickNoteAt(screenPx, t, pickPx)` — projects every active note to screen, picks the closest within a fingertip-sized pixel tolerance and the temporal hit window | `HitDetector::consumeNoteById(noteId, t)` |
| **Keyboard `1`..`7`** | `HitDetector::checkHit(lane, t)` — unchanged lane-based path; the underlying `TapData.laneX` is intact because the Lanota fallback only **copies** notes, doesn't mutate them | existing `dispatchHitResult` → `LanotaRenderer::showJudgment(lane, j)` reverse-maps lane to the synthesized angle and finds the matching note |

Bypassing `dispatchHitResult` for the touch path lets the visual feedback fire at the picked note's exact projected disk position rather than via lane-based search.

---

## 3. DPI-aware hit tolerance

The pick tolerance is in **density-independent pixels** (160-DPI Android reference), converted via `ScreenMetrics::dp(...)` (`engine/src/input/ScreenMetrics.h:49`):

| Constant | Value | Physical | Used by |
|---|---|---|---|
| `CIRCLE_PICK_DP` | `48` dp | ≈ 7.6 mm (fingertip contact) | `Engine::handleGestureCircle` → `pickNoteAt` |
| `CIRCLE_HOLD_DRIFT_DP` | `64` dp (reserved) | ≈ 10 mm | not yet implemented — see `OUT_OF_SCOPE.md` |

On a 480-DPI phone the pick tolerance auto-expands to ~144 px without code changes; the rest of the input system already uses this convention.

---

## 4. Files modified

| File | Change |
|---|---|
| `engine/src/game/modes/LanotaRenderer.h` | New state: `m_hitNotes`, `m_trackCount`, `m_renderer`. New API: `pickNoteAt`, `markNoteHit`, `emitHitFeedback`, `showJudgment` override. Private helpers `projectNoteScreen`, `findNoteByAngle`. |
| `engine/src/game/modes/LanotaRenderer.cpp` | Stored `trackCount` and `m_renderer` from `onInit`. Skip hit notes in `onRender`. Implemented all the new methods. Cleared state in `onShutdown`. |
| `engine/src/gameplay/HitDetector.h` / `.cpp` | Added `consumeNoteById(noteId, t)` and `beginHoldById(noteId, t)`. Lane-based methods unchanged. |
| `engine/src/engine/Engine.h` | Declared `handleGestureCircle`. |
| `engine/src/engine/Engine.cpp` | Added `#include "input/ScreenMetrics.h"`. Added `LanotaRenderer*` dispatch branch in the gesture callback. Implemented `handleGestureCircle` (Tap / Flick / HoldBegin / HoldEnd). |

---

## 5. Key implementation details

### `LanotaRenderer::pickNoteAt`

Iterates `m_rings × ring.notes`. Skips hit notes and notes outside the ±0.15 s window. Computes each candidate's current 3D position with the same formula used by `onRender` (`angle = rd->angle + ring.currentAngle`, `noteZ = -timeDiff * SCROLL_SPEED_Z`), projects through `m_perspVP` via `w2s`, and measures pixel distance to the tap. Score = `distSq + (timeDiff² × 2e5)` so pixel distance dominates but timing breaks ties. Returns `{noteId, ringIdx, NoteType}` for the best match within `pickPx`.

### `LanotaRenderer::showJudgment` (keyboard path)

Reverse-maps lane → authored angle: `targetAngle = lane * 2π/trackCount − π/2`. Calls `findNoteByAngle(targetAngle, 0.15 rad)` which iterates all rings and compares against `rd->angle` (the **authored** angle, not the rotated one — the keyboard mental model is "key N hits the note placed at lane N's angle, regardless of how far the disk has spun"). If found: `markNoteHit` + `emitHitFeedback`.

### `Engine::handleGestureCircle`

```cpp
const float pickPx = ScreenMetrics::dp(CIRCLE_PICK_DP);

auto judgeAndFeedback = [this, &lan](const HitResult& hit, Judgment j) {
    m_judgment.recordJudgment(j);
    m_score.onJudgment(j);
    lan.markNoteHit(hit.noteId);
    lan.emitHitFeedback(hit.noteId, j);
};

switch (evt.type) {
    case GestureType::Tap: { /* pickNoteAt → consumeNoteById → judgeAndFeedback */ }
    case GestureType::Flick: { /* same + judgeFlick when noteType == Flick */ }
    case GestureType::HoldBegin: { /* pickNoteAt → beginHoldById → m_activeTouches */ }
    case GestureType::HoldEnd: { /* m_activeTouches → endHold → judgeAndFeedback */ }
}
```

The `judgeAndFeedback` lambda inlines the four bookkeeping steps that `dispatchHitResult` would do, but replaces the lane-based `showJudgment` call with `markNoteHit + emitHitFeedback` so visual feedback fires at the picked note's actual disk position.

---

## 6. Verification

1. **Build clean.** `cmake --build C:/Users/wense/Music_game/build --config Debug` produces `MusicGameEngineTest.exe` with no new errors.
2. **PC keyboard test.** Launched the engine, played the `Aa` chart in Circle mode. Engine log showed real hits during play:
   ```
   Hit - Bad | Score: 100 | Combo: 0
   Hit - Good | Score: 600 | Combo: 1
   ```
   Notes vanish from the ring on hit; particle bursts fire at the disk position.
3. **Touch / mouse test.** Click directly on a visible note → hit registers and particles emit at click position. Click on empty disk space → no hit. Click on a note far from the right time → no hit (filtered by ±0.15 s).
4. **Bandori regression check.** Picked Basic Drop Notes mode → input still works; the new dispatch branch only fires for `LanotaRenderer`.

---

## 7. Related work

- **Game-mode factory bug fix** (same date) — `Circle` was launching `CytusRenderer` instead of `LanotaRenderer`. Fixed in `Engine.cpp::createRenderer` so noun ↔ renderer roles match.
- **Lanota lane fallback** (same date) — `LanotaRenderer::onInit` now synthesizes a default ring from `TapData` notes when no `LanotaRingData` is present, so Drop-Notes charts can be played in Circle mode.
- **Perspective notes** (same date) — both `BandoriRenderer` and `LanotaRenderer` now project all four note corners (Bandori: ground-plane trapezoid; Lanota: 8-segment curved arc tile) so notes follow the same perspective as their lanes/rings.

---

## 8. Circle Mode v2 Redesign (2026-04-10)

Substantial visual + gameplay pass on Circle mode. All changes are gated behind `GameModeType::Circle` — other modes are entirely unaffected.

### 8.1 Two-disk geometry + straight radial travel

- `INNER_RADIUS = 0.9`, `BASE_RADIUS = 2.4` (the large hit disk). Both rings are drawn each frame.
- Notes fly radially outward in a **straight line**: `noteRadius = ring.radius − clamp(timeDiff/APPROACH_SECS, 0, 1) · (ring.radius − INNER_RADIUS)`. At `timeDiff == APPROACH_SECS` the note spawns on the inner disk; at `timeDiff == 0` it reaches the outer (judge) ring. The disk is flat at `z=0`; the old Z-scroll (`SCROLL_SPEED_Z`) was removed.
- `ring.currentAngle` is pinned to `0` each frame unless the chart provides explicit `rotationEvents`, guaranteeing straight-line travel.

### 8.2 Lane orientation: lane 0 at 12 o'clock, clockwise numbering

World +Y projects to screen top. Clockwise in screen space means **decreasing** angle, so the formula is:

```
angle = π/2 − lane · (2π / trackCount)
```

Any earlier `lane·θ − π/2` form inverted top/bottom. All four call sites use the new form: the fallback `TapData → LanotaRingData` conversion, the divider loop, the lane-0 tick, and `showJudgment`'s reverse mapping.

### 8.3 Lane dividers between lanes, not on them

Dividers are drawn at lane **boundaries**, half a lane offset from centers:

```
divider_angle = π/2 − (lane − ½) · (2π / trackCount)
```

Kept strictly inside the outer disk (`INNER_RADIUS + 0.01` → `outerRing − 0.01`), so nothing bleeds past the hit ring. Lane 0's center (12 o'clock) gets a short gold tick on the outer edge as a visual reference.

### 8.4 Per-note `laneSpan` (1 / 2 / 3), clockwise expansion

`LanotaRingData` gains `int laneSpan` (clamped 1..3). A span-S note at lane N covers lanes N..N+S-1 — width expands **clockwise** from the authored lane.

Implementation: arc visual center is shifted `(S−1)/2 · (2π/trackCount)` clockwise of `rd->angle` (i.e. subtracted), with angular half-width `S · (2π/trackCount) / 2 · 0.96` (2% gap between adjacent same-time notes). The same shift is applied in `pickNoteAt` and `projectNoteScreen` so touch picking and particle feedback track the visual arc.

`findNoteByAngle` still compares against the **authored** `rd->angle` (not the shifted visual center) — pressing key N hits the note authored at lane N regardless of how far it expands clockwise. Tolerance is now `π / trackCount` (half a lane) so lane N only matches authored lane N even at 36 lanes.

### 8.5 Max `trackCount` = 36 (Circle-only)

`SongEditor`'s track slider is `3..36` in Circle mode, `3..12` otherwise. Switching away from Circle auto-clamps `trackCount` down to 12 so non-Circle modes can't inherit an out-of-range value.

### 8.6 Editor UI additions (Circle-only)

- **Default Note Width panel** in the Game Mode config section: `1 / 2 / 3 lanes` buttons + `Apply to All Notes` button. Sets `m_defaultLaneSpan` used by `handleNotePlacement`.
- **Note Properties floating popup** opens when a note is left-clicked in the timeline (tool = None, no modifiers). Shows type/time/track, a per-note lane-width selector, and a `Delete Note` button. Selected note is outlined in yellow with an `×N` tag in the timeline.
- **Scene preview** (`renderSceneView` Circle branch) draws each note's trapezoid across `[track, track + laneSpan]` lanes clockwise from the authored lane, so the live preview matches the in-game arc.

All of the above are gated on `m_song->gameMode.type == GameModeType::Circle`. `m_selectedNoteIdx` is force-reset to `-1` when the mode is anything else, so other modes see zero behavior change.

### 8.7 `laneSpan` data flow end-to-end

`EditorNote.laneSpan` → `TapData.laneSpan` / `HoldData.laneSpan` (new trailing fields, default `1`) → chart JSON (`"laneSpan": N` written only when ≠ 1, so drop-notes charts are byte-identical to before) → `ChartLoader::load` reads back via `clampSpan()` helper → `LanotaRenderer::onInit` fallback copies into `LanotaRingData.laneSpan`.

### 8.8 Files modified

| File | Change |
|---|---|
| `engine/src/game/modes/LanotaRenderer.{h,cpp}` | Two-disk geometry, radial travel, dividers at boundaries, lane 0 at top (clockwise math), per-note `laneSpan` with clockwise expansion, `INNER_RADIUS` constant |
| `engine/src/game/chart/ChartTypes.h` | `TapData.laneSpan`, `HoldData.laneSpan`, `LanotaRingData.laneSpan` (all default `1`) |
| `engine/src/game/chart/ChartLoader.cpp` | `clampSpan()` helper reads optional `"laneSpan"` JSON field for tap/slide/hold/ring |
| `engine/src/ui/SongEditor.{h,cpp}` | `EditorNote.laneSpan`, `m_defaultLaneSpan`, `m_selectedNoteIdx`, track slider `3..36` in Circle mode, Default Note Width panel, Note Properties popup, click-to-select in `renderNotes`, scene preview clockwise expansion, `buildChartFromNotes`/`exportAllCharts` propagate span |

---

## 9. Test Game Button Unification (2026-04-10)

The three Test Game buttons (Start Screen / Music Selection / Song Editor) used to behave differently: Song Editor spawned a child process (editor stayed open), but Start Screen and Music Selection called `enterTestMode()` in-process, which hid the editor UI because the single window was repurposed for the game.

**Fix.** Added `Engine::spawnTestGameProcess(projectPath)` in `engine/src/engine/Engine.{h,cpp}` — saves `music_selection.json` first, then launches `MusicGameEngineTest.exe --test <project>` via `CreateProcessW`. All three Test Game buttons now route through this helper. The editor window stays fully interactive regardless of which screen the button was clicked from; the test game opens in its own independent window.

**Files modified.**

| File | Change |
|---|---|
| `engine/src/engine/Engine.h` | Declared `spawnTestGameProcess(const std::string&)` |
| `engine/src/engine/Engine.cpp` | Implemented helper; includes `<filesystem>` and `<windows.h>` |
| `engine/src/ui/StartScreenEditor.cpp` | Test Game button calls `spawnTestGameProcess` instead of `enterTestMode` |
| `engine/src/ui/MusicSelectionEditor.cpp` | Same fix |
