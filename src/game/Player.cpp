#include "game/Player.h"

#include "game/ICollisionWorld.h"
#include "input/Input.h"

namespace ot {

void Player::spawn(const glm::vec3& position, float yaw) {
    m_center = position;
    m_velocity = glm::vec3(0.0f);
    m_onGround = false;
    m_camera.yaw = yaw;
    m_camera.pitch = 0.0f;
    m_camera.position = m_center + glm::vec3(0.0f, kEyeHeight - kHalfHeight, 0.0f);
}

void Player::applyInput(const PlayerInput& input, float dt, ICollisionWorld& world) {
    m_camera.yaw = input.yaw;
    m_camera.pitch = input.pitch;

    glm::vec3 wishDir = m_camera.forward() * input.moveY + m_camera.right() * input.moveX;
    wishDir.y = 0.0f;
    const float len = glm::length(wishDir);
    if (len > 0.0001f) {
        wishDir /= len;
    }
    m_velocity.x = wishDir.x * kSpeed;
    m_velocity.z = wishDir.z * kSpeed;

    if (input.jump && m_onGround) {
        m_velocity.y = kJumpSpeed;
        m_onGround = false;
    }

    m_velocity.y -= kGravity * dt;

    const glm::vec3 half(kHalfWidth, kHalfHeight, kHalfWidth);
    world.resolve(m_center, m_velocity, half, dt, m_onGround);

    m_camera.position = m_center + glm::vec3(0.0f, kEyeHeight - kHalfHeight, 0.0f);
}

void Player::update(float dt, const Input& input, ICollisionWorld& world) {
    const glm::vec2 look = input.lookDelta(dt);
    m_camera.rotate(look.x, look.y);

    const glm::vec2 move = input.moveAxis();

    PlayerInput pi;
    pi.moveX = move.x;
    pi.moveY = move.y;
    pi.yaw = m_camera.yaw;
    pi.pitch = m_camera.pitch;
    pi.fire = input.fireHeld();
    pi.aim = input.aimHeld();
    pi.jump = input.jumpHeld();
    applyInput(pi, dt, world);
}

void Player::setState(const glm::vec3& center, float yaw, float pitch) {
    m_center = center;
    m_velocity = glm::vec3(0.0f);
    m_camera.yaw = yaw;
    m_camera.pitch = pitch;
    m_camera.position = m_center + glm::vec3(0.0f, kEyeHeight - kHalfHeight, 0.0f);
}

} // namespace ot
