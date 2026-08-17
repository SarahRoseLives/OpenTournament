#include "map/BspMap.h"

#include <algorithm>

#include "game/BrushCollisionWorld.h"

namespace ot::map {

namespace {

glm::vec3 pointAt(const Map& map, int32_t index) {
    return glm::vec3(map.points[index].x, map.points[index].y, map.points[index].z);
}

// Gathers a node's polygon points (via vertPool -> verts -> points) plus the
// per-vertex texture coordinates. Returns false if any index is out of range.
bool gatherPoly(const Map& map, const BspNode& node, std::vector<glm::vec3>& out,
                std::vector<glm::vec2>& outUv) {
    out.clear();
    outUv.clear();
    out.reserve(node.numVertices);
    outUv.reserve(node.numVertices);
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
        outUv.push_back(glm::vec2(map.verts[vi].u, map.verts[vi].v));
    }
    return true;
}

// A node is "solid" (blocks the player) unless it is a non-CSG sheet or its
// surface is flagged non-solid/invisible. Rendering and collision must agree so
// the visible map is exactly the collidable map (no walk-through walls).
bool isSolidNode(const Map& map, const BspNode& node) {
    if (node.nodeFlags & 0x01) {  // NF_NotCsg: non-CSG sheet polygon
        return false;
    }
    if (node.surf >= 0 && node.surf < static_cast<int32_t>(map.surfaces.size())) {
        const uint32_t pf = map.surfaces[node.surf].polyFlags;
        if (pf & 0x00000008) {  // PF_NotSolid
            return false;
        }
        if (pf & 0x00000001) {  // PF_Invisible
            return false;
        }
    }
    return true;
}

} // namespace

