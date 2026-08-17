#include "Convert.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "UE2.h"

using namespace ot::ue2;

namespace {

float rdFloat(const uint8_t* p) {
    uint32_t bits = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                    (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

int32_t rdi32(const uint8_t* p) {
    return static_cast<int32_t>(static_cast<uint32_t>(p[0]) |
                                (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16) |
                                (static_cast<uint32_t>(p[3]) << 24));
}

int16_t rdi16(const uint8_t* p) {
    return static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                                (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t rdu32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int32_t findNameIndex(const Package& pkg, const char* name) {
    for (int i = 0; i < static_cast<int>(pkg.nameCount()); ++i) {
        if (pkg.name(i) == name) {
            return i;
        }
    }
    return -1;
}

struct PlayerStart {
    float x = 0, y = 0, z = 0;
    int32_t yaw = 0;
};

// Walks an actor export's execution-stack state frame + tagged-property stream,
// extracting Location, Rotation yaw, and Team. Returns true if a Location was
// found. Shared by PlayerStart and FlagBase parsing.
struct ActorTransform {
    float x = 0, y = 0, z = 0;
    int32_t yaw = 0;
    int team = 0;
    bool haveLoc = false;
};

bool parseActorProps(const Package& pkg, const uint8_t* base, size_t baseSize,
                     const uint8_t* p, size_t size, ActorTransform& out) {
    const uint8_t* hardEnd = base + baseSize;
    const uint8_t* aend = p + size;
    if (aend > hardEnd) aend = hardEnd;

    const int32_t noneIdx = findNameIndex(pkg, "None");
    const int32_t nameLoc = findNameIndex(pkg, "Location");
    const int32_t nameVec = findNameIndex(pkg, "Vector");
    const int32_t nameRot = findNameIndex(pkg, "Rotation");
    const int32_t nameRotator = findNameIndex(pkg, "Rotator");
    const int32_t nameTeam = findNameIndex(pkg, "Team");

    // Execution-stack state frame: Node, StateNode, ProbeMask(8),
    // LatentAction(4), then Offset (only if Node != 0).
    const int32_t node = Package::readCompact(p, aend);
    Package::readCompact(p, aend);  // StateNode
    p += 8;                        // ProbeMask (QWORD)
    p += 4;                        // LatentAction (INT)
    if (node != 0) {
        Package::readCompact(p, aend);  // Offset
    }

    while (p + 2 <= aend) {
        const int32_t name = Package::readCompact(p, aend);
        if (name == noneIdx) break;
        const uint8_t info = *p++;
        const uint8_t typ = info & 0x0F;
        int32_t item = -1;
        if (typ == 10) item = Package::readCompact(p, aend);
        const uint8_t sc = info & 0x70;
        int32_t size;
        if (sc == 0x00) size = 1;
        else if (sc == 0x10) size = 2;
        else if (sc == 0x20) size = 4;
        else if (sc == 0x30) size = 12;
        else if (sc == 0x40) size = 16;
        else if (sc == 0x50) { if (p >= aend) break; size = *p++; }
        else if (sc == 0x60) { if (p + 2 > aend) break; size = rdi32(p) & 0xffff; p += 2; }
        else { if (p + 4 > aend) break; size = rdi32(p); p += 4; }
        if ((info & 0x80) && typ != 3) {
            const uint8_t b = *p++;
            if (b & 0x80) p += ((b & 0xC0) == 0x80) ? 1 : 3;
        }
        if (name == nameLoc && typ == 10 && item == nameVec && p + 12 <= aend) {
            out.x = rdFloat(p); out.y = rdFloat(p + 4); out.z = rdFloat(p + 8);
            p += 12;
            out.haveLoc = true;
        } else if (name == nameRot && typ == 10 && item == nameRotator && p + 12 <= aend) {
            out.yaw = rdi32(p + 4);
            p += 12;
        } else if (name == nameTeam && typ == 1 && p + 1 <= aend) {
            out.team = p[0];
            p += 1;
        } else if (typ == 3) {
            // bool: value is stored in the info byte's array flag, no bytes follow.
        } else {
            p += size;
        }
    }
    return out.haveLoc;
}

}  // namespace

bool ue1ToOtMap(const std::string& unrPath, const std::string& ut99Root,
                ot::map::Map& out) {
    (void)ut99Root;
    Package pkg;
    if (!pkg.open(unrPath)) {
        return false;
    }

    // Find the largest "Model" export = the level BSP.
    int modelIdx = -1;
    int modelSize = -1;
    for (int i = 0; i < pkg.exportCount(); ++i) {
        if (pkg.exportClass(i) == "Model" && pkg.exp(i).serialSize > modelSize) {
            modelSize = pkg.exp(i).serialSize;
            modelIdx = i;
        }
    }
    if (modelIdx < 0) {
        std::fprintf(stderr, "[ue1] no Model export\n");
        return false;
    }
    const auto& m = pkg.exp(modelIdx);
    const uint8_t* base = pkg.data();
    const uint8_t* p = base + m.serialOffset;
    const uint8_t* end = p + m.serialSize;
    const uint8_t* hardEnd = base + pkg.size();
    if (end > hardEnd) end = hardEnd;

    std::printf("[ue1] Model %s size=%d off=%d\n", pkg.exportName(modelIdx).c_str(),
                m.serialSize, m.serialOffset);

    // UModel::Serialize: UObject/UPrimitive serialization (42 bytes for the main
    // model), then the BSP arrays.
    p += 42;

    const int32_t vectorCount = Package::readCompact(p, end);
    std::vector<ot::map::Vec3> vectors(vectorCount);
    for (int32_t i = 0; i < vectorCount && p + 12 <= end; ++i) {
        vectors[i].x = rdFloat(p); vectors[i].y = rdFloat(p + 4); vectors[i].z = rdFloat(p + 8);
        p += 12;
    }

    const int32_t pointCount = Package::readCompact(p, end);
    std::vector<ot::map::Vec3> points(pointCount);
    for (int32_t i = 0; i < pointCount && p + 12 <= end; ++i) {
        points[i].x = rdFloat(p); points[i].y = rdFloat(p + 4); points[i].z = rdFloat(p + 8);
        p += 12;
    }

    // FBspNode::operator<< (UE1 v432, archive version 68 < 70):
    //   Plane (16), ZoneMask (8), NodeFlags (1), then 7 compact indices
    //   (iVertPool, iSurf, iChild[0..2], iCollisionBound, iRenderBound), then
    //   iZone[2] (2), NumVertices (1), iLeaf[2] as INT (8). No sphere bounds,
    //   no iSection/iFirstVertex/iLightMap for this version.
    struct Node {
        float plane[4];
        uint64_t zoneMask;
        uint8_t flags;
        int32_t vertPool, surf, back, front, iplane, collBound, renderBound;
        uint8_t zone[2];
        uint8_t numVertices;
        int32_t leaf[2];
    };
    const int32_t nodeCount = Package::readCompact(p, end);
    std::vector<Node> nodes(nodeCount);
    for (int32_t i = 0; i < nodeCount && p + 36 <= end; ++i) {
        Node& n = nodes[i];
        for (int k = 0; k < 4; ++k) n.plane[k] = rdFloat(p + k * 4);
        p += 16;
        n.zoneMask = static_cast<uint64_t>(rdu32(p)) | (static_cast<uint64_t>(rdu32(p + 4)) << 32);
        p += 8;
        n.flags = p[0]; p += 1;
        n.vertPool = Package::readCompact(p, end);
        n.surf = Package::readCompact(p, end);
        n.back = Package::readCompact(p, end);
        n.front = Package::readCompact(p, end);
        n.iplane = Package::readCompact(p, end);
        n.collBound = Package::readCompact(p, end);
        n.renderBound = Package::readCompact(p, end);
        n.zone[0] = p[0]; n.zone[1] = p[1]; p += 2;
        n.numVertices = p[0]; p += 1;
        n.leaf[0] = rdi32(p); n.leaf[1] = rdi32(p + 4); p += 8;
    }

    // FBspSurf::operator<< (UE1 v432, archive version 68):
    //   Material (compact), PolyFlags (4), pBase/vNormal/vTextureU/vTextureV
    //   (compact), iLightMap (compact, ver<101), iBrushPoly (compact),
    //   PanU/PanV (SWORD, ver<78), Actor (compact). No Plane (ver<=86),
    //   no LightMapScale (ver<106).
    struct Surf {
        int32_t material;
        uint32_t flags;
        int32_t pBase, vNormal, vTextureU, vTextureV, iLightMap, iBrushPoly;
        int16_t panU, panV;
        int32_t actor;
    };
    const int32_t surfCount = Package::readCompact(p, end);
    std::vector<Surf> surfs(surfCount);
    for (int32_t i = 0; i < surfCount && p + 8 <= end; ++i) {
        Surf& s = surfs[i];
        s.material = Package::readCompact(p, end);
        s.flags = rdu32(p); p += 4;
        s.pBase = Package::readCompact(p, end);
        s.vNormal = Package::readCompact(p, end);
        s.vTextureU = Package::readCompact(p, end);
        s.vTextureV = Package::readCompact(p, end);
        s.iLightMap = Package::readCompact(p, end);
        s.iBrushPoly = Package::readCompact(p, end);
        s.panU = rdi16(p); s.panV = rdi16(p + 2); p += 4;
        s.actor = Package::readCompact(p, end);
    }

    struct Vert {
        int32_t pVertex;
        int32_t iSide;
    };
    const int32_t vertCount = Package::readCompact(p, end);
    std::vector<Vert> verts(vertCount);
    for (int32_t i = 0; i < vertCount && p + 2 <= end; ++i) {
        verts[i].pVertex = Package::readCompact(p, end);
        verts[i].iSide = Package::readCompact(p, end);
    }

    std::printf("[ue1] vectors=%d points=%d nodes=%d surfs=%d verts=%d\n",
                vectorCount, pointCount, nodeCount, surfCount, vertCount);

    // Build the map: UE2/UE1 are Z-up; our engine is Y-up: (x, y, z) -> (x, z, y).
    out.points.reserve(pointCount);
    for (const auto& v : points) {
        out.points.push_back({v.x, v.z, v.y});
    }

    // Materials (dedupe by material name).
    std::vector<std::string> materials;
    auto matIndex = [&](int32_t mat) -> int32_t {
        const std::string name = pkg.resolveIndex(mat);
        for (size_t k = 0; k < materials.size(); ++k) {
            if (materials[k] == name) return static_cast<int32_t>(k);
        }
        materials.push_back(name);
        return static_cast<int32_t>(materials.size()) - 1;
    };

    for (const auto& n : nodes) {
        ot::map::BspNode bn;
        bn.planeX = n.plane[0]; bn.planeY = n.plane[2]; bn.planeZ = n.plane[1]; bn.planeW = n.plane[3];
        bn.vertPool = n.vertPool;
        bn.surf = n.surf;
        bn.vertex = n.vertPool;
        bn.collisionBound = n.collBound;
        bn.zone[0] = static_cast<int8_t>(n.zone[0]);
        bn.zone[1] = static_cast<int8_t>(n.zone[1]);
        bn.leaf[0] = static_cast<int8_t>(n.leaf[0]);
        bn.leaf[1] = static_cast<int8_t>(n.leaf[1]);
        bn.numVertices = n.numVertices;
        bn.nodeFlags = n.flags;
        out.nodes.push_back(bn);
    }

    for (const auto& v : verts) {
        ot::map::BspVert bv;
        bv.pointIndex = v.pVertex;
        bv.side = v.iSide;
        bv.u = 0; bv.v = 0;
        out.verts.push_back(bv);
    }

    for (const auto& s : surfs) {
        ot::map::BspSurface bs;
        bs.materialIndex = matIndex(s.material);
        bs.textureIndex = -1;
        bs.polyFlags = s.flags;
        bs.pBase = s.pBase;
        if (s.vNormal >= 0 && s.vNormal < vectorCount) {
            bs.normalX = vectors[s.vNormal].x;
            bs.normalY = vectors[s.vNormal].z;
            bs.normalZ = vectors[s.vNormal].y;
        }
        if (s.vTextureU >= 0 && s.vTextureU < vectorCount) {
            bs.texUX = vectors[s.vTextureU].x;
            bs.texUY = vectors[s.vTextureU].z;
            bs.texUZ = vectors[s.vTextureU].y;
        }
        if (s.vTextureV >= 0 && s.vTextureV < vectorCount) {
            bs.texVX = vectors[s.vTextureV].x;
            bs.texVY = vectors[s.vTextureV].z;
            bs.texVZ = vectors[s.vTextureV].y;
        }
        bs.brushPoly = s.iBrushPoly;
        bs.actor = s.actor;
        out.surfaces.push_back(bs);
    }

    // Per-vertex UVs from each node's surface texture basis.
    for (const auto& n : nodes) {
        if (n.surf < 0 || n.surf >= surfCount) continue;
        const Surf& s = surfs[n.surf];
        if (s.vTextureU < 0 || s.vTextureU >= vectorCount ||
            s.vTextureV < 0 || s.vTextureV >= vectorCount ||
            s.pBase < 0 || s.pBase >= pointCount) {
            continue;
        }
        const ot::map::Vec3& base = points[s.pBase];
        const ot::map::Vec3& tu = vectors[s.vTextureU];
        const ot::map::Vec3& tv = vectors[s.vTextureV];
        for (uint8_t k = 0; k < n.numVertices; ++k) {
            const int32_t vi = n.vertPool + k;
            if (vi < 0 || vi >= vertCount) continue;
            const int32_t pi = verts[vi].pVertex;
            if (pi < 0 || pi >= pointCount) continue;
            const ot::map::Vec3& pv = points[pi];
            const float dx = pv.x - base.x;
            const float dy = pv.y - base.y;
            const float dz = pv.z - base.z;
            out.verts[vi].u = dx * tu.x + dy * tu.y + dz * tu.z;
            out.verts[vi].v = dx * tv.x + dy * tv.y + dz * tv.z;
        }
    }

    // Player starts.
    const int32_t nameLoc = findNameIndex(pkg, "Location");
    const int32_t nameVec = findNameIndex(pkg, "Vector");
    const int32_t nameRot = findNameIndex(pkg, "Rotation");
    const int32_t nameRotator = findNameIndex(pkg, "Rotator");
    std::vector<PlayerStart> spawns;
    for (int i = 0; i < pkg.exportCount(); ++i) {
        if (pkg.exportClass(i) != "PlayerStart") continue;
        const auto& e = pkg.exp(i);
        if (e.serialSize <= 0) continue;
        ActorTransform at;
        if (parseActorProps(pkg, base, pkg.size(), base + e.serialOffset,
                            static_cast<size_t>(e.serialSize), at)) {
            PlayerStart ps;
            ps.x = at.x; ps.y = at.y; ps.z = at.z; ps.yaw = at.yaw;
            spawns.push_back(ps);
        }
    }

    for (const auto& sd : spawns) {
        out.spawnPoints.push_back({sd.x, sd.z, sd.y});
        out.spawnYaw.push_back(static_cast<float>(sd.yaw) * (6.28318531f / 65536.0f) + 1.57079633f);
    }

    // CTF flag bases (FlagBase with a Team byte: 0 = red, 1 = blue).
    for (int i = 0; i < pkg.exportCount(); ++i) {
        if (pkg.exportClass(i) != "FlagBase") continue;
        const auto& e = pkg.exp(i);
        if (e.serialSize <= 0) continue;
        ActorTransform at;
        if (parseActorProps(pkg, base, pkg.size(), base + e.serialOffset,
                            static_cast<size_t>(e.serialSize), at)) {
            out.flags.push_back({{at.x, at.z, at.y}, at.team});
        }
    }

    out.materials = materials;
    std::strncpy(out.name, pkg.exportName(modelIdx).c_str(), sizeof(out.name) - 1);
    std::printf("[ue1] built %zu points, %zu nodes, %zu surfs, %zu spawns, %zu flags, %zu materials\n",
                out.points.size(), out.nodes.size(), out.surfaces.size(),
                out.spawnPoints.size(), out.flags.size(), out.materials.size());
    return true;
}
