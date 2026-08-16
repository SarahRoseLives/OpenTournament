#include "game/Level.h"

#include <cmath>

#include "game/BrushCollisionWorld.h"
#include "game/CollisionWorld.h"
#include "map/BspMap.h"
#include "map/MapFormat.h"
#include "map/OtMap.h"
#include "render/GLHeaders.h"

namespace ot {

namespace {

void pushVertex(std::vector<float>& verts, const glm::vec3& pos, const glm::vec3& color) {
    verts.push_back(pos.x);
    verts.push_back(pos.y);
    verts.push_back(pos.z);
    verts.push_back(0.0f);
    verts.push_back(0.0f);
    verts.push_back(color.r);
    verts.push_back(color.g);
    verts.push_back(color.b);
}

void addQuad(std::vector<float>& verts, const glm::vec3& a, const glm::vec3& b,
             const glm::vec3& c, const glm::vec3& d, const glm::vec3& color) {
    pushVertex(verts, a, color);
    pushVertex(verts, b, color);
    pushVertex(verts, c, color);
    pushVertex(verts, a, color);
    pushVertex(verts, c, color);
    pushVertex(verts, d, color);
}

void addBox(std::vector<float>& verts, const glm::vec3& min, const glm::vec3& max,
            const glm::vec3& color) {
    const glm::vec3 c0(min.x, min.y, min.z);
    const glm::vec3 c1(max.x, min.y, min.z);
    const glm::vec3 c2(max.x, max.y, min.z);
    const glm::vec3 c3(min.x, max.y, min.z);
    const glm::vec3 c4(min.x, min.y, max.z);
    const glm::vec3 c5(max.x, min.y, max.z);
    const glm::vec3 c6(max.x, max.y, max.z);
    const glm::vec3 c7(min.x, max.y, max.z);

    const glm::vec3 side = color * 0.72f;
    const glm::vec3 top = color;
    const glm::vec3 bottom = color * 0.45f;

    addQuad(verts, c0, c3, c2, c1, side);
    addQuad(verts, c5, c6, c7, c4, side);
    addQuad(verts, c4, c7, c3, c0, side);
    addQuad(verts, c1, c2, c6, c5, side);
    addQuad(verts, c3, c7, c6, c2, top);
    addQuad(verts, c0, c1, c5, c4, bottom);
}

} // namespace

void Level::build() {
    auto world = std::make_unique<CollisionWorld>();
    world->buildDefault();
    m_world = std::move(world);

    // --- Floor: checkerboard tiles ---
    std::vector<float> floorVerts;
    const float tileSize = 100.0f;
    const float half = 1250.0f;
    const int tiles = 25;
    const float y = 0.5f;
    const glm::vec3 dark(0.25f, 0.27f, 0.33f);
    const glm::vec3 light(0.33f, 0.36f, 0.44f);

    for (int i = 0; i < tiles; ++i) {
        for (int j = 0; j < tiles; ++j) {
            const float x0 = -half + i * tileSize;
            const float z0 = -half + j * tileSize;
            const float x1 = x0 + tileSize;
            const float z1 = z0 + tileSize;
            const glm::vec3 color = ((i + j) % 2 == 0) ? dark : light;
            addQuad(floorVerts,
                    glm::vec3(x0, y, z0), glm::vec3(x1, y, z0),
                    glm::vec3(x1, y, z1), glm::vec3(x0, y, z1), color);
        }
    }
    m_floorMesh.upload(floorVerts);

    // --- Boxes (walls + crates) ---
    std::vector<float> boxVerts;
    const float wallHeight = 200.0f;
    const glm::vec3 wallColor(0.22f, 0.24f, 0.30f);
    addBox(boxVerts, glm::vec3(-1250, 0, -1250), glm::vec3(1250, wallHeight, -1200), wallColor);
    addBox(boxVerts, glm::vec3(-1250, 0, 1200), glm::vec3(1250, wallHeight, 1250), wallColor);
    addBox(boxVerts, glm::vec3(-1250, 0, -1250), glm::vec3(-1200, wallHeight, 1250), wallColor);
    addBox(boxVerts, glm::vec3(1200, 0, -1250), glm::vec3(1250, wallHeight, 1250), wallColor);
    addBox(boxVerts, glm::vec3(150, 0, 150), glm::vec3(300, 100, 300), glm::vec3(0.80f, 0.45f, 0.12f));
    addBox(boxVerts, glm::vec3(-300, 0, -150), glm::vec3(-150, 75, -50), glm::vec3(0.18f, 0.55f, 0.85f));
    addBox(boxVerts, glm::vec3(-400, 0, 300), glm::vec3(-250, 150, 450), glm::vec3(0.40f, 0.70f, 0.30f));
    m_boxMesh.upload(boxVerts);
}

void Level::destroy() {
    m_floorMesh.destroy();
    m_boxMesh.destroy();
    m_mapMesh.destroy();
    if (m_mapTexture) {
        glDeleteTextures(1, &m_mapTexture);
        m_mapTexture = 0;
    }
}

void Level::buildFromMap(const map::GeneratedMap& map) {
    m_mapMesh.destroy();
    auto world = std::make_unique<CollisionWorld>();
    map::buildCollision(map, *world);
    m_world = std::move(world);
    m_mapMesh.upload(map::buildMesh(map));
}

void Level::buildFromBsp(const map::Map& bsp) {
    m_mapMesh.destroy();
    if (m_mapTexture) {
        glDeleteTextures(1, &m_mapTexture);
        m_mapTexture = 0;
    }
    auto world = std::make_unique<BrushCollisionWorld>();
    map::buildBspCollision(bsp, *world);
    m_world = std::move(world);

    map::TextureAtlas atlas;
    map::buildAtlas(bsp, atlas);
    if (!atlas.rgba.empty()) {
        glGenTextures(1, &m_mapTexture);
        glBindTexture(GL_TEXTURE_2D, m_mapTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.width, atlas.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, atlas.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_mapMesh.upload(map::buildMesh(bsp, &atlas));
    } else {
        m_mapMesh.upload(map::buildMesh(bsp, nullptr));
    }
}

} // namespace ot
