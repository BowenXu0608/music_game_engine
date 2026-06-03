#pragma once
#include "JudgmentSystem.h"
#include <glm/glm.hpp>

struct JudgmentDisplay {
    Judgment type = Judgment::Miss;
    float lifetime = 0.f;
    glm::vec2 position{0.f};
    float laneX01     = 0.5f;   // normalized lane-center X (0..1), HUD/window space
    float hitLineY01  = 0.9f;   // normalized Y of the judgment line (0=top,1=bottom)
    float laneWidthPx = 120.f;  // on-screen width of one lane at the hit line (px)
    int   timingSign  = 0;      // +1 = early, -1 = late, 0 = none

    static constexpr float DURATION = 0.8f;

    void spawn(Judgment j, glm::vec2 pos) {
        type = j;
        position = pos;
        lifetime = DURATION;
    }

    // Richer spawn used by renderers that drive the floating judgment text.
    void spawn(Judgment j, float normLaneX, float hitY01, float laneW, int sign) {
        type = j;
        laneX01     = normLaneX;
        hitLineY01  = hitY01;
        laneWidthPx = laneW;
        timingSign  = sign;
        position    = {0.f, 0.f};
        lifetime    = DURATION;
    }

    void update(float dt) {
        if (lifetime > 0.f) lifetime -= dt;
    }

    bool isActive() const { return lifetime > 0.f; }

    float alpha() const {
        return glm::clamp(lifetime / DURATION, 0.f, 1.f);
    }

    glm::vec4 color() const {
        switch (type) {
            case Judgment::Perfect: return {0.2f, 1.f, 0.3f, alpha()};   // green
            case Judgment::Good:    return {0.3f, 0.6f, 1.f, alpha()};   // blue
            case Judgment::Bad:     return {1.f, 0.25f, 0.2f, alpha()};  // red
            case Judgment::Miss:    return {0.6f, 0.6f, 0.6f, alpha()};  // gray
        }
        return {1.f, 1.f, 1.f, alpha()};
    }

    const char* text() const {
        switch (type) {
            case Judgment::Perfect: return "PERFECT";
            case Judgment::Good:    return "GOOD";
            case Judgment::Bad:     return "BAD";
            case Judgment::Miss:    return "MISS";
        }
        return "";
    }
};
