#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

class Camera {
public:
    static Camera makeOrtho(float left, float right, float bottom, float top,
                            float nearZ = -1.f, float farZ = 1.f) {
        Camera c;
        c.m_proj = glm::ortho(left, right, bottom, top, nearZ, farZ);
        c.m_view = glm::mat4(1.f);
        c.m_isPerspective = false;
        c.m_nearZ = nearZ;
        c.m_farZ  = farZ;
        return c;
    }

    static Camera makePerspective(float fovYDeg, float aspect,
                                  float nearZ = 0.1f, float farZ = 1000.f) {
        Camera c;
        c.m_proj = glm::perspective(glm::radians(fovYDeg), aspect, nearZ, farZ);
        // Flip Y for Vulkan NDC
        c.m_proj[1][1] *= -1.f;
        c.m_view = glm::mat4(1.f);
        c.m_isPerspective = true;
        c.m_nearZ = nearZ;
        c.m_farZ  = farZ;
        return c;
    }

    void lookAt(glm::vec3 eye, glm::vec3 center, glm::vec3 up = {0,1,0}) {
        m_view = glm::lookAt(eye, center, up);
    }

    void setView(const glm::mat4& v) { m_view = v; }
    void setProj(const glm::mat4& p) { m_proj = p; }

    glm::mat4 view()           const { return m_view; }
    glm::mat4 projection()     const { return m_proj; }
    glm::mat4 viewProjection() const { return m_proj * m_view; }

    bool isPerspective() const { return m_isPerspective; }

    Ray unproject(glm::vec2 screenPos, glm::vec2 screenSize) const {
        // NDC [-1,1]
        float ndcX = (screenPos.x / screenSize.x) * 2.f - 1.f;
        float ndcY = 1.f - (screenPos.y / screenSize.y) * 2.f;

        glm::mat4 invVP = glm::inverse(viewProjection());
        glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, 0.f, 1.f);
        glm::vec4 farPt  = invVP * glm::vec4(ndcX, ndcY, 1.f, 1.f);
        nearPt /= nearPt.w;
        farPt  /= farPt.w;

        Ray ray;
        ray.origin    = glm::vec3(nearPt);
        ray.direction = glm::normalize(glm::vec3(farPt) - ray.origin);
        return ray;
    }

private:
    glm::mat4 m_view{1.f};
    glm::mat4 m_proj{1.f};
    bool      m_isPerspective = false;
    float     m_nearZ = 0.f, m_farZ = 1.f;
};
