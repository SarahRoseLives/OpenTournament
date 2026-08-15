#include "game/Level.h"

#include <cmath>

namespace ot {

namespace {

void pushVertex(std::vector<float>& verts, const glm::vec3& pos, const glm::vec3& color) {
    verts.push_back(pos.x);
    verts.push_back(pos.y);
    verts.push_back(pos.z);
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
    m_world.buildDefault();

    // --- Floor: checkerboard tiles ---
    std::vector<float> floorVerts;
    const float tileSize = 2.0f;
    const float half = 25.0f;
    const int tiles = 25;
    const float y = 0.01f;
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
    const float wallHeight = 4.0f;
    const glm::vec3 wallColor(0.22f, 0.24f, 0.30f);
    addBox(boxVerts, glm::vec3(-25, 0, -25), glm::vec3(25, wallHeight, -24), wallColor);
    addBox(boxVerts, glm::vec3(-25, 0, 24), glm::vec3(25, wallHeight, 25), wallColor);
    addBox(boxVerts, glm::vec3(-25, 0, -25), glm::vec3(-24, wallHeight, 25), wallColor);
    addBox(boxVerts, glm::vec3(24, 0, -25), glm::vec3(25, wallHeight, 25), wallColor);
    addBox(boxVerts, glm::vec3(3, 0, 3), glm::vec3(6, 2, 6), glm::vec3(0.80f, 0.45f, 0.12f));
    addBox(boxVerts, glm::vec3(-6, 0, -3), glm::vec3(-3, 1.5f, -1), glm::vec3(0.18f, 0.55f, 0.85f));
    addBox(boxVerts, glm::vec3(-8, 0, 6), glm::vec3(-5, 3, 9), glm::vec3(0.40f, 0.70f, 0.30f));
    m_boxMesh.upload(boxVerts);
}

void Level::destroy() {
    m_floorMesh.destroy();
    m_boxMesh.destroy();
}

} // namespace ot
