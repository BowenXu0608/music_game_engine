---
name: Gameplay System
description: HitDetector, JudgmentSystem, ScoreTracker, cross-lane holds, drag notes, sample-tick scoring — all gameplay mechanics
type: project
originSessionId: d4e6dddd-1cc1-4f7b-8da6-079be9eb81c0
---
# System 5 — Gameplay ✅ COMPLETE

**Files:** `engine/src/gameplay/`

## Components

| Component | Purpose |
|---|---|
| HitDetector | Lane/position/id-based hit detection + miss detection + hold tracking |
| JudgmentSystem.h | Timing -> Perfect / Good / Bad / Miss, all note types |
| ScoreTracker.h | Score + combo + maxCombo tracking |

## HitDetector

**Hit methods:**
- `checkHit(lane, songTime)` — lane-based (Bandori, Lanota keyboard). Handles TapData, HoldData, FlickData, LanotaRingData (via `angleToLane`).
- `consumeNoteById(noteId, songTime)` — id-based (Lanota touch, Cytus touch). Caller picks the note geometrically; detector validates timing.
- `checkHitPosition(screenPos, screenSize, songTime)` — Arcaea ground taps.
- `checkHitPhigros(screenPos, lineOrigin, lineRotation, songTime)` — Phigros judgment line projection.
- `consumeDrags(lane, songTime)` — consumes all Drag notes at a lane within +/-0.15s. Auto-hit on any touch.

**Hold lifecycle:**
- `beginHold(lane, songTime)` — lane-based. Erases from m_activeNotes, returns note ID.
- `beginHoldById(noteId, songTime)` — id-based. Returns `HitResult` with timingDelta. Accepts both HoldData and TapData (for NoteType::Slide).
- `beginHoldPosition(screenPos, screenSize, songTime)` — Arcaea arc holds.
- `endHold(noteId, releaseTime)` — finalizes hold, returns HitResult.
- All three beginHold* methods erase from m_activeNotes (prevents spurious Miss).

**Hold sample-tick scoring (Bandori-style cross-lane):**
- `consumeSampleTicks(songTime)` — fires at each authored sample point. Compares `evalHoldLaneAt` expected lane against `currentLane` (set by `updateHoldLane`). Match = Perfect, mismatch = Miss. Two consecutive misses = broken hold.
- `consumeBrokenHolds()` — returns IDs of broken holds for touch cleanup.

**ActiveHold struct (public):** `noteId, startTime, noteStartTime, noteDuration, noteType, lane, currentLane, consecutiveMissedTicks, broken, holdData, positionSamples, sampleOffsets, nextSampleIdx`.
- `getActiveHold(noteId)` — read-only access for slide tick scoring.
- `activeHoldIds()` — returns a `std::vector<uint32_t>` of every currently-active hold. Engine pushes this to `GameModeRenderer::setActiveHoldIds()` each frame so renderers can bloom/brighten hold bodies while they're being held (2026-04-12).

## Auto Play (2026-04-12)

Watch-the-chart mode — the game auto-triggers Perfect hits on every note. Toggle per-launch on the Music Selection screen (AUTO PLAY: ON/OFF button below START). Flag flows `MusicSelectionEditor::m_autoPlay` → `Engine::launchGameplay(..., bool autoPlay)` → `Engine::m_autoPlay`.

**HitDetector::autoPlayTick(songTime)** — returns `std::vector<AutoHit>` where `AutoHit { HitResult result, int lane, bool isHoldEnd }`. For every note whose `time <= songTime`:
- Tap/Flick/Drag/Ring/Phigros: emit `HitResult{id, 0.f, type}` and erase.
- Hold/Slide/Arc: push head hit, move the note into `m_activeHolds`, and keep syncing `currentLane = evalHoldLaneAt(holdData, tOff)` so `consumeSampleTicks` scores every tick Perfect.
- Holds whose `noteStartTime + noteDuration` has elapsed emit a release `HitResult` with `isHoldEnd=true` and are erased from `m_activeHolds`.

**Engine::update:** when `m_autoPlay` is true, calls `autoPlayTick` and dispatches every result through the normal `dispatchHitResult` path **before** the miss-sweep, so score / combo / HUD / particle bursts behave identically to a human hitting Perfect.

**Miss detection:** `update(songTime)` removes expired notes (>0.1s past), returns `vector<MissedNote>`. Extracts lane from all data variants including LanotaRingData.

**Lane rounding:** All `laneX` float-to-int conversions use `std::lround` (not truncation).

**Track count:** `setTrackCount(int)` for LanotaRingData angle-to-lane mapping. Set from `GameModeConfig::trackCount` in `Engine::setMode`.

## Cross-Lane Holds (Bandori-style, 2026-04-10)

Multi-waypoint hold path with drag-recorded waypoints + per-segment transition styles (Straight, Angle90, Curve, Rhomboid). Data model in `ChartTypes.h`:
- `HoldWaypoint { tOffset, lane, transitionLen, style }`
- `HoldSamplePoint { tOffset }` — authored tick checkpoints
- `evalHoldLaneAt(hold, tOffset)` — inline helper, walks waypoints or falls back to legacy single-transition
- `holdActiveSegment(hold, tOffset)` — returns active waypoint segment index

Input gating: `updateHoldLane` on every `SlideMove` gesture. Sample ticks gate on whether touch lane matches expected lane. Two consecutive misses break the hold.

## Judgment & Score

- **Timing windows:** Perfect +/-20ms, Good +/-60ms, Bad +/-100ms, Miss >100ms
- **Score config:** `GameModeConfig` has `perfectScore/goodScore/badScore` (for future per-mode overrides)
- **Combo:** ScoreTracker tracks combo + maxCombo, resets on Miss
- **HUD:** `HudTextConfig` for score/combo position, font, color, glow. Rendered via `Engine::renderGameplayHUD` using `ImGui::GetForegroundDrawList`

## Engine Gesture Dispatch

| Mode | Handler | Input path |
|---|---|---|
| 2D Drop (Bandori) | `handleGestureLaneBased` | lane from screen X -> checkHit/beginHold + consumeDrags |
| 3D Drop (Arcaea) | `handleGestureArcaea` | checkHitPosition/beginHoldPosition + updateSlide |
| Phigros | `handleGesturePhigros` | checkHitPhigros on each active judgment line |
| Circle (Lanota) | `handleGestureCircle` | pickNoteAt -> consumeNoteById/beginHoldById |
| Scan Line (Cytus) | `handleGestureScanLine` | pickNoteAt -> consumeNoteById/beginHoldById + updateSlide |

All hold gesture handlers dispatch head judgments. Flick handler calls `showJudgment` and falls back to tap for non-flick notes.

## Audio Lead-in

2-second visual lead-in before audio starts. `loadAudio` returns `bool`; `m_audioStarted` only set on success. `audioOffset` configurable per song.
