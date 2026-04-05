# System 4 + System 5 — Input & Gameplay (User Guide)

**Last updated:** 2026-04-03  
**Status:** ✅ Complete

> For full technical detail see [INPUT_SYSTEM.md](INPUT_SYSTEM.md). This file is the user-facing guide.

A multi-touch input pipeline for the Music Game Engine, targeting Android and iOS. Supports Tap, Flick, Hold, and Slide gestures across all 5 game modes. Desktop mouse input is automatically simulated as a single touch for development.

---

## How it works

Raw touch events flow through four layers before producing a judgment:

```
Platform touch / mouse
        ↓
  InputManager.injectTouch()
        ↓
  GestureRecognizer  →  GestureEvent (Tap / Flick / Hold / Slide)
        ↓
  Engine gesture callback  →  mode-specific dispatch
        ↓
  HitDetector  →  HitResult (noteId, timingDelta)
        ↓
  JudgmentSystem  →  Perfect / Good / Bad / Miss
        ↓
  ScoreTracker
```

---

## Gesture recognition

Each finger runs an independent state machine inside `GestureRecognizer`. The state machine has three states:

```
PotentialTap ──(movement > 25px)──────────────→ Sliding
PotentialTap ──(held > 0.15s, no movement)──→ Holding
```

What gets emitted at each transition:

| Gesture | When |
|---|---|
| `Tap` | Released from PotentialTap |
| `Flick` | Released from Sliding with speed > 400 px/s |
| `HoldBegin` | Held > 0.15s without moving |
| `HoldEnd` | Released from Holding |
| `SlideBegin` | Movement crossed 25px threshold |
| `SlideMove` | Every move event while Sliding |
| `SlideEnd` | Released from Sliding (below flick speed) |

Velocity is averaged over the last 80ms of samples (ring buffer, max 16 entries) to avoid single-frame spikes.

---

## Hit detection

The `HitDetector` has three hit-test modes depending on the active game mode:

**Lane-based** (Bandori, Cytus, Lanota)
- Touch X position is divided into 7 equal columns → lane index 0–6
- `checkHit(lane, songTime)` matches notes by exact lane within ±100ms

**Position-based** (Arcaea)
- `checkHitPosition(screenPos, screenSize, songTime)` maps `laneX` to screen X and tests within a 90px radius

**Line projection** (Phigros)
- `checkHitPhigros(screenPos, lineOrigin, lineRotation, songTime)` projects the touch onto the rotating judgment line using dot product, checks perpendicular distance < 90px and along-line distance vs `posOnLine`

Hold notes are tracked separately. `beginHold` registers the note without consuming it; `endHold` finalises it and returns a `HitResult` with the release timing error.

---

## Timing windows

| Judgment | Window |
|---|---|
| Perfect | ±20ms |
| Good | ±60ms |
| Bad | ±100ms |
| Miss | > 100ms or note passed |

Flick judgment also requires direction accuracy ≥ 0.7 (dot product of swipe velocity vs expected direction). Slide and Arc judgment use average position error + completion ratio.

---

## Desktop mouse simulation

On desktop, the left mouse button is automatically treated as a single touch (ID = `-1`). The full gesture pipeline runs identically — you can test Tap, Hold, and Slide with the mouse. Flick requires a fast drag-and-release.

No code changes are needed to switch between mouse and real touch. The same `injectTouch()` path handles both.

---

## Adding touch to a new platform

Call `engine.inputManager().injectTouch()` from your platform-native code:

```cpp
// Signature
void InputManager::injectTouch(int32_t id, TouchPhase phase, glm::vec2 pos, double timestamp);
```

`id` is the platform's finger identifier (non-negative on Android and iOS — no collision with the mouse simulation ID of `-1`).

**Android JNI:**
```cpp
extern "C" JNIEXPORT void JNICALL
Java_com_example_MusicGame_onTouch(JNIEnv*, jobject,
    jint id, jint phase, jfloat x, jfloat y, jdouble t)
{
    engine->inputManager().injectTouch(id, static_cast<TouchPhase>(phase), {x, y}, t);
}
```

**iOS (UIView):**
```objc
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    for (UITouch* touch in touches) {
        CGPoint pt = [touch locationInView:self];
        int32_t tid = (int32_t)(uintptr_t)touch;
        engine->inputManager().injectTouch(tid, TouchPhase::Began, {pt.x, pt.y}, touch.timestamp);
    }
}
```

The same pattern applies for `touchesMoved`, `touchesEnded`, and `touchesCancelled` using `TouchPhase::Moved`, `Ended`, and `Cancelled` respectively.

---

## Tuning gesture thresholds

All thresholds are `constexpr` in `TouchThresholds` namespace inside `TouchTypes.h`:

```cpp
namespace TouchThresholds {
    inline constexpr float TAP_SLOP_PX        = 20.0f;   // max drift for a tap
    inline constexpr float TAP_MAX_DURATION_S = 0.15f;   // hold fires after this
    inline constexpr float FLICK_MIN_VELOCITY = 400.0f;  // px/s to classify as flick
    inline constexpr float SLIDE_SLOP_PX      = 25.0f;   // movement to start a slide
    inline constexpr float VELOCITY_WINDOW_S  = 0.08f;   // velocity averaging window
}
```

On high-DPI screens (e.g. 3x scale), multiply the pixel thresholds by the device's screen scale factor before comparing, or convert touch positions to logical pixels before calling `injectTouch`.

---

## File reference

| File | What it does |
|---|---|
| `engine/src/input/TouchTypes.h` | Data structs: `TouchPoint`, `GestureEvent`, `TouchThresholds` |
| `engine/src/input/GestureRecognizer.h/.cpp` | Per-finger state machine |
| `engine/src/input/InputManager.h` | Keyboard + touch aggregator, platform injection point |
| `engine/src/gameplay/HitDetector.h/.cpp` | Note hit tests (lane, position, Phigros line, hold) |
| `engine/src/gameplay/JudgmentSystem.h` | Timing → judgment grade |
| `engine/src/gameplay/ScoreTracker.h` | Score and combo |
| `engine/src/engine/Engine.h/.cpp` | GLFW callbacks, gesture dispatch, hold tracking |
