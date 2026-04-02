#pragma once
#include "JudgmentSystem.h"
#include <glm/glm.hpp>

struct JudgmentDisplay {
    Judgment type = Judgment::Miss;
    float lifetime = 0.f;
    glm::vec2 position{0.f};

    static constexpr float DURATION = 0.8f;

    void spawn(Judgment j, glm::vec2 pos) {
        type = j;
        position = pos;
        lifetime = DURATION;
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
            case Judgment::Perfect: return {1.f, 0.9f, 0.2f, alpha()};
            case Judgment::Good:    return {0.2f, 1.f, 0.3f, alpha()};
            case Judgment::Bad:     return {1.f, 0.5f, 0.2f, alpha()};
            case Judgment::Miss:    return {1.f, 0.2f, 0.2f, alpha()};
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
