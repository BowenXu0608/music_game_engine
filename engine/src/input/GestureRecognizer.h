#pragma once
#include "TouchTypes.h"
#include <functional>
#include <unordered_map>
#include <deque>

class GestureRecognizer {
public:
    using GestureCallback = std::function<void(const GestureEvent&)>;

    void setCallback(GestureCallback cb) { m_callback = std::move(cb); }

    // Feed a raw touch event from InputManager
    void onTouch(const TouchPoint& touch);

    // Call once per frame from Engine::update() to fire hold timeouts
    void update(double currentTime);

private:
    enum class TouchState : uint8_t { PotentialTap, Holding, Sliding };

    struct Sample { glm::vec2 pos; double time; };

    struct TrackedTouch {
        TouchState state     = TouchState::PotentialTap;
        glm::vec2  startPos  {};
        glm::vec2  lastPos   {};
        double     startTime = 0.0;
        double     lastTime  = 0.0;
        std::deque<Sample> recentSamples; // capped at 16 for velocity averaging
    };

    void handleBegan    (const TouchPoint& touch);
    void handleMoved    (const TouchPoint& touch);
    void handleEnded    (const TouchPoint& touch);
    void handleCancelled(const TouchPoint& touch);

    glm::vec2 computeVelocity(const TrackedTouch& t) const;
    void emit(const GestureEvent& evt) { if (m_callback) m_callback(evt); }

    std::unordered_map<int32_t, TrackedTouch> m_touches;
    GestureCallback m_callback;
};
