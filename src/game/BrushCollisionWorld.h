#pragma once

#include <glm/glm.hpp>

#include <vector>

#include "game/ICollisionWorld.h"

namespace ot {

// A convex solid: the intersection of half-spaces normal·x <= dist
// (normals point outward).
struct ConvexBrush {
    std::vector<glm::vec3> normals;
    std::vector<float> dists;
    AABB bounds;
};

// Convex-brush collision world (imported .map levels). GL-free.
class BrushCollisionWorld : public ICollisionWorld {
public:
    void addBrush(const std::vector<glm::vec3>& normals,
                  const std::vector<float>& dists,
                  const AABB& bounds);

    void resolve(glm::vec3& center, glm::vec3& velocity,
                 const glm::vec3& half, float dt, bool& onGround) const override;

    RayHit raycast(const glm::vec3& origin, const glm::vec3& dir,
                   float maxDistance) const override;

    size_t brushCount() const { return m_brushes.size(); }

private:
    std::vector<ConvexBrush> m_brushes;
};

} // namespace ot
