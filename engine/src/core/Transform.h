#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct Transform {
    glm::vec3 position{0.f};
    glm::quat rotation{glm::identity<glm::quat>()};
    glm::vec3 scale{1.f};

    glm::mat4 toMatrix() const {
        glm::mat4 t = glm::translate(glm::mat4(1.f), position);
        glm::mat4 r = glm::mat4_cast(rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.f), scale);
        return t * r * s;
    }

    // 2D helpers
    void setRotationZ(float radians) {
        rotation = glm::angleAxis(radians, glm::vec3(0.f, 0.f, 1.f));
    }
    float getRotationZ() const {
        return glm::eulerAngles(rotation).z;
    }
};
