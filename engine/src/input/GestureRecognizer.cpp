#include "GestureRecognizer.h"
#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>

void GestureRecognizer::onTouch(const TouchPoint& touch) {
    switch (touch.phase) {
        case TouchPhase::Began:      handleBegan(touch);     break;
        case TouchPhase::Moved:      handleMoved(touch);     break;
        case TouchPhase::Stationary: handleMoved(touch);     break;
        case TouchPhase::Ended:      handleEnded(touch);     break;
        case TouchPhase::Cancelled:  handleCancelled(touch); break;
    }
}

void GestureRecognizer::update(double currentTime) {
    for (auto& [id, t] : m_touches) {
        if (t.state != TouchState::PotentialTap) continue;

        float movement = glm::length(t.lastPos - t.startPos);
        float elapsed  = static_cast<float>(currentTime - t.startTime);

        if (elapsed > TouchThresholds::TAP_MAX_DURATION_S &&
            movement < TouchThresholds::TAP_SLOP_PX)
        {
            t.state = TouchState::Holding;
            emit({GestureType::HoldBegin, id, t.lastPos, t.startPos,
                  glm::vec2{0.f}, currentTime,
                  static_cast<float>(currentTime - t.startTime)});
        }
    }
}

void GestureRecognizer::handleBegan(const TouchPoint& touch) {
    // Android can reuse IDs — overwrite any stale entry
    TrackedTouch t;
    t.state     = TouchState::PotentialTap;
    t.startPos  = touch.pos;
    t.lastPos   = touch.pos;
    t.startTime = touch.timestamp;
    t.lastTime  = touch.timestamp;
    t.recentSamples.push_back({touch.pos, touch.timestamp});
    m_touches[touch.id] = std::move(t);
}

void GestureRecognizer::handleMoved(const TouchPoint& touch) {
    auto it = m_touches.find(touch.id);
    if (it == m_touches.end()) return;
    auto& t = it->second;

    // Maintain velocity sample ring buffer
    t.recentSamples.push_back({touch.pos, touch.timestamp});
    if (t.recentSamples.size() > 16)
        t.recentSamples.pop_front();
    // Evict samples outside the velocity window
    while (t.recentSamples.size() > 2 &&
           touch.timestamp - t.recentSamples.front().time > TouchThresholds::VELOCITY_WINDOW_S)
        t.recentSamples.pop_front();

    float movement = glm::length(touch.pos - t.startPos);

    if (t.state == TouchState::PotentialTap || t.state == TouchState::Holding) {
        if (movement >= TouchThresholds::SLIDE_SLOP_PX) {
            t.state = TouchState::Sliding;
            emit({GestureType::SlideBegin, touch.id, touch.pos, t.startPos,
                  computeVelocity(t), touch.timestamp,
                  static_cast<float>(touch.timestamp - t.startTime)});
        }
    } else if (t.state == TouchState::Sliding) {
        emit({GestureType::SlideMove, touch.id, touch.pos, t.startPos,
              computeVelocity(t), touch.timestamp,
              static_cast<float>(touch.timestamp - t.startTime)});
    }

    t.lastPos  = touch.pos;
    t.lastTime = touch.timestamp;
}

void GestureRecognizer::handleEnded(const TouchPoint& touch) {
    auto it = m_touches.find(touch.id);
    if (it == m_touches.end()) return;
    auto& t = it->second;

    float duration = static_cast<float>(touch.timestamp - t.startTime);
    glm::vec2 vel  = computeVelocity(t);

    switch (t.state) {
        case TouchState::PotentialTap:
            emit({GestureType::Tap, touch.id, touch.pos, t.startPos,
                  glm::vec2{0.f}, touch.timestamp, duration});
            break;
        case TouchState::Holding:
            emit({GestureType::HoldEnd, touch.id, touch.pos, t.startPos,
                  glm::vec2{0.f}, touch.timestamp, duration});
            break;
        case TouchState::Sliding:
            if (glm::length(vel) >= TouchThresholds::FLICK_MIN_VELOCITY)
                emit({GestureType::Flick, touch.id, touch.pos, t.startPos,
                      vel, touch.timestamp, duration});
            else
                emit({GestureType::SlideEnd, touch.id, touch.pos, t.startPos,
                      vel, touch.timestamp, duration});
            break;
    }

    m_touches.erase(it);
}

void GestureRecognizer::handleCancelled(const TouchPoint& touch) {
    auto it = m_touches.find(touch.id);
    if (it == m_touches.end()) return;
    auto& t = it->second;

    float duration = static_cast<float>(touch.timestamp - t.startTime);
    if (t.state == TouchState::Holding)
        emit({GestureType::HoldEnd, touch.id, touch.pos, t.startPos,
              glm::vec2{0.f}, touch.timestamp, duration});
    else if (t.state == TouchState::Sliding)
        emit({GestureType::SlideEnd, touch.id, touch.pos, t.startPos,
              computeVelocity(t), touch.timestamp, duration});

    m_touches.erase(it);
}

glm::vec2 GestureRecognizer::computeVelocity(const TrackedTouch& t) const {
    if (t.recentSamples.size() < 2) return glm::vec2{0.f};

    glm::vec2 sum{0.f};
    int count = 0;
    for (size_t i = 1; i < t.recentSamples.size(); ++i) {
        double dt = t.recentSamples[i].time - t.recentSamples[i - 1].time;
        if (dt > 0.0) {
            sum += (t.recentSamples[i].pos - t.recentSamples[i - 1].pos) /
                   static_cast<float>(dt);
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<float>(count) : glm::vec2{0.f};
}
