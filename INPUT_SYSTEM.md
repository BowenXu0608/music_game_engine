# System 4 + System 5 — Input, Gesture & Gameplay

**Last updated:** 2026-04-03  
**Status:** ✅ Complete (updated with gameplay integration changes)

This document covers two tightly coupled systems:
- **System 4 — Input & Gesture:** raw touch → `InputManager` → `GestureRecognizer` → `GestureEvent`
- **System 5 — Gameplay:** `HitDetector` → `JudgmentSystem` → `ScoreTracker`

See also: [README.md](README.md) | [RENDERING_SYSTEM.md](RENDERING_SYSTEM.md) | [EDITOR_SYSTEM.md](EDITOR_SYSTEM.md)

---

## Overview

The Input System captures player input, detects note hits, judges timing accuracy, and tracks score/combo. It supports both keyboard (desktop) and multi-touch (mobile/tablet), which is the primary target platform.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                    Platform Layer                     │
│  Android JNI / iOS Bridge / GLFW Mouse (desktop)     │
└──────────────────┬───────────────────────────────────┘
                   ↓ TouchPoint (raw)
┌──────────────────────────────────────────────────────┐
│                  InputManager                         │
│  - injectTouch()  ← platform injection               │
│  - onMouseMove()  ← GLFW mouse simulation            │
│  - onKey()        ← keyboard (desktop lanes)         │
└──────────────────┬───────────────────────────────────┘
                   ↓ TouchPoint
┌──────────────────────────────────────────────────────┐
│               GestureRecognizer                       │
│  Per-finger state machine → GestureEvent             │
│  Tap / Flick / HoldBegin / HoldEnd / Slide*          │
└──────────────────┬───────────────────────────────────┘
                   ↓ GestureEvent
┌──────────────────────────────────────────────────────┐
│              Engine (gesture callback)                │
│  Mode dispatch: LaneBased / Arcaea / Phigros         │
└──────────────────┬───────────────────────────────────┘
                   ↓
┌──────────────────────────────────────────────────────┐
│                 HitDetector                           │
│  checkHit / checkHitPosition / checkHitPhigros       │
│  beginHold / endHold / updateSlide                   │
└──────────────────┬───────────────────────────────────┘
                   ↓ HitResult
┌──────────────────────────────────────────────────────┐
│               JudgmentSystem                          │
│  judge / judgeFlick / judgeSlide / judgeArc          │
└──────────────────┬───────────────────────────────────┘
                   ↓ Judgment
