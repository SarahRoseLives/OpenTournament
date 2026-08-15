#include "game/Weapon.h"

#include <cmath>

#include "game/CollisionWorld.h"
#include "input/Input.h"

namespace ot {

namespace {
constexpr float kFireRate = 0.12f;
constexpr float kAimSpeed = 12.0f;
constexpr float kMaxDistance = 300.0f;
constexpr float kTracerLifetime = 0.07f;
constexpr float kFlashDuration = 0.09f;
constexpr float kMuzzleOffset = 0.4f;
} // namespace

void Weapon::update(float dt, const Input& input, const Camera& camera, CollisionWorld& world) {
    const float targetAim = input.aimHeld() ? 1.0f : 0.0f;
    m_aim += (targetAim - m_aim) * (1.0f - std::exp(-kAimSpeed * dt));

    m_cooldown -= dt;
    if (input.fireHeld() && m_cooldown <= 0.0f) {
        fire(camera, world);
        m_cooldown = kFireRate;
    }

    for (auto it = m_tracers.begin(); it != m_tracers.end();) {
        it->timeLeft -= dt;
        if (it->timeLeft <= 0.0f) {
            it = m_tracers.erase(it);
        } else {
            ++it;
        }
    }

    if (m_flashTimer > 0.0f) {
        m_flashTimer -= dt;
    }
}

void Weapon::fire(const Camera& camera, CollisionWorld& world) {
    const glm::vec3 dir = camera.forward();
    const glm::vec3 origin = camera.position + dir * kMuzzleOffset;

    const RayHit hit = world.raycast(origin, dir, kMaxDistance);
    const glm::vec3 end = hit.hit ? hit.point : origin + dir * kMaxDistance;

    m_tracers.push_back({origin, end, kTracerLifetime});
    if (hit.hit) {
        m_flashTimer = kFlashDuration;
    }
}

} // namespace ot
