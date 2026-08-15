#pragma once

#include <glm/glm.hpp>

#include <vector>

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

// GL-free collision data and queries. Used by both the client (rendering)
// and the headless dedicated server.
class CollisionWorld {
public:
    void addBox(const glm::vec3& min, const glm::vec3& max);

    void buildDefault();

    void resolve(glm::vec3& center, glm::vec3& velocity,
                 const glm::vec3& half, float dt, bool& onGround) const;

    RayHit raycast(const glm::vec3& origin, const glm::vec3& dir,
                   float maxDistance) const;

    static RayHit rayBox(const glm::vec3& origin, const glm::vec3& dir,
                         const AABB& box, float maxDistance);

private:
    std::vector<AABB> m_boxes;
};

} // namespace ot
