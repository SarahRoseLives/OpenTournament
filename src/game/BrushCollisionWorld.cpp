#include "game/BrushCollisionWorld.h"

#include <cmath>
#include <cstdio>

namespace ot {

void BrushCollisionWorld::addBrush(const std::vector<glm::vec3>& normals,
                                   const std::vector<float>& dists,
                                   const AABB& bounds) {
    if (normals.size() < 4) {
        return;
    }
    ConvexBrush brush;
    brush.normals = normals;
    brush.dists = dists;
    brush.bounds = bounds;
    m_brushes.push_back(std::move(brush));
}

void BrushCollisionWorld::resolve(glm::vec3& center, glm::vec3& velocity,
                                  const glm::vec3& half, float dt, bool& onGround) const {
    onGround = false;
    center += velocity * dt;

    const float incomingVy = velocity.y;

    for (int iter = 0; iter < 4; ++iter) {
        bool pushed = false;
        for (const auto& brush : m_brushes) {
            // Broad phase: AABB overlap.
            if (center.x + half.x <= brush.bounds.min.x ||
                center.x - half.x >= brush.bounds.max.x ||
                center.y + half.y <= brush.bounds.min.y ||
                center.y - half.y >= brush.bounds.max.y ||
                center.z + half.z <= brush.bounds.min.z ||
                center.z - half.z >= brush.bounds.max.z) {
                continue;
            }

            // Find the shallowest penetrating face and push out along it.
            glm::vec3 push(0.0f);
            float minDepth = 1e30f;
            bool penetrating = true;
            for (size_t i = 0; i < brush.normals.size(); ++i) {
                const glm::vec3& n = brush.normals[i];
                const float closest =
                    glm::dot(n, center) -
                    (std::fabs(n.x) * half.x + std::fabs(n.y) * half.y +
                     std::fabs(n.z) * half.z);
                const float d = closest - brush.dists[i];
                if (d > 0.0f) {
                    penetrating = false;
                    break;
                }
                const float depth = -d;
                if (depth < minDepth) {
                    minDepth = depth;
                    push = n * depth;
                }
            }

            if (penetrating && minDepth < 1e29f) {
                static int dbg = 0;
                if (dbg < 12) {
                    std::printf("[ot] resolve push: depth=%.3f push=(%.2f %.2f %.2f) planes=%zu "
                                "bmin=(%.0f %.0f %.0f) bmax=(%.0f %.0f %.0f)\n",
                                minDepth, push.x, push.y, push.z, brush.normals.size(),
                                brush.bounds.min.x, brush.bounds.min.y, brush.bounds.min.z,
                                brush.bounds.max.x, brush.bounds.max.y, brush.bounds.max.z);
                    ++dbg;
                }
                center += push;
                const glm::vec3 n = glm::normalize(push);
                const float vn = glm::dot(velocity, n);
                if (vn < 0.0f) {
                    velocity -= n * vn;
                }
                if (n.y > 0.7f && incomingVy <= 0.0f) {
                    onGround = true;
                }
                pushed = true;
            }
        }
        if (!pushed) {
            break;
        }
    }
}

RayHit BrushCollisionWorld::raycast(const glm::vec3& origin, const glm::vec3& dir,
                                    float maxDistance) const {
    RayHit best;
    best.distance = maxDistance;

    for (const auto& brush : m_brushes) {
        // Quick AABB reject.
        {
            float tmin = 0.0f;
            float tmax = maxDistance;
            bool ok = true;
            for (int axis = 0; axis < 3; ++axis) {
                const float o = origin[axis];
                const float d = dir[axis];
                if (std::fabs(d) < 1e-8f) {
                    if (o < brush.bounds.min[axis] || o > brush.bounds.max[axis]) {
                        ok = false;
                        break;
                    }
                } else {
                    float t1 = (brush.bounds.min[axis] - o) / d;
                    float t2 = (brush.bounds.max[axis] - o) / d;
                    if (t1 > t2) {
                        std::swap(t1, t2);
                    }
                    tmin = std::max(tmin, t1);
                    tmax = std::min(tmax, t2);
                    if (tmin > tmax) {
                        ok = false;
                        break;
                    }
                }
            }
            if (!ok) {
                continue;
            }
        }

        // Slab test against the brush planes (inside = normal·x <= dist).
        float tNear = 0.0f;
        float tFar = maxDistance;
        int hitPlane = -1;
        bool hit = true;
        for (size_t i = 0; i < brush.normals.size(); ++i) {
            const glm::vec3& n = brush.normals[i];
            const float denom = glm::dot(n, dir);
            const float numer = brush.dists[i] - glm::dot(n, origin);
            if (std::fabs(denom) < 1e-8f) {
                if (numer < 0.0f) {
                    hit = false;
                    break;
                }
            } else {
                const float t = numer / denom;
                if (denom < 0.0f) {
                    if (t > tNear) {
                        tNear = t;
                        hitPlane = static_cast<int>(i);
                    }
                } else {
                    if (t < tFar) {
                        tFar = t;
                    }
                }
                if (tNear > tFar) {
                    hit = false;
                    break;
                }
            }
        }

        if (hit && tNear >= 0.0f && tNear < best.distance) {
            best.hit = true;
            best.distance = tNear;
            best.point = origin + dir * tNear;
            best.normal = (hitPlane >= 0) ? brush.normals[hitPlane] : glm::vec3(0, 1, 0);
        }
    }

    return best;
}

} // namespace ot
