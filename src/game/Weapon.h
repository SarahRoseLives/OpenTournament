#pragma once

#include <glm/glm.hpp>

#include <vector>

#include "render/Camera.h"

namespace ot {

class CollisionWorld;
class Input;

struct Tracer {
    glm::vec3 start;
    glm::vec3 end;
    float timeLeft = 0.0f;
};

// A simple hitscan weapon with aim-down-sights (FOV zoom), a fire cooldown,
// and short-lived tracer lines for visual feedback.
class Weapon {
public:
    void update(float dt, const Input& input, const Camera& camera, CollisionWorld& world);

    float aimFactor() const { return m_aim; }
    bool hitFlash() const { return m_flashTimer > 0.0f; }
    const std::vector<Tracer>& tracers() const { return m_tracers; }

private:
    void fire(const Camera& camera, CollisionWorld& world);

    float m_cooldown = 0.0f;
    float m_aim = 0.0f;
    float m_flashTimer = 0.0f;
    std::vector<Tracer> m_tracers;
};

} // namespace ot
