#include "UE2.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "map/MapFormat.h"

using namespace ot::ue2;

static float rdFloat(const uint8_t* p) {
    uint32_t bits = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                    (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    float f = 0.0f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

static int32_t rdi32(const uint8_t* p) {
    return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
}

static uint32_t rdu32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static void usage() {
    std::fprintf(stderr, "usage: ue2tool dump <file.ut2>\n");
    std::fprintf(stderr, "       ue2tool names <file.ut2> <start> <count>\n");
    std::fprintf(stderr, "       ue2tool hex <file.ut2> <offset> <len>\n");
    std::fprintf(stderr, "       ue2tool extract <file.ut2> [--obj] [--out <path>]\n");
}

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 1;
    }
    const std::string cmd = argv[1];
    const std::string path = argv[2];

    if (cmd == "hex") {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) { std::fprintf(stderr, "cannot open\n"); return 1; }
        long off = argc > 3 ? std::strtol(argv[3], nullptr, 0) : 0;
        long len = argc > 4 ? std::strtol(argv[4], nullptr, 0) : 64;
        std::fseek(f, off, SEEK_SET);
        std::vector<unsigned char> b(len);
        std::fread(b.data(), 1, b.size(), f);
        std::fclose(f);
        for (size_t i = 0; i < b.size(); ++i) {
            std::printf("%02X ", b[i]);
            if ((i + 1) % 16 == 0) std::printf("\n");
        }
        std::printf("\n");
        return 0;
    }

    Package pkg;
    if (!pkg.open(path)) {
        return 1;
    }

    if (cmd == "names") {
        int start = argc > 3 ? std::atoi(argv[3]) : 0;
        int count = argc > 4 ? std::atoi(argv[4]) : 20;
        for (int i = start; i < start + count; ++i) {
            std::printf("[%d] %s\n", i, pkg.name(i).c_str());
        }
        return 0;
    }

    if (cmd == "compact") {
        long off = argc > 3 ? std::strtol(argv[3], nullptr, 0) : 0;
        int count = argc > 4 ? std::atoi(argv[4]) : 30;
        const uint8_t* p = pkg.data() + off;
        const uint8_t* end = pkg.data() + pkg.size();
        for (int i = 0; i < count; ++i) {
            const uint8_t* start = p;
            const int32_t v = Package::readCompact(p, end);
            std::printf("[%d] off=%lld len=%lld val=%d\n", i,
                        (long long)(start - pkg.data()), (long long)(p - start), v);
        }
        return 0;
    }

    if (cmd == "scan") {
        // Find the largest Model export and scan its data for the BSP surface
        // array: 52-byte records where vNormal/vTextureU/vTextureV (int32 at
        // offsets 12/16/20) are small indices into the vector pool (0..207).
        int modelIdx = -1, modelSize = -1;
        for (int i = 0; i < pkg.exportCount(); ++i) {
            if (pkg.exportClass(i) == "Model" && pkg.exp(i).serialSize > modelSize) {
                modelSize = pkg.exp(i).serialSize;
                modelIdx = i;
            }
        }
        if (modelIdx < 0) { std::fprintf(stderr, "no model\n"); return 1; }
        const auto& m = pkg.exp(modelIdx);
        const uint8_t* base = pkg.data() + m.serialOffset;
        const size_t sz = static_cast<size_t>(m.serialSize);
        std::printf("model size=%d\n", m.serialSize);

        for (size_t off = 0; off + 72 <= sz; off += 4) {
            // Vert array: 8-byte records [pVertex in 0..points][iSide in 0..1].
            {
                const int32_t pv = rdi32(base + off);
                const int32_t side = rdi32(base + off + 4);
                if (pv >= 0 && pv < 50000 && (side == 0 || side == 1)) {
                    size_t run = 0;
                    for (size_t k = off; k + 8 <= sz; k += 8) {
                        const int32_t a = rdi32(base + k);
                        const int32_t b = rdi32(base + k + 4);
                        if (!(a >= 0 && a < 50000 && (b == 0 || b == 1))) break;
                        run++;
                    }
                    if (run > 2000) {
                        std::printf("vert run: modelOffset=%zu fileOffset=%lld run=%zu\n",
                                    off, (long long)(m.serialOffset + off), run);
                    }
                }
            }
            // Surf array: vNormal/vTextureU/vTextureV in 0..3000, trailing plane.
            for (int stride : {52, 72, 56, 64}) {
                if (off + stride > sz) continue;
                const int32_t vn = rdi32(base + off + 12);
                const int32_t vu = rdi32(base + off + 16);
                const int32_t vv = rdi32(base + off + 20);
                if (vn >= 0 && vn < 3000 && vu >= 0 && vu < 3000 && vv >= 0 && vv < 3000) {
                    size_t run = 0;
                    for (size_t k = off; k + stride <= sz; k += stride) {
                        const int32_t a = rdi32(base + k + 12);
                        const int32_t b = rdi32(base + k + 16);
                        const int32_t c = rdi32(base + k + 20);
                        if (!(a >= 0 && a < 3000 && b >= 0 && b < 3000 && c >= 0 && c < 3000)) break;
                        run++;
                    }
                    if (run > 1000) {
                        std::printf("surf run: modelOffset=%zu fileOffset=%lld stride=%d run=%zu\n",
                                    off, (long long)(m.serialOffset + off), stride, run);
                    }
                }
            }
        }
        return 0;
    }

    if (cmd == "imports") {
        int count = argc > 3 ? std::atoi(argv[3]) : 30;
        for (int i = 0; i < count && i < pkg.importCount(); ++i) {
            const auto& im = pkg.imp(i);
            std::printf("[%d] classPackage=%s className=%s name=%s package=%d\n",
                        i, pkg.name(im.classPackage).c_str(), pkg.name(im.className).c_str(),
                        pkg.name(im.objectName).c_str(), im.package);
        }
        return 0;
    }

    if (cmd == "dump") {
        std::printf("package: %s\n", path.c_str());
        std::printf("  fileVersion=%u licensee=%u flags=0x%08X\n",
                    pkg.fileVersion(), 0u, 0u);
        std::printf("  nameCount=%u exportCount=%d importCount=%d\n",
                    pkg.nameCount(), pkg.exportCount(), 0);

        std::printf("first 20 exports (raw):\n");
        for (int i = 0; i < 20 && i < pkg.exportCount(); ++i) {
            const auto& e = pkg.exp(i);
            std::printf("  [%d] cls=%d(%s) super=%d(%s) name=%s outer=%d\n",
                        i, e.cls, pkg.exportClass(i).c_str(),
                        e.super, pkg.resolveIndex(e.super).c_str(),
                        pkg.exportName(i).c_str(), e.package);
        }

        std::printf("exports with class Level or Model:\n");
        for (int i = 0; i < pkg.exportCount(); ++i) {
            const std::string cls = pkg.exportClass(i);
            if (cls == "Level" || cls == "Model") {
                const auto& e = pkg.exp(i);
                std::printf("  [%d] class=%-12s super=%-12s name=%-24s outer=%d "
                            "size=%d offset=%d\n",
                            i, cls.c_str(), pkg.resolveIndex(e.super).c_str(),
                            pkg.exportName(i).c_str(), e.package, e.serialSize,
                            e.serialOffset);
            }
        }
        std::printf("done.\n");
        return 0;
    }

    if (cmd == "extract") {
        bool wantObj = false;
        std::string outPath;
        for (int i = 3; i < argc; ++i) {
            if (std::strcmp(argv[i], "--obj") == 0) wantObj = true;
            else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) outPath = argv[++i];
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
            std::fprintf(stderr, "[ue2] no Model export found\n");
            return 1;
        }
        const auto& m = pkg.exp(modelIdx);
        std::printf("Model export [%d] name=%s size=%d offset=%d\n", modelIdx,
                    pkg.exportName(modelIdx).c_str(), m.serialSize, m.serialOffset);

        const uint8_t* base = pkg.data();
        const uint8_t* end = base + pkg.size();
        const uint8_t* p = base + m.serialOffset;
        const uint8_t* stop = p + m.serialSize;

        // UModel::Serialize (UE2/UT2004). Empirically a 42-byte leading run of
        // zero fields, then: Vectors, Points, Nodes, Surfs, Verts,
        // NumSharedSides, RootOutside, RootInside.
        p += 42;

        const int32_t vectorCount = Package::readCompact(p, stop);
        std::vector<ot::map::Vec3> vectors(vectorCount);
        for (int32_t i = 0; i < vectorCount && p + 12 <= stop; ++i) {
            vectors[i].x = rdFloat(p); vectors[i].y = rdFloat(p + 4); vectors[i].z = rdFloat(p + 8);
            p += 12;
        }

        const int32_t pointCount = Package::readCompact(p, stop);
        std::vector<ot::map::Vec3> points(pointCount);
        for (int32_t i = 0; i < pointCount && p + 12 <= stop; ++i) {
            points[i].x = rdFloat(p); points[i].y = rdFloat(p + 4); points[i].z = rdFloat(p + 8);
            p += 12;
        }

        const int32_t nodeCount = Package::readCompact(p, stop);
        struct Node { float px, py, pz, pw; int32_t vertPool, surf, vertex, cb; int8_t zone[2], leaf[2]; uint8_t nv, flags; };
        std::vector<Node> nodes(nodeCount);
        for (int32_t i = 0; i < nodeCount && p + 38 <= stop; ++i) {
            Node& n = nodes[i];
            n.px = rdFloat(p); n.py = rdFloat(p + 4); n.pz = rdFloat(p + 8); n.pw = rdFloat(p + 12);
            n.vertPool = rdi32(p + 16); n.surf = rdi32(p + 20); n.vertex = rdi32(p + 24); n.cb = rdi32(p + 28);
            n.zone[0] = static_cast<int8_t>(p[32]); n.zone[1] = static_cast<int8_t>(p[33]);
            n.leaf[0] = static_cast<int8_t>(p[34]); n.leaf[1] = static_cast<int8_t>(p[35]);
            n.nv = p[36]; n.flags = p[37];
            p += 38;
        }

        const int32_t surfCount = Package::readCompact(p, stop);
        struct Surf { int32_t mat; uint32_t poly; int32_t pBase; int32_t nNormal, nTexU, nTexV; int32_t brush, actor; };
        std::vector<Surf> surfs(surfCount);
        for (int32_t i = 0; i < surfCount && p + 52 <= stop; ++i) {
            surfs[i].mat = rdi32(p);
            surfs[i].poly = rdu32(p + 4);
            surfs[i].pBase = rdi32(p + 8);
            surfs[i].nNormal = rdi32(p + 12);
            surfs[i].nTexU = rdi32(p + 16);
            surfs[i].nTexV = rdi32(p + 20);
            surfs[i].brush = rdi32(p + 24);
            surfs[i].actor = rdi32(p + 28);
            // Plane (16 bytes) at +32..+47
            p += 52;
        }

        const int32_t vertCount = Package::readCompact(p, stop);
        struct Vert { int32_t pi, side; };
        std::vector<Vert> verts(vertCount);
        for (int32_t i = 0; i < vertCount && p + 8 <= stop; ++i) {
            verts[i].pi = rdi32(p);
            verts[i].side = rdi32(p + 4);
            p += 8;
        }

        const long consumed = p - (base + m.serialOffset);
        std::printf("vectors=%d points=%d nodes=%d surfs=%d verts=%d consumed=%ld (of %d)\n",
                    vectorCount, pointCount, nodeCount, surfCount, vertCount, consumed, m.serialSize);

        // Triangulate. node.iVertPool indexes into Verts; FVert.pVertex into Points.
        std::vector<ot::map::Vec3> triVerts;      // triangle vertices (dedup via points)
        std::vector<int32_t> triPointIndex;       // point index per triangle vertex
        std::vector<int32_t> triSurf;             // surface index per triangle
        size_t bad = 0;
        for (const auto& n : nodes) {
            if (n.nv < 3) continue;
            std::vector<int32_t> polyPointIdx;
            bool ok = true;
            for (uint8_t k = 0; k < n.nv; ++k) {
                const int32_t vi = n.vertPool + k;
                if (vi < 0 || vi >= vertCount) { ok = false; ++bad; break; }
                const int32_t pi = verts[vi].pi;
                if (pi < 0 || pi >= pointCount) { ok = false; ++bad; break; }
                polyPointIdx.push_back(pi);
            }
            if (!ok || polyPointIdx.size() < 3) continue;
            for (size_t k = 1; k + 1 < polyPointIdx.size(); ++k) {
                triPointIndex.push_back(polyPointIdx[0]);
                triPointIndex.push_back(polyPointIdx[k]);
                triPointIndex.push_back(polyPointIdx[k + 1]);
                triSurf.push_back(n.surf >= 0 && n.surf < surfCount ? n.surf : -1);
            }
        }
        std::printf("triangles=%zu bad=%zu\n", triPointIndex.size() / 3, bad);

        if (!wantObj && outPath.empty()) {
            return 0;
        }

        // Resolve material names.
        std::vector<std::string> matNames(surfCount);
        for (int32_t i = 0; i < surfCount; ++i) {
            matNames[i] = pkg.resolveIndex(surfs[i].mat);
        }

        if (wantObj) {
            std::string objPath = outPath.empty() ? "out.obj" : outPath;
            FILE* f = std::fopen(objPath.c_str(), "wb");
            if (!f) { std::fprintf(stderr, "cannot write %s\n", objPath.c_str()); return 1; }
            std::fprintf(f, "# OpenTournament UE2 BSP export\n");
            for (const auto& v : points) {
                std::fprintf(f, "v %f %f %f\n", v.x, v.y, v.z);
            }
            int32_t lastSurf = -1;
            for (size_t i = 0; i + 2 < triPointIndex.size(); i += 3) {
                const int32_t s = triSurf[i / 3];
                if (s != lastSurf) {
                    std::fprintf(f, "g %s\n", s >= 0 ? matNames[s].c_str() : "none");
                    lastSurf = s;
                }
                std::fprintf(f, "f %d %d %d\n", triPointIndex[i] + 1,
                             triPointIndex[i + 1] + 1, triPointIndex[i + 2] + 1);
            }
            std::fclose(f);
            std::printf("wrote %s\n", objPath.c_str());
        }

        if (!outPath.empty() && outPath != (wantObj ? outPath : "")) {
            ot::map::Map map;
            std::strncpy(map.name, "DM-Rankin", sizeof(map.name) - 1);
            map.points = points;
            for (const auto& n : nodes) {
                ot::map::BspNode bn;
                bn.planeX = n.px; bn.planeY = n.py; bn.planeZ = n.pz; bn.planeW = n.pw;
                bn.vertPool = n.vertPool; bn.surf = n.surf; bn.vertex = n.vertex;
                bn.collisionBound = n.cb;
                bn.zone[0] = n.zone[0]; bn.zone[1] = n.zone[1];
                bn.leaf[0] = n.leaf[0]; bn.leaf[1] = n.leaf[1];
                bn.numVertices = n.nv; bn.nodeFlags = n.flags;
                map.nodes.push_back(bn);
            }
            for (const auto& v : verts) {
                ot::map::BspVert bv; bv.pointIndex = v.pi; bv.side = v.side;
                map.verts.push_back(bv);
            }
            for (const auto& s : surfs) {
                ot::map::BspSurface bs;
                bs.materialIndex = s.mat; bs.polyFlags = s.poly; bs.pBase = s.pBase;
                bs.brushPoly = s.brush; bs.actor = s.actor;
                map.surfaces.push_back(bs);
            }
            map.materials = matNames;
            if (ot::map::saveMap(map, outPath)) {
                std::printf("wrote map %s\n", outPath.c_str());
            } else {
                std::fprintf(stderr, "failed to write map %s\n", outPath.c_str());
            }
        }

        return 0;
    }

    usage();
    return 1;
}
