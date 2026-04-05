#pragma once
#include "TouchTypes.h"
#include "GestureRecognizer.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <unordered_map>

// InputManager no longer sets glfwSetWindowUserPointer or glfwSetKeyCallback.
// Engine owns all GLFW callbacks and forwards events here.
class InputManager {
public:
    void init() {
        // Nothing to do — Engine registers GLFW callbacks and calls our methods directly
    }

    // Called by Engine::keyCallback
    void onKey(int key, int action) {
        if (action != GLFW_PRESS && action != GLFW_RELEASE) return;
        int lane = keyToLane(key);
        if (lane >= 0 && m_keyCallback)
            m_keyCallback(lane, action == GLFW_PRESS);
    }

    // Called by Engine::cursorPosCallback — only forwards while mouse is held
    void onMouseMove(double x, double y, double timestamp) {
        if (!m_mouseDown) return;
        injectTouch(-1, TouchPhase::Moved, {static_cast<float>(x), static_cast<float>(y)}, timestamp);
    }

    // Called once per frame from Engine::update()
    void update(double currentTime) {
        m_gesture.update(currentTime);
    }

    // Platform injection: call from Android JNI / iOS bridge for real multi-touch.
    // Also used internally for mouse simulation (id = -1).
    void injectTouch(int32_t id, TouchPhase phase, glm::vec2 pos, double timestamp) {
        if (phase == TouchPhase::Began)  m_mouseDown = (id == -1);
        if (phase == TouchPhase::Ended || phase == TouchPhase::Cancelled)
            if (id == -1) m_mouseDown = false;

        TouchPoint tp;
        tp.id        = id;
        tp.phase     = phase;
        tp.pos       = pos;
        tp.deltaPos  = glm::vec2{0.f}; // GestureRecognizer computes delta internally
        tp.timestamp = timestamp;
        m_gesture.onTouch(tp);
    }

    void setKeyCallback(std::function<void(int lane, bool pressed)> cb) {
        m_keyCallback = std::move(cb);
    }

    void setGestureCallback(GestureRecognizer::GestureCallback cb) {
        m_gesture.setCallback(std::move(cb));
    }

    // Backward-compatible alias
    void setCallback(std::function<void(int lane, bool pressed)> cb) {
        setKeyCallback(std::move(cb));
    }

private:
    int keyToLane(int key) {
        static const std::unordered_map<int, int> keyMap = {
            {GLFW_KEY_1, 0}, {GLFW_KEY_2, 1}, {GLFW_KEY_3, 2}, {GLFW_KEY_4, 3},
            {GLFW_KEY_5, 4}, {GLFW_KEY_6, 5}, {GLFW_KEY_7, 6}, {GLFW_KEY_8, 7},
            {GLFW_KEY_9, 8}, {GLFW_KEY_0, 9},
            {GLFW_KEY_Q, 10}, {GLFW_KEY_W, 11}
        };
        auto it = keyMap.find(key);
        return it != keyMap.end() ? it->second : -1;
    }

    GestureRecognizer                    m_gesture;
    std::function<void(int, bool)>       m_keyCallback;
    bool                                 m_mouseDown = false;
};
