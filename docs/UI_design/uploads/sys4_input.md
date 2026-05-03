---
name: Input & Gesture System
description: InputManager, GestureRecognizer, ScreenMetrics DPI, keyboard mapping, touch platform support
type: project
originSessionId: d4e6dddd-1cc1-4f7b-8da6-079be9eb81c0
---
# System 4 — Input & Gesture ✅ COMPLETE

**Files:** `engine/src/input/`

## Components

| Component | Purpose |
|---|---|
| TouchTypes.h | Raw TouchPoint + GestureEvent data types |
| GestureRecognizer | Per-finger state machine: Tap/Flick/Hold/Slide |
| InputManager.h | Keyboard + touch aggregator, platform injection |
| ScreenMetrics.h | DPI-aware `dp()` function for density-independent sizing |

## Keyboard Mapping

Keys 1-9, 0, Q, W -> lanes 0-11 (up to 12 tracks). Keyboard callback in Engine.cpp:
- Press: tries `beginHold` first (for hold heads), then `checkHit` (for taps/flicks). Also calls `updateHoldLane` on all active keyboard holds for cross-lane tracking.
- Release: calls `endHold` for the held lane.

## Touch / Gesture

**Platform support:** GLFW mouse simulation (desktop), Android JNI, iOS UITouch.
**Mouse simulation** uses touch ID -1 (never collides with real IDs).

**GestureRecognizer state machine:**
- Idle -> TouchDown -> (hold threshold) -> Holding
- Holding -> (movement threshold) -> Sliding
- TouchDown -> (quick release) -> Tap
- TouchDown -> (velocity threshold) -> Flick
- Emits: `GestureType::{Tap, Flick, HoldBegin, HoldEnd, SlideBegin, SlideMove, SlideEnd}`

## DPI Scaling (ScreenMetrics)

`ScreenMetrics::dp(float dpValue)` converts density-independent pixels to screen pixels. Reference density: 160 DPI. All touch thresholds and hit radii use `dp()`:
- Pick tolerance: `dp(48.f)` (~7.6mm fingertip radius) for Lanota + Cytus
- Slide tick tolerance: `dp(64.f)` for scan-line slide scoring
- Hit radius: `HitDetector::HIT_RADIUS_PX = 90.0f` for Arcaea position-based

Mobile layout: BanG Dream uses ultrawide layout (50% highway, raised camera, larger notes).
