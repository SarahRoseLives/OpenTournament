#pragma once

#include <glm/glm.hpp>

#include <vector>

#include "game/ICollisionWorld.h"

namespace ot {

// AABB-based collision world (procedural arena). GL-free; used by both the
// client (rendering) and the headless dedicated server.
class CollisionWorld : public ICollisionWorld {
public:
    void addBox(const glm::vec3& min, const glm::vec3& max);

    void clear() { m_boxes.clear(); }

    void buildDefault();

    void resolve(glm::vec3& center, glm::vec3& velocity,
                 const glm::vec3& half, float dt, bool& onGround) const override;

    RayHit raycast(const glm::vec3& origin, const glm::vec3& dir,
                   float maxDistance) const override;

    static RayHit rayBox(const glm::vec3& origin, const glm::vec3& dir,
                         const AABB& box, float maxDistance);

private:
    std::vector<AABB> m_boxes;
};

} // namespace ot
