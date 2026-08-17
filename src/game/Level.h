#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <vector>

#include "game/ICollisionWorld.h"
#include "render/Mesh.h"

namespace ot {

namespace map {
struct GeneratedMap;
struct Map;
}

// Renders the level and owns the collision world used for physics/queries.
class Level {
public:
    void build();
    void buildFromMap(const map::GeneratedMap& map);
    void buildFromBsp(const map::Map& bsp);
    void destroy();

    ICollisionWorld& world() { return *m_world; }
    const ICollisionWorld& world() const { return *m_world; }

    const Mesh& floorMesh() const { return m_floorMesh; }
    const Mesh& boxMesh() const { return m_boxMesh; }
    const Mesh& mapMesh() const { return m_mapMesh; }
    const Mesh& flagMesh() const { return m_flagMesh; }
    unsigned int mapTexture() const { return m_mapTexture; }

    // CTF flag home positions (world space, Y-up). Empty for non-CTF maps.
    const std::vector<glm::vec3>& flagPositions() const { return m_flagPositions; }

private:
    std::unique_ptr<ICollisionWorld> m_world;
    Mesh m_floorMesh;
    Mesh m_boxMesh;
    Mesh m_mapMesh;
    Mesh m_flagMesh;
    std::vector<glm::vec3> m_flagPositions;
    unsigned int m_mapTexture = 0;
};

} // namespace ot
