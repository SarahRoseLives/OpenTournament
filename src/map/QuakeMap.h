#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ot {
class BrushCollisionWorld;
}

namespace ot::map {

// A plane with an outward-facing unit normal (the brush is the intersection
// of the half-spaces normal·x <= dist).
struct Plane {
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    float dist = 0.0f;
};

// A convex brush (solid) defined by its boundary planes.
struct Brush {
    std::vector<Plane> planes;
    std::vector<std::string> textures; // texture name per plane (for materials)
    glm::vec3 bmin{0.0f};
    glm::vec3 bmax{0.0f};
    std::string entityClass;           // owning entity's classname
};

struct SpawnPoint {
    glm::vec3 position{0.0f};
    float yaw = 0.0f; // degrees
};

// Material name -> deduplicated index.
struct QuakeMapData {
    std::vector<Brush> brushes;
    std::vector<SpawnPoint> spawns;
    std::vector<std::string> materials; // deduplicated texture names

    int materialIndex(const std::string& name);
};

// Parses Quake/Valve .map text (also used for Quake 2). Returns false on
// failure. Coordinates are converted from Quake (Z-up) to our (Y-up):
//   x -> x, y -> z, z -> y.
bool parseQuakeMap(const std::string& text, QuakeMapData& out);

// Triangulated brush geometry.
struct TriangleMesh {
    std::vector<glm::vec3> positions;     // flat, 3 vertices per triangle
    std::vector<int32_t> materialIndex;   // per triangle
    std::vector<glm::vec3> normals;       // per triangle (outward face normal)
};

void triangulateBrushes(QuakeMapData& data, TriangleMesh& out);

// Builds a convex-brush collision world from the parsed brushes.
void buildBrushCollision(const QuakeMapData& data, BrushCollisionWorld& out);

} // namespace ot::map
