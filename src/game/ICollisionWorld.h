#pragma once

#include <glm/glm.hpp>

namespace ot {

struct AABB {
    glm::vec3 min;
    glm::vec3 max;
};

struct RayHit {
    bool hit = false;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
};

// Collision-world interface shared by the procedural arena (AABB based) and
// imported .map levels (convex brush based).
class ICollisionWorld {
public:
    virtual ~ICollisionWorld() = default;

    // Resolves the player AABB (centered at `center`, half extents `half`)
    // against the world after a velocity step.
    virtual void resolve(glm::vec3& center, glm::vec3& velocity,
                         const glm::vec3& half, float dt, bool& onGround) const = 0;

    virtual RayHit raycast(const glm::vec3& origin, const glm::vec3& dir,
                           float maxDistance) const = 0;
};

} // namespace ot
