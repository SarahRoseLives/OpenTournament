#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ot {
class CollisionWorld;
}

namespace ot::map {

// OpenTournament text map format (.otmap).
//
// The file stores a seed + generator parameters (a "recipe"). The actual
// geometry is produced deterministically by generate() on every platform, so
// the server and all clients reconstruct identical levels from the same text.
//
// Example:
//   # OpenTournament Map v1
//   name "DM-123456789"
//   seed 123456789
//   arena 40
//   obstacles 6
//   spawns 6
//
// All layout is computed on an integer grid and converted to float only at
// the end, so the generated geometry is byte-identical on x86 and ARM.

constexpr int kMapVersion = 1;
constexpr int kMaxSpawns = 8;

struct GenParams {
    uint32_t seed = 0;
    float arena = 40.0f;    // playable half-extent in world units
    int obstacles = 6;      // number of symmetric cover clusters (mirrored 4x)
    int spawns = 6;         // number of spawn points (1..kMaxSpawns)
};

struct Box {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    int material = 0;
};

struct Spawn {
    glm::vec3 position{0.0f}; // feet position (y = 0)
    float yaw = 0.0f;         // radians, facing toward the arena center
};

struct GeneratedMap {
    std::string name;
    std::vector<glm::vec3> materials; // palette colors, indexed by Box::material
    std::vector<Box> boxes;
    std::vector<Spawn> spawns;
};

// Parse .otmap text into parameters + name. Returns false on failure.
bool parseOtMapText(const std::string& text, GenParams& params, std::string& name);

// Serialize parameters + name to .otmap text.
std::string serializeOtMapText(const GenParams& params, const std::string& name);

// Deterministically generate the level from the given parameters.
GeneratedMap generate(const GenParams& params);

// Build interleaved vertex data (position + color, 6 floats per vertex) for
// rendering the generated map.
std::vector<float> buildMesh(const GeneratedMap& map);

// Populate an AABB collision world from the generated map.
void buildCollision(const GeneratedMap& map, CollisionWorld& world);

} // namespace ot::map
