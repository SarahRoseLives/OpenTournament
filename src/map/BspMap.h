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

// Packs the map's textures into a single RGBA atlas image (grid layout, each
// texture resized to a uniform power-of-two slot).
struct TextureAtlas {
    std::vector<uint8_t> rgba;   // atlas pixels (RGBA8)
    int width = 0;
    int height = 0;
    int slot = 0;                // per-texture slot size
    std::vector<float> uvScale;  // per texture (sx, sy)
    std::vector<float> uvOffset; // per texture (ox, oy)
};
void buildAtlas(const Map& map, TextureAtlas& out);

// Builds interleaved vertex data (position + uv + color, 8 floats per vertex)
// for rendering the BSP map. If `atlas` is non-null, texture coordinates are
// remapped into the atlas space.
std::vector<float> buildMesh(const Map& bsp, const TextureAtlas* atlas = nullptr);

// Computes the axis-aligned bounds of the points actually referenced by the
// BSP vertices (skips any unused points in the point pool).
void computeBounds(const Map& map, glm::vec3& bmin, glm::vec3& bmax);

} // namespace ot::map