┌──────────────────────────────────────────────────────┐
│                ScoreTracker                           │
│  score / combo / maxCombo                            │
└──────────────────────────────────────────────────────┘
```

## Components

### 1. TouchTypes (`engine/src/input/TouchTypes.h`)
Pure data header. No dependencies beyond `<glm/glm.hpp>`.

**TouchPoint** — raw platform event:
- `int32_t id` — finger ID; `-1` for mouse simulation
- `TouchPhase phase` — Began / Moved / Stationary / Ended / Cancelled
- `glm::vec2 pos` — screen pixels, origin top-left
- `glm::vec2 deltaPos` — movement since last event
- `double timestamp` — seconds, same clock as songTime

**GestureEvent** — recognized gesture:
- `GestureType type` — Tap / Flick / HoldBegin / HoldEnd / SlideBegin / SlideMove / SlideEnd
- `int32_t touchId` — which finger
- `glm::vec2 pos` — screen position at event time
- `glm::vec2 startPos` — where the gesture began
- `glm::vec2 velocity` — pixels/sec (meaningful for Flick and SlideMove)
- `float duration` — seconds since touch began

**TouchThresholds** namespace (all tunable):
| Constant | Value | Meaning |
|---|---|---|
| `TAP_SLOP_PX` | 20 px | Max movement still considered a tap |
| `TAP_MAX_DURATION_S` | 0.15 s | Hold fires after this duration |
| `FLICK_MIN_VELOCITY` | 400 px/s | Min speed to classify as flick |
| `SLIDE_SLOP_PX` | 25 px | Min movement to start a slide |
| `VELOCITY_WINDOW_S` | 0.08 s | Window for velocity averaging |

---

### 2. GestureRecognizer (`engine/src/input/GestureRecognizer.h/.cpp`)
Converts raw `TouchPoint` events into `GestureEvent`s. Each finger runs an independent state machine tracked by touch ID.

**States per finger:**
```
PotentialTap ──(movement > SLIDE_SLOP_PX)──→ Sliding
PotentialTap ──(held > TAP_MAX_DURATION_S)──→ Holding
```

**State transitions and emitted events:**

| Trigger | From State | To State | Emits |
|---|---|---|---|
| TouchPhase::Began | — | PotentialTap | — |
| Moved, movement > SLIDE_SLOP_PX | PotentialTap or Holding | Sliding | SlideBegin |
| Moved | Sliding | Sliding | SlideMove |
| update(), held > TAP_MAX_DURATION_S | PotentialTap | Holding | HoldBegin |
| Ended | PotentialTap | — | Tap |
| Ended | Holding | — | HoldEnd |
| Ended, \|velocity\| > FLICK_MIN_VELOCITY | Sliding | — | Flick |
| Ended, \|velocity\| ≤ FLICK_MIN_VELOCITY | Sliding | — | SlideEnd |
| Cancelled | Holding | — | HoldEnd |
| Cancelled | Sliding | — | SlideEnd |

**Velocity calculation:** averaged over the last `VELOCITY_WINDOW_S` seconds using a ring buffer of up to 16 position samples. Avoids single-frame spikes.

**Edge cases handled:**
- Android touch ID reuse: `handleBegan` overwrites any stale entry for that ID
- Very fast tap (Began+Ended before `update()` runs): `handleEnded` sees `PotentialTap` and emits `Tap` directly
- `Cancelled` phase: emits appropriate end event and cleans up state

---

### 3. InputManager (`engine/src/input/InputManager.h`)
Header-only. Owns a `GestureRecognizer`. Does **not** call `glfwSetWindowUserPointer` — Engine owns the GLFW user pointer exclusively.

**Key methods:**
- `init()` — no-op; Engine registers all GLFW callbacks
- `injectTouch(int32_t id, TouchPhase, glm::vec2 pos, double timestamp)` — primary entry point for all touch input; called by Engine's static GLFW callbacks for mouse simulation, and directly by platform-native code for real multi-touch
- `onKey(int key, int action)` — forwarded from Engine's `keyCallback`; `keyToLane` maps keys `1`–`9`, `0`, `Q`, `W` to lanes 0–11 (supports up to 12 tracks)
- `onMouseMove(double x, double y, double timestamp)` — forwarded from Engine's `cursorPosCallback`; only forwards to gesture recognizer while mouse button is held
- `update(double currentTime)` — must be called each frame; fires hold timeouts in GestureRecognizer
- `setKeyCallback(fn)` — keyboard lane callback (backward compatible)
- `setGestureCallback(fn)` — gesture event callback

Mouse simulation uses touch ID `-1` (Android/iOS use non-negative IDs, so no collision).

---

### 4. HitDetector (`engine/src/gameplay/HitDetector.h/.cpp`)
Maintains the list of active notes and tests input events against them.

**Lane-based hit (Bandori, Cytus, Lanota):**
```cpp
std::optional<HitResult> checkHit(int lane, double songTime);
```
Matches `TapData`/`HoldData` notes by exact lane index within ±100ms timing window.

**Position-based hit (Arcaea ground taps):**
```cpp
std::optional<HitResult> checkHitPosition(glm::vec2 screenPos, glm::vec2 screenSize, double songTime);
```
Maps `laneX / 4.0 * screenWidth` to screen X, tests within `HIT_RADIUS_PX = 90px`.

**Phigros hit (rotating judgment lines):**
```cpp
std::optional<HitResult> checkHitPhigros(glm::vec2 screenPos, glm::vec2 lineOrigin, float lineRotation, double songTime);
```
Projects touch onto the line via dot product. Checks perpendicular distance < `HIT_RADIUS_PX` and along-line distance vs `posOnLine`.

**Miss detection:**
```cpp
struct MissedNote { uint32_t noteId; std::string noteType; int lane; };
std::vector<MissedNote> update(double songTime);
```
`update` now returns a `std::vector<MissedNote>` instead of void. Each missed note includes `noteId`, `noteType`, and `lane`. Engine dispatches `Judgment::Miss` for each returned entry and calls `showJudgment` on the active renderer.

**Initialization:**
```cpp
void init(const std::vector<NoteData>& notes);
```
`init` now also calls `m_activeHolds.clear()` to prevent stale hold state from a previous gameplay session.

**Hold tracking:**
```cpp
std::optional<uint32_t> beginHold(int lane, double songTime);
std::optional<uint32_t> beginHoldPosition(glm::vec2 screenPos, glm::vec2 screenSize, double songTime);
std::optional<HitResult> endHold(uint32_t noteId, double releaseTime);
void updateSlide(uint32_t noteId, glm::vec2 currentPos, double songTime);
float getSlideAccuracy(uint32_t noteId) const;
```
`beginHold` registers a note in `m_activeHolds` without removing it from `m_activeNotes`. `endHold` removes from both and returns a `HitResult` with `timingDelta = releaseTime - (noteStart + duration)`.

**Timing windows:**
| Judgment | Window |
|---|---|
| Perfect | ±20ms |
| Good | ±60ms |
| Bad | ±100ms |
| Miss | >100ms or note passed |

---

### 5. JudgmentSystem (`engine/src/gameplay/JudgmentSystem.h`)
Converts timing/accuracy data to a `Judgment` enum.

| Method | Used for |
|---|---|
| `judge(timingDelta)` | Tap |
| `judgeFlick(timingDelta, directionAccuracy)` | Flick (direction 0–1, min 0.7) |
| `judgeHold(pressTime, releaseTime, noteStart, duration)` | Hold |
| `judgeSlide(avgPositionError, completionRatio)` | Slide (min 80% completion) |
| `judgeArc(avgTrackingError, completionRatio)` | Arcaea arc (min 85% completion) |
| `judgeSkyNote(timingDelta)` | Arcaea sky tap (more lenient) |

---

### 6. ScoreTracker (`engine/src/gameplay/ScoreTracker.h`)
| Judgment | Points | Combo |
|---|---|---|
| Perfect | 1000 | +1 |
| Good | 500 | +1 |
| Bad | 100 | reset |
| Miss | 0 | reset |

Values are still hardcoded, but `GameModeConfig` now exposes user-configurable `perfectScore`, `goodScore`, `badScore` fields for future per-mode overrides.

### 7. JudgmentDisplay Colors

| Judgment | Color (RGB) |
|---|---|
| Perfect | green `{0.2, 1.0, 0.3}` |
| Good | blue `{0.3, 0.6, 1.0}` |
| Bad | red `{1.0, 0.25, 0.2}` |
| Miss | gray `{0.6, 0.6, 0.6}` |

---

## Engine Integration

Engine owns all 4 components and wires them together. It also owns the GLFW user pointer (fixing a prior bug where InputManager overwrote it).

**Initialization flow:**
1. `Engine::init()` → registers GLFW callbacks (key, mouse button, cursor pos, framebuffer resize) — all retrieve `Engine*` from user pointer
2. `m_input.init()` — no-op
3. `m_input.setKeyCallback(...)` — keyboard lane input
4. `m_input.setGestureCallback(...)` — touch/gesture input, dispatches to mode-specific handler

**Per-frame flow:**
```
glfwPollEvents()
  → keyCallback → m_input.onKey()
  → mouseButtonCallback → m_input.injectTouch(-1, Began/Ended, ...)
  → cursorPosCallback → m_input.onMouseMove()

