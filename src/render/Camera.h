#pragma once

#include <glm/glm.hpp>

namespace ot {

class Camera {
public:
    glm::vec3 position{0.0f, 1.7f, 0.0f};
    float yaw = 0.0f;    // radians, rotation around Y
    float pitch = 0.0f;  // radians, positive = looking up
    float fov = glm::radians(70.0f);
    float aspect = 1.0f;
    float zNear = 0.1f;
    float zFar = 200.0f;

    glm::vec3 forward() const;
    glm::vec3 right() const;

    void rotate(float dyaw, float dpitch);
    void setAspect(float a) { aspect = a; }

    glm::mat4 view() const;
    glm::mat4 proj() const;
    glm::mat4 viewProj() const;
};

} // namespace ot
