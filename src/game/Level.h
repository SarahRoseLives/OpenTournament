#pragma once

#include <glm/glm.hpp>

#include <vector>

#include "game/CollisionWorld.h"
#include "render/Mesh.h"

namespace ot {

namespace map {
struct GeneratedMap;
}

// Renders the level and owns the collision world used for physics/queries.
class Level {
public:
    void build();
    void buildFromMap(const map::GeneratedMap& map);
    void destroy();

    CollisionWorld& world() { return m_world; }
    const CollisionWorld& world() const { return m_world; }

    const Mesh& floorMesh() const { return m_floorMesh; }
    const Mesh& boxMesh() const { return m_boxMesh; }
    const Mesh& mapMesh() const { return m_mapMesh; }

private:
    CollisionWorld m_world;
    Mesh m_floorMesh;
    Mesh m_boxMesh;
    Mesh m_mapMesh;
};

} // namespace ot
