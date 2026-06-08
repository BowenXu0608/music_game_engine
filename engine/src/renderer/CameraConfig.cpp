#include "renderer/CameraConfig.h"
#include "ui/ProjectHub.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

Camera buildGameplayCamera(const GameModeConfig& gm, float aspect) {
    // View: place the camera at cameraPosition with a Unity-style orientation
    // (looks down its local -Z, up is local +Y). Euler = (pitch, yaw, roll).
    glm::vec3 pos{gm.cameraPosition[0], gm.cameraPosition[1], gm.cameraPosition[2]};
    glm::quat rot{glm::radians(glm::vec3{gm.cameraRotationDeg[0],
                                         gm.cameraRotationDeg[1],
                                         gm.cameraRotationDeg[2]})};
    glm::mat4 world = glm::translate(glm::mat4(1.f), pos) * glm::mat4_cast(rot);
    glm::mat4 view  = glm::inverse(world);

    float nearZ = std::max(gm.cameraNearClip, 0.001f);
    float farZ  = std::max(gm.cameraFarClip, nearZ + 0.01f);

    Camera cam;
    if (gm.cameraProjection == CameraProjection::Orthographic) {
        float halfH = std::max(gm.cameraOrthoSize, 0.01f);
        float halfW = halfH * std::max(aspect, 0.01f);
        cam = Camera::makeOrtho(-halfW, halfW, -halfH, halfH, nearZ, farZ);
        // makeOrtho doesn't apply the Vulkan Y-flip that makePerspective does;
        // match it so both projection modes share one screen-Y convention.
        glm::mat4 proj = cam.projection();
        proj[1][1] *= -1.f;
        cam.setProj(proj);
    } else {
        cam = Camera::makePerspective(gm.cameraFovYDeg, aspect, nearZ, farZ);
    }
    cam.setView(view);
    return cam;
}
