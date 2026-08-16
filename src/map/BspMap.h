#pragma once

#include <glm/glm.hpp>

#include <vector>

#include "map/MapFormat.h"
#include "map/QuakeMap.h"

namespace ot {
class BrushCollisionWorld;
}

namespace ot::map {

// Triangulates BSP geometry (node polygons) into a render mesh. Points are
// already in the engine's Y-up space (the UE2 -> Y-up conversion happens when
// the .otmap is written by ue2tool).
void triangulateBsp(const Map& map, TriangleMesh& out);

// Builds convex-brush collision from the solid BSP polygons. Each polygon
// becomes a thin convex prism, so the existing BrushCollisionWorld can resolve
// player AABB collisions against it.
void buildBspCollision(const Map& map, BrushCollisionWorld& out);

// Builds interleaved vertex data (position + color, 6 floats per vertex) for
// rendering the BSP map.
std::vector<float> buildMesh(const Map& bsp);

// Computes the axis-aligned bounds of the points actually referenced by the
// BSP vertices (skips any unused points in the point pool).
void computeBounds(const Map& map, glm::vec3& bmin, glm::vec3& bmax);

} // namespace ot::map
