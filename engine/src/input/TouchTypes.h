#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// ── Raw touch ────────────────────────────────────────────────────────────────

enum class TouchPhase : uint8_t {
    Began,       // finger down / mouse button down
    Moved,       // position changed while down
    Stationary,  // down but no movement this frame
    Ended,       // finger up / mouse button up
    Cancelled    // OS interrupted (call, notification, etc.)
};

struct TouchPoint {
    int32_t    id;         // platform touch ID; -1 for mouse simulation
    TouchPhase phase;
    glm::vec2  pos;        // screen pixels, origin top-left
    glm::vec2  deltaPos;   // movement since last event (zero on Began)
    double     timestamp;  // seconds, same clock as songTime
};

// ── Recognized gestures ──────────────────────────────────────────────────────

enum class GestureType : uint8_t {
    Tap,        // quick press + release, minimal movement
    Flick,      // fast swipe — check velocity for direction
    HoldBegin,  // touch held past TAP_MAX_DURATION_S without moving
    HoldEnd,    // held touch released
    SlideBegin, // movement exceeded SLIDE_SLOP_PX
    SlideMove,  // ongoing slide update (emitted every Moved while sliding)
    SlideEnd    // sliding touch released
};

struct GestureEvent {
    GestureType type;
    int32_t     touchId;   // which finger
    glm::vec2   pos;       // screen position at event time
    glm::vec2   startPos;  // where the gesture began
    glm::vec2   velocity;  // pixels/sec, meaningful for Flick and SlideMove
    double      timestamp;
    float       duration;  // seconds since touch began
};

// ── Tunable thresholds ───────────────────────────────────────────────────────

namespace TouchThresholds {
    inline constexpr float TAP_SLOP_PX        = 20.0f;   // max movement for a tap
    inline constexpr float TAP_MAX_DURATION_S = 0.15f;   // max duration before hold fires
    inline constexpr float FLICK_MIN_VELOCITY = 400.0f;  // px/s to classify as flick
    inline constexpr float SLIDE_SLOP_PX      = 25.0f;   // min movement to start a slide
    inline constexpr float VELOCITY_WINDOW_S  = 0.08f;   // window for velocity averaging
}
