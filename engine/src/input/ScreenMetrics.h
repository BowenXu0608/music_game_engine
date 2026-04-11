// ============================================================================
// ScreenMetrics — DPI-aware pixel scaling
//
// Problem: hardcoded pixel thresholds (TAP_SLOP_PX=20, HIT_RADIUS_PX=90)
// represent wildly different physical sizes across screen densities:
//   720p  5.5" phone (267 DPI): 20px = 1.9mm, 90px = 8.6mm
//   1440p 6.7" phone (480 DPI): 20px = 1.1mm, 90px = 4.8mm
//   4K    11"  tablet (400 DPI): 20px = 1.3mm, 90px = 5.7mm
//
// A fingertip covers ~7-10mm. On high-DPI screens, the thresholds become
// smaller than a finger can control — slides misfire as taps, hit circles
// are too small, flick velocity thresholds are unreachable.
//
// Solution: define thresholds in density-independent pixels (dp) at a
// reference of 160 DPI (Android's baseline), then scale to actual screen
// pixels at runtime using dp().
//
// At 160 DPI: 1 dp = 1 px.  At 320 DPI: 1 dp = 2 px.
// ============================================================================
#pragma once
#include <algorithm>
#include <cmath>

class ScreenMetrics {
public:
    // Call once at startup or when screen changes.
    // screenW/H: resolution in pixels.
    // physicalDiag: physical diagonal in inches (0 = auto-estimate from resolution).
    static void init(int screenW, int screenH, float physicalDiagInches = 0.f) {
        s_screenW = screenW;
        s_screenH = screenH;

        if (physicalDiagInches > 0.f) {
            float diagPx = std::sqrt((float)(screenW * screenW + screenH * screenH));
            s_dpi = diagPx / physicalDiagInches;
        } else {
            // Estimate: assume ~6.5" phone for mobile resolutions, ~24" for desktop
            float diagPx = std::sqrt((float)(screenW * screenW + screenH * screenH));
            float estDiag = (screenW <= 2560) ? 6.5f : 24.f;
            s_dpi = diagPx / estDiag;
        }

        s_scale = s_dpi / REFERENCE_DPI;
        s_scale = std::clamp(s_scale, 0.5f, 4.0f);  // sanity bounds
    }

    // Convert density-independent pixels to actual screen pixels.
    // dp values represent physical size: dp(10) ≈ 1.6mm on any screen.
    static float dp(float dpValue) { return dpValue * s_scale; }

    // Convert screen pixels to dp.
    static float pxToDp(float px) { return px / s_scale; }

    // Current scale factor (DPI / 160).
    static float scale()   { return s_scale; }
    static float dpi()     { return s_dpi; }
    static int   screenW() { return s_screenW; }
    static int   screenH() { return s_screenH; }

    // Screen aspect ratio (width / height).
    static float aspect() {
        return s_screenH > 0 ? (float)s_screenW / s_screenH : 1.f;
    }

    // True if the screen is ultra-wide (>= 2.0 aspect, e.g., 20:9 phones).
    static bool isUltraWide() { return aspect() >= 2.0f; }

private:
    static constexpr float REFERENCE_DPI = 160.f;  // Android baseline

    static inline int   s_screenW = 1280;
    static inline int   s_screenH = 720;
    static inline float s_dpi     = 160.f;
    static inline float s_scale   = 1.0f;
};
