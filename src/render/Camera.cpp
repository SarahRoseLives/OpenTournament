#include "render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace ot {

static const glm::vec3 kUp(0.0f, 1.0f, 0.0f);

glm::vec3 Camera::forward() const {
    const float cp = std::cos(pitch);
    return glm::vec3(cp * std::sin(yaw), std::sin(pitch), -cp * std::cos(yaw));
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), kUp));
}

void Camera::rotate(float dyaw, float dpitch) {
    yaw += dyaw;
    pitch += dpitch;

    constexpr float kMaxPitch = glm::radians(89.0f);
    pitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, position + forward(), kUp);
}

glm::mat4 Camera::proj() const {
    return glm::perspective(fov, aspect, zNear, zFar);
}

glm::mat4 Camera::viewProj() const {
    return proj() * view();
}

} // namespace ot
