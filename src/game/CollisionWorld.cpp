#include "game/CollisionWorld.h"

#include <algorithm>
#include <cmath>

namespace ot {

void CollisionWorld::addBox(const glm::vec3& min, const glm::vec3& max) {
    m_boxes.push_back({min, max});
}

void CollisionWorld::buildDefault() {
    m_boxes.clear();

    addBox(glm::vec3(-25.0f, -1.0f, -25.0f), glm::vec3(25.0f, 0.0f, 25.0f));  // floor

    const float wallHeight = 4.0f;
    addBox(glm::vec3(-25, 0, -25), glm::vec3(25, wallHeight, -24));  // north
    addBox(glm::vec3(-25, 0, 24), glm::vec3(25, wallHeight, 25));    // south
    addBox(glm::vec3(-25, 0, -25), glm::vec3(-24, wallHeight, 25));  // west
    addBox(glm::vec3(24, 0, -25), glm::vec3(25, wallHeight, 25));    // east

    addBox(glm::vec3(3, 0, 3), glm::vec3(6, 2, 6));        // crate 1
    addBox(glm::vec3(-6, 0, -3), glm::vec3(-3, 1.5f, -1)); // crate 2
    addBox(glm::vec3(-8, 0, 6), glm::vec3(-5, 3, 9));      // crate 3
}

static bool overlaps(const glm::vec3& center, const glm::vec3& half, const AABB& box) {
    return center.x + half.x > box.min.x && center.x - half.x < box.max.x &&
           center.y + half.y > box.min.y && center.y - half.y < box.max.y &&
           center.z + half.z > box.min.z && center.z - half.z < box.max.z;
}

void CollisionWorld::resolve(glm::vec3& center, glm::vec3& velocity,
                             const glm::vec3& half, float dt, bool& onGround) const {
    onGround = false;

    center.x += velocity.x * dt;
    for (const auto& box : m_boxes) {
        if (overlaps(center, half, box)) {
            center.x = velocity.x > 0.0f ? box.min.x - half.x : box.max.x + half.x;
            velocity.x = 0.0f;
        }
    }

    center.z += velocity.z * dt;
    for (const auto& box : m_boxes) {
        if (overlaps(center, half, box)) {
            center.z = velocity.z > 0.0f ? box.min.z - half.z : box.max.z + half.z;
            velocity.z = 0.0f;
        }
    }

    center.y += velocity.y * dt;
    for (const auto& box : m_boxes) {
        if (overlaps(center, half, box)) {
            if (velocity.y <= 0.0f) {
                center.y = box.max.y + half.y;
                onGround = true;
            } else {
                center.y = box.min.y - half.y;
            }
            velocity.y = 0.0f;
        }
    }
}

static bool rayAABB(const glm::vec3& origin, const glm::vec3& dir,
                    const AABB& box, float maxDist, float& outT) {
    float tmin = 0.0f;
    float tmax = maxDist;

    for (int axis = 0; axis < 3; ++axis) {
        const float oa = origin[axis];
        const float da = dir[axis];
        if (std::fabs(da) < 1e-8f) {
            if (oa < box.min[axis] || oa > box.max[axis]) {
                return false;
            }
        } else {
            const float inv = 1.0f / da;
            float t1 = (box.min[axis] - oa) * inv;
            float t2 = (box.max[axis] - oa) * inv;
            if (t1 > t2) {
                std::swap(t1, t2);
            }
            if (t1 > tmin) {
                tmin = t1;
            }
            if (t2 < tmax) {
                tmax = t2;
            }
            if (tmin > tmax) {
                return false;
            }
        }
    }

    outT = tmin;
    return true;
}

RayHit CollisionWorld::rayBox(const glm::vec3& origin, const glm::vec3& dir,
                              const AABB& box, float maxDistance) {
    RayHit result;
    result.distance = maxDistance;

    float t = 0.0f;
    if (!rayAABB(origin, dir, box, maxDistance, t)) {
        return result;
    }

    result.hit = true;
    result.distance = t;
    result.point = origin + dir * t;

    const float eps = 1e-3f;
    glm::vec3 normal(0.0f);
    if (std::fabs(result.point.x - box.min.x) < eps) {
        normal.x = -1.0f;
    } else if (std::fabs(result.point.x - box.max.x) < eps) {
        normal.x = 1.0f;
    } else if (std::fabs(result.point.y - box.min.y) < eps) {
        normal.y = -1.0f;
    } else if (std::fabs(result.point.y - box.max.y) < eps) {
        normal.y = 1.0f;
    } else if (std::fabs(result.point.z - box.min.z) < eps) {
        normal.z = -1.0f;
    } else if (std::fabs(result.point.z - box.max.z) < eps) {
        normal.z = 1.0f;
    }
    result.normal = normal;
    return result;
}

RayHit CollisionWorld::raycast(const glm::vec3& origin, const glm::vec3& dir,
                               float maxDistance) const {
    RayHit best;
    best.distance = maxDistance;

    for (const auto& box : m_boxes) {
        const RayHit hit = rayBox(origin, dir, box, maxDistance);
        if (hit.hit && hit.distance < best.distance) {
            best = hit;
        }
    }

    return best;
}

} // namespace ot
