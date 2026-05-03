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

## TouchThresholds DPI scaling (2026-05-03)

`engine/src/input/TouchTypes.h::TouchThresholds` had `TAP_SLOP_PX = 20.0f` and `SLIDE_SLOP_PX = 25.0f` declared `inline constexpr`. On a high-DPI Android device (e.g. 458 dpi → ~2.86 px/dp), 20 px reads as ~7 dp — well below the 44 dp accessibility minimum and a hair-trigger for drag detection.

The fix:

- Both `TAP_SLOP_PX` and `SLIDE_SLOP_PX` converted from `inline constexpr float` to `inline float` so they're runtime-mutable.
- New helper: `inline void TouchThresholds::scaleByDpi(float dpiScale)` multiplies them in-place; the input is clamped to `>= 1.0f` so call sites can pass 0 or negative without zeroing the slop.
- `AndroidEngine::onWindowInit` calls `TouchThresholds::scaleByDpi(dpiScale)` after the DPI is computed (`AConfiguration_getDensity → density / 160.0f`). The same `dpiScale` drives ImGui font + `ScaleAllSizes`.

The `*_S` (`TAP_MAX_DURATION_S`, `VELOCITY_WINDOW_S`) and `FLICK_MIN_VELOCITY` constants stay `constexpr` — they're seconds and pixels-per-second, dimensionally density-independent.

Desktop builds keep the 20/25 baseline (no `scaleByDpi` call — `dpiScale` is always 1.0 on desktop where the editor authors charts). On phones, the scale lands somewhere between 1.5 and 4.0 depending on the device class.
