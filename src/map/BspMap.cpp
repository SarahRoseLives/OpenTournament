#include "map/BspMap.h"

#include "game/BrushCollisionWorld.h"

namespace ot::map {

namespace {

glm::vec3 pointAt(const Map& map, int32_t index) {
    return glm::vec3(map.points[index].x, map.points[index].y, map.points[index].z);
}

// Gathers a node's polygon points (via vertPool -> verts -> points). Returns
// false if any index is out of range.
bool gatherPoly(const Map& map, const BspNode& node, std::vector<glm::vec3>& out) {
    out.clear();
    out.reserve(node.numVertices);
    for (int32_t k = 0; k < node.numVertices; ++k) {
        const int32_t vi = node.vertPool + k;
        if (vi < 0 || vi >= static_cast<int32_t>(map.verts.size())) {
            return false;
        }
        const int32_t pi = map.verts[vi].pointIndex;
        if (pi < 0 || pi >= static_cast<int32_t>(map.points.size())) {
            return false;
        }
        out.push_back(pointAt(map, pi));
    }
    return true;
}

} // namespace

void triangulateBsp(const Map& map, TriangleMesh& out) {
    for (const auto& node : map.nodes) {
        if (node.numVertices < 3) {
            continue;
        }
        std::vector<glm::vec3> poly;
        if (!gatherPoly(map, node, poly) || poly.size() < 3) {
            continue;
        }

        glm::vec3 n = glm::cross(poly[1] - poly[0], poly[2] - poly[0]);
        if (glm::length(n) < 1e-6f) {
            n = glm::vec3(0.0f, 1.0f, 0.0f);
        } else {
            n = glm::normalize(n);
        }

        const int32_t mat =
            (node.surf >= 0 && node.surf < static_cast<int32_t>(map.surfaces.size()))
                ? map.surfaces[node.surf].materialIndex
                : 0;

        for (size_t k = 1; k + 1 < poly.size(); ++k) {
            out.positions.push_back(poly[0]);
            out.positions.push_back(poly[k]);
            out.positions.push_back(poly[k + 1]);
            out.normals.push_back(n);
            out.materialIndex.push_back(mat);
        }
    }
}

void buildBspCollision(const Map& map, BrushCollisionWorld& out) {
    const float kThickness = 2.0f;

    for (const auto& node : map.nodes) {
        if (node.numVertices < 3) {
            continue;
        }
        // NF_NotCsg: non-solid (transparent) polygon, skip for collision.
        if (node.nodeFlags & 0x01) {
            continue;
        }
        std::vector<glm::vec3> poly;
        if (!gatherPoly(map, node, poly) || poly.size() < 3) {
            continue;
        }

        // Drop duplicate/degenerate vertices.
        std::vector<glm::vec3> clean;
        for (const auto& p : poly) {
            if (clean.empty() || glm::length(p - clean.back()) > 0.01f) {
                clean.push_back(p);
            }
        }
        if (clean.size() >= 3 && glm::length(clean.front() - clean.back()) < 0.01f) {
            clean.pop_back();
        }
        if (clean.size() < 3) {
            continue;
        }

        // Polygon normal: prefer the outward-facing surface normal, fall back
        // to the cross product.
        glm::vec3 n(0.0f, 1.0f, 0.0f);
        if (node.surf >= 0 && node.surf < static_cast<int32_t>(map.surfaces.size())) {
            const auto& s = map.surfaces[node.surf];
            n = glm::vec3(s.normalX, s.normalY, s.normalZ);
        }
        if (glm::length(n) < 1e-6f) {
            n = glm::cross(clean[1] - clean[0], clean[2] - clean[0]);
        }
        n = glm::normalize(n);

        glm::vec3 centroid(0.0f);
        for (const auto& p : clean) {
            centroid += p;
        }
        centroid /= static_cast<float>(clean.size());

        // Build the prism: front/back faces along n plus one edge plane per
        // polygon edge. Edge normals point away from the centroid.
        std::vector<glm::vec3> normals;
        std::vector<float> dists;
        const float d0 = glm::dot(n, clean[0]);
        normals.push_back(n);
        dists.push_back(d0 + kThickness * 0.5f);
        normals.push_back(-n);
        dists.push_back(-d0 + kThickness * 0.5f);
        for (size_t i = 0; i < clean.size(); ++i) {
            const glm::vec3& a = clean[i];
            const glm::vec3& b = clean[(i + 1) % clean.size()];
            glm::vec3 en = glm::cross(b - a, n);
            if (glm::length(en) < 1e-6f) {
                continue;
            }
            en = glm::normalize(en);
            if (glm::dot(en, a - centroid) < 0.0f) {
                en = -en;
            }
            normals.push_back(en);
            dists.push_back(glm::dot(en, a));
        }

        glm::vec3 bmin(1e30f), bmax(-1e30f);
        for (const auto& p : clean) {
            bmin = glm::min(bmin, p);
            bmax = glm::max(bmax, p);
        }
        bmin -= glm::vec3(kThickness);
        bmax += glm::vec3(kThickness);

        out.addBrush(normals, dists, AABB{bmin, bmax});
    }
}

std::vector<float> buildMesh(const Map& bsp) {
    TriangleMesh mesh;
    triangulateBsp(bsp, mesh);

    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.35f));
    std::vector<float> verts;
    verts.reserve(mesh.positions.size() * 6);
    for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
        const glm::vec3& p0 = mesh.positions[i];
        const glm::vec3& p1 = mesh.positions[i + 1];
        const glm::vec3& p2 = mesh.positions[i + 2];
        glm::vec3 n = mesh.normals[i / 3];
        if (glm::length(n) < 1e-6f) {
            n = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        const int mat = mesh.materialIndex[i / 3];
        const std::string& name = (mat >= 0 && mat < static_cast<int>(bsp.materials.size()))
                                      ? bsp.materials[mat]
                                      : "";
        const float shade = 0.5f + 0.5f * std::fabs(glm::dot(n, lightDir));

        uint32_t h = 2166136261u;
        for (char c : name) {
            h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
        }
        h ^= static_cast<uint32_t>(mat) * 2654435761u;
        const float r = ((h >> 16) & 0xff) / 255.0f;
        const float g = ((h >> 8) & 0xff) / 255.0f;
        const float b = (h & 0xff) / 255.0f;
        const glm::vec3 color = glm::vec3(0.25f + 0.75f * r, 0.25f + 0.75f * g, 0.25f + 0.75f * b) * shade;

        for (const glm::vec3* p : {&p0, &p1, &p2}) {
            verts.push_back(p->x);
            verts.push_back(p->y);
            verts.push_back(p->z);
            verts.push_back(color.r);
            verts.push_back(color.g);
            verts.push_back(color.b);
        }
    }
    return verts;
}

void computeBounds(const Map& map, glm::vec3& bmin, glm::vec3& bmax) {
    bmin = glm::vec3(1e30f);
    bmax = glm::vec3(-1e30f);
    for (const auto& v : map.verts) {
        if (v.pointIndex < 0 || v.pointIndex >= static_cast<int32_t>(map.points.size())) {
            continue;
        }
        const auto& p = map.points[v.pointIndex];
        const glm::vec3 pos(p.x, p.y, p.z);
        bmin = glm::min(bmin, pos);
        bmax = glm::max(bmax, pos);
    }
}

} // namespace ot::map