Engine::update()
  → m_input.update(songTime)   ← fires hold timeouts
  → missedNotes = m_hitDetector.update(songTime)  ← returns missed notes
  → for each MissedNote: ScoreTracker::onJudgment(Miss), renderer->showJudgment(lane, Miss)

GestureRecognizer fires callback
  → Engine::handleGestureLaneBased / handleGestureArcaea / handleGesturePhigros
  → HitDetector::checkHit / checkHitPosition / checkHitPhigros
  → JudgmentSystem::judge*
  → ScoreTracker::onJudgment
  → renderer->showJudgment(lane, judgment)  ← via GameModeRenderer base class
```

**Mode dispatch in gesture callback:**
```cpp
if (dynamic_cast<ArcaeaRenderer*>)  → handleGestureArcaea
if (dynamic_cast<PhigrosRenderer*>) → handleGesturePhigros
else                                → handleGestureLaneBased  // Bandori, Cytus, Lanota
```

`handleGestureLaneBased` computes the lane from touch X using `m_gameplayConfig.trackCount` (dynamic, no longer hardcoded to 7). This allows any mode to define its own track count.

**showJudgment on base renderer:**
`GameModeRenderer` now provides `virtual void showJudgment(int lane, Judgment judgment) {}` as a default no-op. `Engine::dispatchHitResult` calls it on any active renderer (not just `BandoriRenderer`), so all game modes can display judgment feedback without special-casing.

**Hold tracking across frames:**
Engine maintains `std::unordered_map<int32_t, uint32_t> m_activeTouches` (touchId → noteId). `HoldBegin` inserts, `HoldEnd` retrieves and calls `endHold`.

---

## Platform Injection (Android / iOS)

For real multi-touch on device, call `inputManager().injectTouch()` directly from platform-native code:

**Android JNI example:**
```cpp
// In your Android native activity's touch handler:
extern "C" JNIEXPORT void JNICALL
Java_com_example_MusicGame_onTouch(JNIEnv*, jobject, jint id, jint phase, jfloat x, jfloat y, jdouble t) {
    engine->inputManager().injectTouch(id, static_cast<TouchPhase>(phase), {x, y}, t);
}
```

**iOS Obj-C bridge example:**
```objc
// In your UIView touchesBegan/Moved/Ended:
for (UITouch* touch in touches) {
    CGPoint pt = [touch locationInView:self];
    int32_t touchId = (int32_t)(uintptr_t)touch;
    engine->inputManager().injectTouch(touchId, TouchPhase::Began, {pt.x, pt.y}, touch.timestamp);
}
```

Touch IDs from both platforms are non-negative, so they never collide with the mouse simulation ID (`-1`).

---

## Files

| File | Purpose |
|---|---|
| `engine/src/input/TouchTypes.h` | TouchPoint, GestureEvent, TouchThresholds |
| `engine/src/input/GestureRecognizer.h/.cpp` | Per-finger state machine |
| `engine/src/input/InputManager.h` | Aggregates keyboard + touch, platform injection |
| `engine/src/gameplay/HitDetector.h/.cpp` | Note hit detection (lane + position + hold) |
| `engine/src/gameplay/JudgmentSystem.h` | Timing → Perfect/Good/Bad/Miss |
| `engine/src/gameplay/ScoreTracker.h` | Score and combo tracking |
| `engine/src/engine/Engine.h/.cpp` | Wires everything, owns GLFW callbacks |
| `engine/src/gamemodes/GameModeRenderer.h` | Base class with virtual `showJudgment` |

## Implementation Status

- [x] TouchTypes — raw touch + gesture data structures
- [x] GestureRecognizer — Tap, Flick, Hold, Slide state machine
- [x] InputManager — keyboard + touch + platform injection
- [x] HitDetector — lane-based, position-based (Arcaea), Phigros line projection, hold tracking
- [x] JudgmentSystem — all note types
- [x] ScoreTracker — score/combo
- [x] Engine integration — GLFW callbacks, mode dispatch, hold tracking map
- [ ] HUD rendering — score/combo display on screen
- [x] Visual hit effects — judgment text with color-coded display (Perfect/Good/Bad/Miss)
- [ ] Configurable timing windows
- [ ] Replay system
- [ ] Auto-play mode
