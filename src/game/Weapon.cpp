#include "game/Weapon.h"

#include <cmath>

#include "game/ICollisionWorld.h"
#include "game/WeaponDef.h"
#include "input/Input.h"

namespace ot {

namespace {
constexpr float kAimSpeed = 12.0f;
constexpr float kTracerLifetime = 0.07f;
constexpr float kFlashDuration = 0.09f;
constexpr float kMuzzleOffset = 40.0f;
} // namespace

void Weapon::update(float dt, const Input& input, const Camera& camera, ICollisionWorld& world,
                    const WeaponDef& def) {
    const float targetAim = input.aimHeld() ? 1.0f : 0.0f;
    m_aim += (targetAim - m_aim) * (1.0f - std::exp(-kAimSpeed * dt));

    m_cooldown -= dt;
    if (input.fireHeld() && m_cooldown <= 0.0f) {
        fire(camera, world, def);
        m_cooldown = def.fireInterval;
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

void Weapon::fire(const Camera& camera, ICollisionWorld& world, const WeaponDef& def) {
    const glm::vec3 dir = camera.forward();
    const glm::vec3 origin = camera.position + dir * kMuzzleOffset;

    const RayHit hit = world.raycast(origin, dir, def.range);
    const glm::vec3 end = hit.hit ? hit.point : origin + dir * def.range;

    m_tracers.push_back({origin, end, kTracerLifetime});
    if (hit.hit) {
        m_flashTimer = kFlashDuration;
    }
}

} // namespace ot
