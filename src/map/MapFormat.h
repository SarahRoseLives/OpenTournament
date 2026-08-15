#pragma once

#include <cstdint>
#include <string>
#include <vector>

// OpenTournament map format (.otmap).
//
// Layout (all integers little-endian):
//   offset 0:  u32 magic = 0x4F544D50 ("OTMP")
//   offset 4:  u32 version
//   offset 8:  u32 totalSize
//   offset 12: u32 crc32      (CRC32 of bytes [headerSize, totalSize))
//   offset 16: char name[64]  (null-padded)
//   offset 80: section table  (headerSize = 80)
//   section table: u32 sectionCount, then per section: u32 id, u32 offset, u32 size
//
// Section ids:
//   1 = GEOMETRY (BSP: points, nodes, verts, surfaces)
//   2 = PLAYER_STARTS (count, then per start: pos(3f) + yaw(1f))
//   3 = MATERIALS (count, then per material: u32 len + utf8 bytes)

namespace ot::map {

constexpr uint32_t kMagic = 0x4F544D50u;
constexpr uint32_t kVersion = 1;
constexpr uint32_t kHeaderSize = 80;

enum class SectionId : uint32_t {
    Geometry = 1,
    PlayerStarts = 2,
    Materials = 3,
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// BSP node (mirrors UE2 FBspNode).
struct BspNode {
    float planeX = 0, planeY = 0, planeZ = 0, planeW = 0;
    int32_t vertPool = 0;   // index of first vertex in the vertex pool
    int32_t surf = 0;       // surface index
    int32_t vertex = 0;     // index of first vertex in the verts array
    int32_t collisionBound = 0;
    int8_t zone[2] = {0, 0};
    int8_t leaf[2] = {0, 0};
    uint8_t numVertices = 0;
    uint8_t nodeFlags = 0;
};

// BSP vertex reference (mirrors UE2 FVert).
struct BspVert {
    int32_t pointIndex = 0; // index into the vertex pool (points)
    int32_t side = 0;
};

// BSP surface (mirrors UE2 FBspSurf).
struct BspSurface {
    int32_t materialIndex = -1; // index into materials
    uint32_t polyFlags = 0;
    int32_t pBase = 0;          // vertex index for UV origin
    float normalX = 0, normalY = 0, normalZ = 0;
    float texUX = 0, texUY = 0, texUZ = 0;
    float texVX = 0, texVY = 0, texVZ = 0;
    int32_t brushPoly = 0;
    int32_t actor = 0;
    float planeX = 0, planeY = 0, planeZ = 0, planeW = 0;
};

struct Map {
    char name[64] = {0};
    std::vector<Vec3> points;      // vertex pool
    std::vector<BspNode> nodes;
    std::vector<BspVert> verts;
    std::vector<BspSurface> surfaces;
    std::vector<Vec3> spawnPoints;
    std::vector<float> spawnYaw;
    std::vector<std::string> materials;
};

uint32_t crc32(const uint8_t* data, size_t size);

bool saveMap(const Map& map, const std::string& path);
bool loadMap(Map& map, const std::string& path);

} // namespace ot::map