void triangulateBsp(const Map& map, TriangleMesh& out) {
    for (const auto& node : map.nodes) {
        if (node.numVertices < 3 || !isSolidNode(map, node)) {
            continue;
        }
        std::vector<glm::vec3> poly;
        std::vector<glm::vec2> polyUv;
        if (!gatherPoly(map, node, poly, polyUv) || poly.size() < 3) {
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
        const int32_t tex =
            (node.surf >= 0 && node.surf < static_cast<int32_t>(map.surfaces.size()))
                ? map.surfaces[node.surf].textureIndex
                : -1;

        for (size_t k = 1; k + 1 < poly.size(); ++k) {
            out.positions.push_back(poly[0]);
            out.positions.push_back(poly[k]);
            out.positions.push_back(poly[k + 1]);
            out.uvs.push_back(polyUv[0]);
            out.uvs.push_back(polyUv[k]);
            out.uvs.push_back(polyUv[k + 1]);
            out.normals.push_back(n);
            out.materialIndex.push_back(mat);
            out.textureIndex.push_back(tex);
        }
    }
}

void buildBspCollision(const Map& map, BrushCollisionWorld& out) {
    const float kThickness = 2.0f;

    for (const auto& node : map.nodes) {
        if (node.numVertices < 3 || !isSolidNode(map, node)) {
            continue;
        }
        std::vector<glm::vec3> poly;
        std::vector<glm::vec2> polyUv;
        if (!gatherPoly(map, node, poly, polyUv) || poly.size() < 3) {
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

std::vector<float> buildMesh(const Map& bsp, const TextureAtlas* atlas) {
    TriangleMesh mesh;
    triangulateBsp(bsp, mesh);

    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.35f));
    std::vector<float> verts;
    verts.reserve(mesh.positions.size() * 8);
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
        const int tex = mesh.textureIndex[i / 3];
        const float shade = 0.5f + 0.5f * std::fabs(glm::dot(n, lightDir));

        // Textured triangles use a neutral (white) vertex color so the texture
        // shows through un-tinted; untextured triangles fall back to a stable
        // per-material color derived from the material name.
        glm::vec3 color(1.0f);
        if (tex < 0) {
            uint32_t h = 2166136261u;
            for (char c : name) {
                h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
            }
            h ^= static_cast<uint32_t>(mat) * 2654435761u;
            const float r = ((h >> 16) & 0xff) / 255.0f;
            const float g = ((h >> 8) & 0xff) / 255.0f;
            const float b = (h & 0xff) / 255.0f;
            color = glm::vec3(0.45f + 0.55f * r, 0.45f + 0.55f * g, 0.45f + 0.55f * b);
        }
        color *= shade;

        // Per-texture atlas UV transform.
        float su = 1.0f, sv = 1.0f, ou = 0.0f, ov = 0.0f;
        if (atlas && tex >= 0 && tex < static_cast<int>(atlas->uvScale.size())) {
            su = atlas->uvScale[tex * 2 + 0];
            sv = atlas->uvScale[tex * 2 + 1];
            ou = atlas->uvOffset[tex * 2 + 0];
            ov = atlas->uvOffset[tex * 2 + 1];
        }

        const glm::vec2 uv0 = mesh.uvs[i + 0];
        const glm::vec2 uv1 = mesh.uvs[i + 1];
        const glm::vec2 uv2 = mesh.uvs[i + 2];
        const glm::vec2 auv0(ou + uv0.x * su, ov + uv0.y * sv);
        const glm::vec2 auv1(ou + uv1.x * su, ov + uv1.y * sv);
        const glm::vec2 auv2(ou + uv2.x * su, ov + uv2.y * sv);

        const glm::vec3* ps[3] = {&p0, &p1, &p2};
        const glm::vec2* uvs[3] = {&auv0, &auv1, &auv2};
        for (int k = 0; k < 3; ++k) {
            verts.push_back(ps[k]->x);
            verts.push_back(ps[k]->y);
            verts.push_back(ps[k]->z);
            verts.push_back(uvs[k]->x);
            verts.push_back(uvs[k]->y);
            verts.push_back(color.r);
            verts.push_back(color.g);
            verts.push_back(color.b);
        }
    }
    return verts;
}

void buildAtlas(const Map& map, TextureAtlas& out) {
    out = TextureAtlas{};
    if (map.textures.empty()) {
        return;
    }

    // Determine a uniform slot size (power of two, capped at 1024).
    int slot = 1;
    for (const auto& t : map.textures) {
        int m = std::max(t.width, t.height);
        while (slot < m && slot < 1024) {
            slot <<= 1;
        }
    }

    const int n = static_cast<int>(map.textures.size());
    int cols = 1;
    while (cols * cols < n) {
        ++cols;
    }
    int rows = (n + cols - 1) / cols;

    out.slot = slot;
    out.width = cols * slot;
    out.height = rows * slot;
    out.rgba.assign(static_cast<size_t>(out.width) * out.height * 4, 0);
    out.uvScale.resize(n * 2);
    out.uvOffset.resize(n * 2);

    for (int ti = 0; ti < n; ++ti) {
        const auto& tex = map.textures[ti];
        const int col = ti % cols;
        const int row = ti / cols;
        const int ox = col * slot;
        const int oy = row * slot;
        for (int y = 0; y < slot; ++y) {
            const int sy = tex.height > 0 ? (y * tex.height) / slot : 0;
            for (int x = 0; x < slot; ++x) {
                const int sx = tex.width > 0 ? (x * tex.width) / slot : 0;
                const size_t srcIdx = (static_cast<size_t>(sy) * tex.width + sx) * 4;
                const size_t dstIdx = (static_cast<size_t>(oy + y) * out.width + (ox + x)) * 4;
                if (srcIdx + 3 < tex.rgba.size()) {
                    out.rgba[dstIdx + 0] = tex.rgba[srcIdx + 0];
                    out.rgba[dstIdx + 1] = tex.rgba[srcIdx + 1];
                    out.rgba[dstIdx + 2] = tex.rgba[srcIdx + 2];
                    out.rgba[dstIdx + 3] = tex.rgba[srcIdx + 3];
                }
            }
        }
        out.uvScale[ti * 2 + 0] = static_cast<float>(slot) / out.width;
        out.uvScale[ti * 2 + 1] = static_cast<float>(slot) / out.height;
        out.uvOffset[ti * 2 + 0] = static_cast<float>(ox) / out.width;
        out.uvOffset[ti * 2 + 1] = static_cast<float>(oy) / out.height;
    }
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
