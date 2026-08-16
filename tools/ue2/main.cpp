#include "UE2.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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

static uint16_t rdu16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static int32_t findNameIndex(const Package& pkg, const char* name) {
    for (uint32_t i = 0; i < pkg.nameCount(); ++i) {
        if (pkg.name(static_cast<int32_t>(i)) == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// A parsed PlayerStart's spawn data (UE2 Z-up coordinates, rotator yaw units).
struct PlayerStartData {
    float x = 0, y = 0, z = 0;
    int32_t yaw = 0;
};

// Parses a PlayerStart actor's serialized data: the execution-stack state frame
// (RF_HasStack) followed by the tagged-property stream, extracting Location and
// Rotation.
static bool parsePlayerStart(const Package& pkg, const uint8_t* base, size_t baseSize,
                             const uint8_t* p, int32_t size, int32_t nameLoc,
                             int32_t nameVec, int32_t nameRot, int32_t nameRotator,
                             PlayerStartData& out) {
    const uint8_t* hardEnd = base + baseSize;
    const uint8_t* end = p + size;
    if (end > hardEnd) {
        end = hardEnd;
    }

    // State frame: Node, StateNode, ProbeMask(8), LatentAction(4), Offset.
    const int32_t node = Package::readCompact(p, end);
    Package::readCompact(p, end);  // StateNode
    p += 8;                        // ProbeMask (QWORD)
    p += 4;                        // LatentAction (INT)
    if (node != 0) {
        Package::readCompact(p, end);  // Offset
    }

    bool haveLoc = false;
    while (p + 2 <= end) {
        const int32_t name = Package::readCompact(p, end);
        if (name == 0) {
            break;
        }
        const uint8_t info = *p++;
        const uint8_t typ = info & 0x0F;
        int32_t item = -1;
        if (typ == 10) {  // StructProperty: ItemName comes before the size.
            item = Package::readCompact(p, end);
        }
        const uint8_t sc = info & 0x70;
        int32_t vsize;
        if (sc == 0x00) {
            vsize = 1;
        } else if (sc == 0x10) {
            vsize = 2;
        } else if (sc == 0x20) {
            vsize = 4;
        } else if (sc == 0x30) {
            vsize = 12;
        } else if (sc == 0x40) {
            vsize = 16;
        } else if (sc == 0x50) {
            vsize = *p++;
        } else if (sc == 0x60) {
            vsize = rdu16(p);
            p += 2;
        } else {
            vsize = rdi32(p);
            p += 4;
        }
        if ((info & 0x80) && typ != 3) {  // array index (non-bool)
            const uint8_t b = *p++;
            if (b & 0x80) {
                p += ((b & 0xC0) == 0x80) ? 1 : 3;
            }
        }

        if (name == nameLoc && typ == 10 && item == nameVec && p + 12 <= end) {
            out.x = rdFloat(p);
            out.y = rdFloat(p + 4);
            out.z = rdFloat(p + 8);
            p += 12;
            haveLoc = true;
        } else if (name == nameRot && typ == 10 && item == nameRotator && p + 12 <= end) {
            out.yaw = rdi32(p + 4);  // Rotator = (Pitch, Yaw, Roll)
            p += 12;
        } else if (typ == 3) {
            // bool: value is stored in the info byte's array flag, no bytes follow.
        } else {
            p += vsize;
        }
    }
    return haveLoc;
}

// ---- Static mesh (FStaticMesh) parsing ----

struct SMTri {
    float v[9];     // 3 vertices (x,y,z)
    int32_t mat;    // material index into the mesh's Materials array
};

// Recursively skip a UE2 tagged-property stream (terminated by name == 0).
// Mirrors UStruct::SerializeTaggedProperties + FPropertyTag: each tag carries a
// Size field that is the exact byte count of the value, so unknown structs and
// arrays can be skipped without knowing their layout.
static bool skipTagged(const Package& pkg, const uint8_t*& p, const uint8_t* end,
                       int depth, bool verbose) {
    if (depth > 32) {
        return false;
    }
    while (p + 2 <= end) {
        const int32_t name = Package::readCompact(p, end);
        if (name == 0) {
            return true;
        }
        const uint8_t info = *p++;
        const uint8_t typ = info & 0x0F;
        if (typ == 10) {  // StructProperty: struct name follows.
            Package::readCompact(p, end);
        }
        const uint8_t sc = info & 0x70;
        int32_t size = 0;
        if (sc == 0x00) {
            size = 1;
        } else if (sc == 0x10) {
            size = 2;
        } else if (sc == 0x20) {
            size = 4;
        } else if (sc == 0x30) {
            size = 12;
        } else if (sc == 0x40) {
            size = 16;
        } else if (sc == 0x50) {
            if (p >= end) return false;
            size = *p++;
        } else if (sc == 0x60) {
            if (p + 2 > end) return false;
            size = rdu16(p);
            p += 2;
        } else {
            if (p + 4 > end) return false;
            size = rdi32(p);
            p += 4;
        }
        if ((info & 0x80) && typ != 3) {  // array index (non-bool)
            if (p >= end) return false;
            const uint8_t b = *p++;
            if (b & 0x80) {
                p += ((b & 0xC0) == 0x80) ? 1 : 3;
            }
        }
        if (verbose) {
            std::printf("    prop name=%s typ=%d size=%d\n", pkg.name(name).c_str(), typ, size);
        }
        if (typ != 3) {  // bool value is stored in the info byte
            if (size < 0 || p + size > end) return false;
            p += size;
        }
    }
    return false;
}

// Parses one UStaticMesh export into a triangle soup (positions + material
// index). Returns true on success.
static bool parseStaticMesh(const Package& pkg, int exportIndex,
                            std::vector<SMTri>& outTris, int* outSectionCount) {
    const auto& e = pkg.exp(exportIndex);
    if (e.serialSize <= 0) {
        return false;
    }
    const uint8_t* base = pkg.data();
    const uint8_t* p = base + e.serialOffset;
    const uint8_t* end = p + e.serialSize;
    const uint8_t* hardEnd = base + pkg.size();
    if (end > hardEnd) {
        end = hardEnd;
    }

    // 1. UObject property stream (Bounds, BodySetup, Materials, bools, ...).
    if (!skipTagged(pkg, p, end, 0, false)) {
        std::fprintf(stderr, "  [smesh] property skip failed\n");
        return false;
    }
    std::printf("  after props off=%lld (size=%d)\n",
                (long long)(p - (base + e.serialOffset)), e.serialSize);

    // UPrimitive::Serialize serializes BoundingBox (FBox = Min+Max+IsValid = 25
    // bytes) + BoundingSphere (FSphere = FPlane = 16 bytes) natively, after the
    // property stream and before UStaticMesh's Sections/BoundingBox.
    p += 41;

    // 2. Sections.
    const int32_t sectionCount = Package::readCompact(p, end);
    if (sectionCount < 0 || sectionCount > 100000) {
        return false;
    }
    p += static_cast<size_t>(sectionCount) * 14;  // FStaticMeshSection (Ver >= 112)
    if (outSectionCount) {
        *outSectionCount = sectionCount;
    }

    // 3. BoundingBox (FBox = 25 bytes) serialized by UStaticMesh::Serialize.
    p += 25;

    // 4. VertexStream.
    const int32_t vertCount = Package::readCompact(p, end);
    p += static_cast<size_t>(vertCount) * 24;  // FStaticMeshVertex
    p += 4;                                    // Revision

    // 5. ColorStream (FRawColorStream: FColor[count] + Revision).
    const int32_t colorCount = Package::readCompact(p, end);
    p += static_cast<size_t>(colorCount) * 4 + 4;

    // 6. AlphaStream (FRawAlphaStream: FColor[count] + Revision).
    const int32_t alphaCount = Package::readCompact(p, end);
    p += static_cast<size_t>(alphaCount) * 4 + 4;

    // 7. UVStreams.
    const int32_t uvStreamCount = Package::readCompact(p, end);
    for (int32_t i = 0; i < uvStreamCount; ++i) {
        const int32_t uvCount = Package::readCompact(p, end);
        p += static_cast<size_t>(uvCount) * 8 + 4 + 4;  // UVs + CoordinateIndex + Revision
    }

    // 8. IndexBuffer (FRawIndexBuffer: _WORD[count] + Revision).
    const int32_t idxCount = Package::readCompact(p, end);
    p += static_cast<size_t>(idxCount) * 2 + 4;

    // 9. WireframeIndexBuffer.
    const int32_t wfIdxCount = Package::readCompact(p, end);
    p += static_cast<size_t>(wfIdxCount) * 2 + 4;

    // 10. CollisionModel (UModel* object reference).
    Package::readCompact(p, end);

    // 11. kDOPTree.
    const int32_t nodeCount = Package::readCompact(p, end);
    p += static_cast<size_t>(nodeCount) * 32;  // FkDOPNode
    const int32_t kdopTriCount = Package::readCompact(p, end);
    p += static_cast<size_t>(kdopTriCount) * 8;  // FkDOPCollisionTriangle

    std::printf("  verts=%d colors=%d alpha=%d uv=%d idx=%d sections=%d kdopNodes=%d\n",
                vertCount, colorCount, alphaCount, uvStreamCount, idxCount,
                sectionCount, nodeCount);

    // 12. RawTriangles (TLazyArray<FStaticMeshTriangle>): endOffset + count + tris.
    p += 4;  // end offset
    const int32_t triCount = Package::readCompact(p, end);
    if (triCount < 0 || triCount > 10000000) {
        return false;
    }
    outTris.reserve(static_cast<size_t>(triCount));
    for (int32_t i = 0; i < triCount; ++i) {
        if (p + 60 > end) {
            return false;
        }
        SMTri t;
        for (int k = 0; k < 9; ++k) {
            t.v[k] = rdFloat(p);
            p += 4;
        }
        const int32_t numUVs = rdi32(p);
        p += 4;
        p += static_cast<size_t>(numUVs) * 24;  // UVs[3][numUVs]
        p += 12;                                // Colors[3]
        t.mat = rdi32(p);
        p += 4;
        p += 4;  // SmoothingMask
        outTris.push_back(t);
    }
    return true;
}

// ---- Static mesh placement + resolution ----

struct SMPlacement {
    int32_t mesh;       // FPackageIndex: >0 export, <0 import
    float loc[3];       // UE2 Z-up location
    float scale[3];
    int32_t pitch, yaw, roll;  // rotator units (65536 = full turn)
};

// Resolve a static mesh FPackageIndex to (Package, export index). Returns the
// package that owns the mesh (the input package for exports, a loaded .usx for
// imports) and the export index of the StaticMesh within it.
static bool resolveStaticMesh(const Package& mainPkg, int32_t mesh,
                              const Package*& outPkg, int* outExport,
                              std::vector<std::unique_ptr<Package>>& cache) {
    if (mesh > 0) {
        const int32_t idx = mesh - 1;
        if (idx < mainPkg.exportCount() && mainPkg.exportClass(idx) == "StaticMesh") {
            outPkg = &mainPkg;
            *outExport = idx;
            return true;
        }
        return false;
    }
    if (mesh == 0) {
        return false;
    }
    const int32_t imp = -mesh - 1;
    if (imp < 0 || imp >= mainPkg.importCount()) {
        return false;
    }
    // The import's "package" field names the owning package (another import).
    const auto& im = mainPkg.imp(imp);
    const std::string pkgName = mainPkg.resolveIndex(im.package);
    if (pkgName.empty() || pkgName == "None") {
        return false;
    }
    const std::string meshName = mainPkg.resolveIndex(im.objectName);

    const std::string usxPath = "C:\\UT2004\\StaticMeshes\\" + pkgName + ".usx";
    for (auto& cached : cache) {
        for (int i = 0; i < cached->exportCount(); ++i) {
            if (cached->exportClass(i) == "StaticMesh" && cached->exportName(i) == meshName) {
                outPkg = cached.get();
                *outExport = i;
                return true;
            }
        }
    }
    auto p = std::make_unique<Package>();
    if (!p->open(usxPath)) {
        return false;
    }
    for (int i = 0; i < p->exportCount(); ++i) {
        if (p->exportClass(i) == "StaticMesh" && p->exportName(i) == meshName) {
            outPkg = p.get();
            *outExport = i;
            cache.push_back(std::move(p));
            return true;
        }
    }
    return false;
}

// UE2 FRotator -> 3x3 rotation matrix (columns = X, Y, Z axes). UE2 Z-up.
static void rotatorMatrix(int32_t pitch, int32_t yaw, int32_t roll, float m[9]) {
    const float k = 6.28318531f / 65536.0f;
    const float sp = std::sin(static_cast<float>(pitch) * k);
    const float cp = std::cos(static_cast<float>(pitch) * k);
    const float sy = std::sin(static_cast<float>(yaw) * k);
    const float cy = std::cos(static_cast<float>(yaw) * k);
    const float sr = std::sin(static_cast<float>(roll) * k);
    const float cr = std::cos(static_cast<float>(roll) * k);
    // Columns (X, Y, Z) matching UT2004 FRotator ordering.
    m[0] = cy * cr + sy * sp * sr;
    m[1] = sy * cr - cy * sp * sr;
    m[2] = -cp * sr;
    m[3] = -sy * cp;
    m[4] = cy * cp;
    m[5] = sp;
    m[6] = cy * sr - sy * sp * cr;
    m[7] = sy * sr + cy * sp * cr;
    m[8] = cp * cr;
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

    if (cmd == "classes") {
        for (int i = 0; i < pkg.exportCount(); ++i) {
            std::printf("[%d] cls=%s name=%s outer=%d size=%d off=%d flags=0x%08X\n", i,
                        pkg.exportClass(i).c_str(), pkg.exportName(i).c_str(),
                        pkg.exp(i).package, pkg.exp(i).serialSize,
                        pkg.exp(i).serialOffset, pkg.exp(i).objectFlags);
        }
        return 0;
    }

    if (cmd == "smactors") {
        const int32_t nameLoc = findNameIndex(pkg, "Location");
        const int32_t nameSM = findNameIndex(pkg, "StaticMesh");
        const int32_t nameVec = findNameIndex(pkg, "Vector");
        const int32_t nameScale = findNameIndex(pkg, "DrawScale3D");
        std::printf("names: loc=%d sm=%d vec=%d scale3d=%d\n",
                    nameLoc, nameSM, nameVec, nameScale);

        for (int i = 0; i < pkg.exportCount(); ++i) {
            const std::string cls = pkg.exportClass(i);
            if (cls != "StaticMeshActor" && cls != "StaticMeshInstance") {
                continue;
            }
            const auto& e = pkg.exp(i);
            if (e.serialSize <= 0) {
                continue;
            }
            const uint8_t* base = pkg.data();
            const uint8_t* p = base + e.serialOffset;
            const uint8_t* end = p + e.serialSize;
            const uint8_t* hardEnd = base + pkg.size();
            if (end > hardEnd) {
                end = hardEnd;
            }

            if (e.objectFlags & 0x02000000) {  // RF_HasStack
                const int32_t node = Package::readCompact(p, end);
                Package::readCompact(p, end);  // StateNode
                p += 8;                        // ProbeMask
                p += 4;                        // LatentAction
                if (node != 0) {
                    Package::readCompact(p, end);  // Offset
                }
            }

            float loc[3] = {0, 0, 0};
            float scale[3] = {1, 1, 1};
            int32_t mesh = 0;
            while (p + 2 <= end) {
                const int32_t name = Package::readCompact(p, end);
                if (name == 0) {
                    break;
                }
                const uint8_t info = *p++;
                const uint8_t typ = info & 0x0F;
                int32_t item = -1;
                if (typ == 10) {
                    item = Package::readCompact(p, end);
                }
                const uint8_t sc = info & 0x70;
                int32_t vsize;
                if (sc == 0x00) {
                    vsize = 1;
                } else if (sc == 0x10) {
                    vsize = 2;
                } else if (sc == 0x20) {
                    vsize = 4;
                } else if (sc == 0x30) {
                    vsize = 12;
                } else if (sc == 0x40) {
                    vsize = 16;
                } else if (sc == 0x50) {
                    vsize = *p++;
                } else if (sc == 0x60) {
                    vsize = rdu16(p);
                    p += 2;
                } else {
                    vsize = rdi32(p);
                    p += 4;
                }
                if ((info & 0x80) && typ != 3) {
                    const uint8_t b = *p++;
                    if (b & 0x80) {
                        p += ((b & 0xC0) == 0x80) ? 1 : 3;
                    }
                }

                if (name == nameLoc && typ == 10 && item == nameVec && p + 12 <= end) {
                    loc[0] = rdFloat(p);
                    loc[1] = rdFloat(p + 4);
                    loc[2] = rdFloat(p + 8);
                    p += 12;
                } else if (name == nameSM && typ == 5) {
                    mesh = Package::readCompact(p, end);
                } else if (name == nameScale && typ == 10 && item == nameVec && p + 12 <= end) {
                    scale[0] = rdFloat(p);
                    scale[1] = rdFloat(p + 4);
                    scale[2] = rdFloat(p + 8);
                    p += 12;
                } else if (typ == 5) {
                    Package::readCompact(p, end);
                } else if (typ == 3) {
                    // bool: value stored in info byte, no bytes follow.
                } else {
                    p += vsize;
                }
            }
            std::printf("[%d] %s loc=(%.0f %.0f %.0f) scale=(%.2f %.2f %.2f) mesh=%d(%s)\n",
                        i, pkg.exportName(i).c_str(), loc[0], loc[1], loc[2],
                        scale[0], scale[1], scale[2], mesh, pkg.resolveIndex(mesh).c_str());
        }
        return 0;
    }

    if (cmd == "smesh") {
        int idx = argc > 3 ? std::atoi(argv[3]) : -1;
        if (idx < 0) {
            for (int i = 0; i < pkg.exportCount(); ++i) {
                if (pkg.exportClass(i) == "StaticMesh") {
                    std::printf("[%d] %s\n", i, pkg.exportName(i).c_str());
                }
            }
            return 0;
        }
        std::printf("parsing static mesh [%d] %s\n", idx, pkg.exportName(idx).c_str());
        std::vector<SMTri> tris;
        int sections = 0;
        if (!parseStaticMesh(pkg, idx, tris, &sections)) {
            std::fprintf(stderr, "failed\n");
            return 1;
        }
        std::printf("sections=%d triangles=%zu\n", sections, tris.size());
        float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
        for (const auto& t : tris) {
            for (int k = 0; k < 3; ++k) {
                for (int c = 0; c < 3; ++c) {
                    const float v = t.v[k * 3 + c];
                    mn[c] = mn[c] < v ? mn[c] : v;
                    mx[c] = mx[c] > v ? mx[c] : v;
                }
            }
        }
        std::printf("bounds min=(%.0f %.0f %.0f) max=(%.0f %.0f %.0f)\n",
                    mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
        for (size_t i = 0; i < tris.size() && i < 3; ++i) {
            std::printf("  tri[%zu] (%.0f %.0f %.0f) (%.0f %.0f %.0f) (%.0f %.0f %.0f) mat=%d\n",
                        i, tris[i].v[0], tris[i].v[1], tris[i].v[2],
                        tris[i].v[3], tris[i].v[4], tris[i].v[5],
                        tris[i].v[6], tris[i].v[7], tris[i].v[8], tris[i].mat);
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
        const uint8_t* p = base + m.serialOffset;
        const uint8_t* modelEnd = p + m.serialSize;

        auto readU64 = [&](const uint8_t* q) {
            return static_cast<uint64_t>(rdu32(q)) | (static_cast<uint64_t>(rdu32(q + 4)) << 32);
        };

        // UModel::Serialize (UT2004): 42-byte header (Super + Bounds), then
        // Vectors, Points, Nodes, Surfs, Verts, NumSharedSides, NumZones, ...
        // FBspNode uses compact (variable-length) indices and a BYTE NodeFlags.
        p += 42;

        const int32_t vectorCount = Package::readCompact(p, modelEnd);
        std::vector<ot::map::Vec3> vectors(vectorCount);
        for (int32_t i = 0; i < vectorCount && p + 12 <= modelEnd; ++i) {
            vectors[i].x = rdFloat(p); vectors[i].y = rdFloat(p + 4); vectors[i].z = rdFloat(p + 8);
            p += 12;
        }

        const int32_t pointCount = Package::readCompact(p, modelEnd);
        std::vector<ot::map::Vec3> points(pointCount);
        for (int32_t i = 0; i < pointCount && p + 12 <= modelEnd; ++i) {
            points[i].x = rdFloat(p); points[i].y = rdFloat(p + 4); points[i].z = rdFloat(p + 8);
            p += 12;
        }

        // FBspNode::operator<< (UT2004): Plane, ZoneMask, NodeFlags(BYTE), then
        // 7 compact indices, ExclusiveSphereBound(FSphere), iZone, NumVertices,
        // iLeaf, iSection, iFirstVertex, iLightMap.
        struct Node {
            float plane[4];
            uint64_t zoneMask;
            uint8_t flags;
            int32_t vertPool, surf, back, front, iplane, collBound, renderBound;
            float sphere[4];
            uint8_t zone[2];
            uint8_t numVertices;
            int32_t leaf[2];
            int32_t iSection, iFirstVertex, iLightMap;
        };

        const int32_t nodeCount = Package::readCompact(p, modelEnd);
        std::vector<Node> nodes(nodeCount);
        for (int32_t i = 0; i < nodeCount && p + 24 <= modelEnd; ++i) {
            Node& n = nodes[i];
            for (int k = 0; k < 4; ++k) n.plane[k] = rdFloat(p + k * 4);
            p += 16;
            n.zoneMask = readU64(p); p += 8;
            n.flags = p[0]; p += 1;
            n.vertPool = Package::readCompact(p, modelEnd);
            n.surf = Package::readCompact(p, modelEnd);
            n.back = Package::readCompact(p, modelEnd);
            n.front = Package::readCompact(p, modelEnd);
            n.iplane = Package::readCompact(p, modelEnd);
            n.collBound = Package::readCompact(p, modelEnd);
            n.renderBound = Package::readCompact(p, modelEnd);
            for (int k = 0; k < 4; ++k) n.sphere[k] = rdFloat(p + k * 4);
            p += 16;
            n.zone[0] = p[0]; n.zone[1] = p[1]; p += 2;
            n.numVertices = p[0]; p += 1;
            n.leaf[0] = rdi32(p); n.leaf[1] = rdi32(p + 4); p += 8;
            n.iSection = rdi32(p); n.iFirstVertex = rdi32(p + 4); n.iLightMap = rdi32(p + 8);
            p += 12;
        }

        // FBspSurf::operator<< (UT2004): Material, PolyFlags, pBase, vNormal,
        // vTextureU, vTextureV, iBrushPoly, Actor, Plane, LightMapScale.
        struct Surf {
            int32_t material;
            uint32_t flags;
            int32_t pBase, vNormal, vTextureU, vTextureV, iBrushPoly, owner;
            float plane[4];
            float lightMapScale;
        };
        const int32_t surfCount = Package::readCompact(p, modelEnd);
        std::vector<Surf> surfs(surfCount);
        for (int32_t i = 0; i < surfCount && p + 8 <= modelEnd; ++i) {
            Surf& s = surfs[i];
            s.material = Package::readCompact(p, modelEnd);
            s.flags = rdu32(p); p += 4;
            s.pBase = Package::readCompact(p, modelEnd);
            s.vNormal = Package::readCompact(p, modelEnd);
            s.vTextureU = Package::readCompact(p, modelEnd);
            s.vTextureV = Package::readCompact(p, modelEnd);
            s.iBrushPoly = Package::readCompact(p, modelEnd);
            s.owner = Package::readCompact(p, modelEnd);
            for (int k = 0; k < 4; ++k) s.plane[k] = rdFloat(p + k * 4);
            p += 16;
            s.lightMapScale = rdFloat(p); p += 4;
        }

        // FVert::operator<< (UT2004): AR_INDEX(pVertex) << AR_INDEX(iSide).
        struct Vert { int32_t pi, side; };
        const int32_t vertCount = Package::readCompact(p, modelEnd);
        std::vector<Vert> verts(vertCount);
        for (int32_t i = 0; i < vertCount && p < modelEnd; ++i) {
            verts[i].pi = Package::readCompact(p, modelEnd);
            verts[i].side = Package::readCompact(p, modelEnd);
        }

        const long consumed = p - (base + m.serialOffset);
        std::printf("vectors=%d points=%d nodes=%d surfs=%d verts=%d consumed=%ld (of %d)\n",
                    vectorCount, pointCount, nodeCount, surfCount, vertCount, consumed, m.serialSize);

        // Triangulate. node.vertPool indexes into Verts; FVert.pVertex into Points.
        std::vector<int32_t> triPointIndex;
        std::vector<int32_t> triSurf;
        size_t bad = 0;
        for (const auto& n : nodes) {
            if (n.numVertices < 3) continue;
            std::vector<int32_t> polyPointIdx;
            bool ok = true;
            for (uint8_t k = 0; k < n.numVertices; ++k) {
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

        // Extract PlayerStart spawn points from the level's actor list.
        const int32_t nameLoc = findNameIndex(pkg, "Location");
        const int32_t nameVec = findNameIndex(pkg, "Vector");
        const int32_t nameRot = findNameIndex(pkg, "Rotation");
        const int32_t nameRotator = findNameIndex(pkg, "Rotator");
        std::vector<PlayerStartData> spawns;
        if (nameLoc >= 0 && nameVec >= 0 && nameRot >= 0 && nameRotator >= 0) {
            for (int i = 0; i < pkg.exportCount(); ++i) {
                const std::string cls = pkg.exportClass(i);
                if (cls != "PlayerStart" && cls != "xPlayerStart") {
                    continue;
                }
                const auto& e = pkg.exp(i);
                if (e.serialSize <= 0) {
                    continue;
                }
                PlayerStartData sd;
                if (parsePlayerStart(pkg, base, pkg.size(), base + e.serialOffset,
                                     e.serialSize, nameLoc, nameVec, nameRot,
                                     nameRotator, sd)) {
                    spawns.push_back(sd);
                }
            }
        }
        std::printf("player starts=%zu\n", spawns.size());

        if (!wantObj && outPath.empty()) {
            return 0;
        }

        std::vector<std::string> matNames(surfCount);
        for (int32_t i = 0; i < surfCount; ++i) {
            matNames[i] = pkg.resolveIndex(surfs[i].material);
        }

        if (wantObj) {
            std::string objPath = "out.obj";
            if (!outPath.empty()) {
                objPath = outPath;
                const size_t dot = objPath.find_last_of('.');
                if (dot != std::string::npos) {
                    objPath = objPath.substr(0, dot) + ".obj";
                }
            }
            FILE* f = std::fopen(objPath.c_str(), "wb");
            if (!f) { std::fprintf(stderr, "cannot write %s\n", objPath.c_str()); return 1; }
            std::fprintf(f, "# OpenTournament UE2 BSP export\n");
            for (const auto& v : points) {
                // UE2 is Z-up; our engine is Y-up: x->x, y->z, z->y.
                std::fprintf(f, "v %f %f %f\n", v.x, v.z, v.y);
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

        if (!outPath.empty()) {
            // Deduplicate materials and map each surface to its material index.
            std::vector<std::string> materials;
            std::vector<int32_t> surfMat(surfCount);
            for (int32_t i = 0; i < surfCount; ++i) {
                const std::string& name = matNames[i];
                int32_t idx = -1;
                for (size_t k = 0; k < materials.size(); ++k) {
                    if (materials[k] == name) { idx = static_cast<int32_t>(k); break; }
                }
                if (idx < 0) { idx = static_cast<int32_t>(materials.size()); materials.push_back(name); }
                surfMat[i] = idx;
            }

            ot::map::Map map;
            std::string mapName = path;
            const size_t slash = mapName.find_last_of("/\\");
            if (slash != std::string::npos) {
                mapName = mapName.substr(slash + 1);
            }
            const size_t dot = mapName.find_last_of('.');
            if (dot != std::string::npos) {
                mapName = mapName.substr(0, dot);
            }
            std::strncpy(map.name, mapName.c_str(), sizeof(map.name) - 1);
            // UE2 is Z-up; our engine is Y-up: x->x, y->z, z->y.
            map.points.reserve(points.size());
            for (const auto& v : points) {
                map.points.push_back({v.x, v.z, v.y});
            }
            for (const auto& n : nodes) {
                ot::map::BspNode bn;
                bn.planeX = n.plane[0]; bn.planeY = n.plane[2]; bn.planeZ = n.plane[1]; bn.planeW = n.plane[3];
                bn.vertPool = n.vertPool; bn.surf = n.surf; bn.vertex = n.vertPool;
                bn.collisionBound = n.collBound;
                bn.zone[0] = static_cast<int8_t>(n.zone[0]); bn.zone[1] = static_cast<int8_t>(n.zone[1]);
                bn.leaf[0] = static_cast<int8_t>(n.leaf[0]); bn.leaf[1] = static_cast<int8_t>(n.leaf[1]);
                bn.numVertices = n.numVertices; bn.nodeFlags = n.flags;
                map.nodes.push_back(bn);
            }
            for (const auto& v : verts) {
                ot::map::BspVert bv; bv.pointIndex = v.pi; bv.side = v.side;
                map.verts.push_back(bv);
            }
            for (int32_t i = 0; i < surfCount; ++i) {
                const Surf& s = surfs[i];
                ot::map::BspSurface bs;
                bs.materialIndex = surfMat[i]; bs.polyFlags = s.flags; bs.pBase = s.pBase;
                if (s.vNormal >= 0 && s.vNormal < vectorCount) {
                    bs.normalX = vectors[s.vNormal].x; bs.normalY = vectors[s.vNormal].z; bs.normalZ = vectors[s.vNormal].y;
                }
                if (s.vTextureU >= 0 && s.vTextureU < vectorCount) {
                    bs.texUX = vectors[s.vTextureU].x; bs.texUY = vectors[s.vTextureU].z; bs.texUZ = vectors[s.vTextureU].y;
                }
                if (s.vTextureV >= 0 && s.vTextureV < vectorCount) {
                    bs.texVX = vectors[s.vTextureV].x; bs.texVY = vectors[s.vTextureV].z; bs.texVZ = vectors[s.vTextureV].y;
                }
                bs.brushPoly = s.iBrushPoly; bs.actor = s.owner;
                bs.planeX = s.plane[0]; bs.planeY = s.plane[2]; bs.planeZ = s.plane[1]; bs.planeW = s.plane[3];
                map.surfaces.push_back(bs);
            }

            // ---- Static meshes: append each placed mesh's triangles as BSP
            // nodes (3-vertex polygons) so they render and collide identically
            // to the BSP geometry.
            const int32_t nameSM = findNameIndex(pkg, "StaticMesh");
            const int32_t nameScale3d = findNameIndex(pkg, "DrawScale3D");
            std::vector<std::unique_ptr<Package>> smPackages;
            size_t smTriangles = 0;
            size_t smActors = 0;
            size_t smFailed = 0;
            if (nameLoc >= 0 && nameVec >= 0 && nameRot >= 0 && nameRotator >= 0 &&
                nameSM >= 0) {
                for (int i = 0; i < pkg.exportCount(); ++i) {
                    if (pkg.exportClass(i) != "StaticMeshActor") {
                        continue;
                    }
                    const auto& e = pkg.exp(i);
                    if (e.serialSize <= 0) {
                        continue;
                    }
                    const uint8_t* ap = pkg.data() + e.serialOffset;
                    const uint8_t* aend = ap + e.serialSize;
                    const uint8_t* hardEnd = pkg.data() + pkg.size();
                    if (aend > hardEnd) {
                        aend = hardEnd;
                    }
                    if (e.objectFlags & 0x02000000) {  // RF_HasStack
                        const int32_t st = Package::readCompact(ap, aend);
                        Package::readCompact(ap, aend);  // StateNode
                        ap += 8;                         // ProbeMask
                        ap += 4;                         // LatentAction
                        if (st != 0) {
                            Package::readCompact(ap, aend);  // Offset
                        }
                    }
                    SMPlacement pl;
                    pl.mesh = 0;
                    while (ap + 2 <= aend) {
                        const int32_t name = Package::readCompact(ap, aend);
                        if (name == 0) {
                            break;
                        }
                        const uint8_t info = *ap++;
                        const uint8_t typ = info & 0x0F;
                        int32_t item = -1;
                        if (typ == 10) {
                            item = Package::readCompact(ap, aend);
                        }
                        const uint8_t sc = info & 0x70;
                        int32_t vsize;
                        if (sc == 0x00) {
                            vsize = 1;
                        } else if (sc == 0x10) {
                            vsize = 2;
                        } else if (sc == 0x20) {
                            vsize = 4;
                        } else if (sc == 0x30) {
                            vsize = 12;
                        } else if (sc == 0x40) {
                            vsize = 16;
                        } else if (sc == 0x50) {
                            if (ap >= aend) break;
                            vsize = *ap++;
                        } else if (sc == 0x60) {
                            if (ap + 2 > aend) break;
                            vsize = rdu16(ap);
                            ap += 2;
                        } else {
                            if (ap + 4 > aend) break;
                            vsize = rdi32(ap);
                            ap += 4;
                        }
                        if ((info & 0x80) && typ != 3) {
                            const uint8_t b = *ap++;
                            if (b & 0x80) {
                                ap += ((b & 0xC0) == 0x80) ? 1 : 3;
                            }
                        }
                        if (name == nameLoc && typ == 10 && item == nameVec && ap + 12 <= aend) {
                            pl.loc[0] = rdFloat(ap);
                            pl.loc[1] = rdFloat(ap + 4);
                            pl.loc[2] = rdFloat(ap + 8);
                            ap += 12;
                        } else if (name == nameRot && typ == 10 && item == nameRotator && ap + 12 <= aend) {
                            pl.pitch = rdi32(ap);
                            pl.yaw = rdi32(ap + 4);
                            pl.roll = rdi32(ap + 8);
                            ap += 12;
                        } else if (name == nameSM && typ == 5) {
                            pl.mesh = Package::readCompact(ap, aend);
                        } else if (name == nameScale3d && typ == 10 && item == nameVec && ap + 12 <= aend) {
                            pl.scale[0] = rdFloat(ap);
                            pl.scale[1] = rdFloat(ap + 4);
                            pl.scale[2] = rdFloat(ap + 8);
                            ap += 12;
                        } else if (typ == 5) {
                            Package::readCompact(ap, aend);
                        } else if (typ == 3) {
                        } else {
                            ap += vsize;
                        }
                    }
                    if (pl.mesh == 0) {
                        continue;
                    }
                    const Package* smPkg = nullptr;
                    int smExport = -1;
                    if (!resolveStaticMesh(pkg, pl.mesh, smPkg, &smExport, smPackages)) {
                        ++smFailed;
                        continue;
                    }
                    std::vector<SMTri> tris;
                    if (!parseStaticMesh(*smPkg, smExport, tris, nullptr)) {
                        ++smFailed;
                        continue;
                    }

                    const std::string matName = smPkg->exportName(smExport);
                    int32_t matIdx = -1;
                    for (size_t k = 0; k < materials.size(); ++k) {
                        if (materials[k] == matName) {
                            matIdx = static_cast<int32_t>(k);
                            break;
                        }
                    }
                    if (matIdx < 0) {
                        matIdx = static_cast<int32_t>(materials.size());
                        materials.push_back(matName);
                    }

                    ot::map::BspSurface bs;
                    bs.materialIndex = matIdx;
                    bs.polyFlags = 0;
                    map.surfaces.push_back(bs);
                    const int32_t surfIdx = static_cast<int32_t>(map.surfaces.size()) - 1;

                    float rm[9];
                    rotatorMatrix(pl.pitch, pl.yaw, pl.roll, rm);

                    for (const auto& t : tris) {
                        for (int v = 0; v < 3; ++v) {
                            const float lx = t.v[v * 3 + 0] * pl.scale[0];
                            const float ly = t.v[v * 3 + 1] * pl.scale[1];
                            const float lz = t.v[v * 3 + 2] * pl.scale[2];
                            const float wx = rm[0] * lx + rm[3] * ly + rm[6] * lz + pl.loc[0];
                            const float wy = rm[1] * lx + rm[4] * ly + rm[7] * lz + pl.loc[1];
                            const float wz = rm[2] * lx + rm[5] * ly + rm[8] * lz + pl.loc[2];
                            // UE2 Z-up -> engine Y-up: (x, y, z) -> (x, z, y).
                            map.points.push_back({wx, wz, wy});
                            ot::map::BspVert bv;
                            bv.pointIndex = static_cast<int32_t>(map.points.size()) - 1;
                            bv.side = 0;
                            map.verts.push_back(bv);
                        }
                        ot::map::BspNode bn;
                        bn.vertPool = static_cast<int32_t>(map.verts.size()) - 3;
                        bn.surf = surfIdx;
                        bn.vertex = bn.vertPool;
                        bn.numVertices = 3;
                        bn.nodeFlags = 0;
                        map.nodes.push_back(bn);
                        ++smTriangles;
                    }
                    ++smActors;
                }
            }
            std::printf("static meshes: actors=%zu triangles=%zu failed=%zu\n",
                        smActors, smTriangles, smFailed);

            map.materials = materials;

            // Player starts: UE2 Z-up -> our Y-up (x, y, z) -> (x, z, y), and
            // rotator yaw units (65536 = full turn) -> radians (+90 deg remap).
            for (const auto& sd : spawns) {
                map.spawnPoints.push_back({sd.x, sd.z, sd.y});
                map.spawnYaw.push_back(static_cast<float>(sd.yaw) * (6.28318531f / 65536.0f) +
                                       1.57079633f);
            }

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
