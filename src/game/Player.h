#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "render/Camera.h"

namespace ot {

class ICollisionWorld;
class Input;

struct PlayerInput {
    float moveX = 0.0f;
    float moveY = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool fire = false;
    bool aim = false;
    bool jump = false;
    int weapon = 0;
    uint32_t sequence = 0;
};

class Player {
public:
    static constexpr float kHalfWidth = 42.0f;
    static constexpr float kHalfHeight = 44.0f;
    static constexpr float kEyeHeight = 64.0f;

    void spawn(const glm::vec3& position, float yaw);

    // Authoritative/networked: set look angles from input and integrate movement.
    void applyInput(const PlayerInput& input, float dt, ICollisionWorld& world);

    // Local single-player: applies mouse/stick look deltas, then moves.
    void update(float dt, const Input& input, ICollisionWorld& world);

    void setState(const glm::vec3& center, float yaw, float pitch);

    const Camera& camera() const { return m_camera; }
    Camera& camera() { return m_camera; }

    const glm::vec3& center() const { return m_center; }
    const glm::vec3& velocity() const { return m_velocity; }

private:
    static constexpr float kSpeed = 440.0f;
    static constexpr float kGravity = 950.0f;
    static constexpr float kJumpSpeed = 420.0f;

    Camera m_camera;
    glm::vec3 m_center{0.0f};
    glm::vec3 m_velocity{0.0f};
    bool m_onGround = false;
};

} // namespace ot
