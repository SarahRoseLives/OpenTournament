#include "map/OtMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "game/CollisionWorld.h"

namespace ot::map {

namespace {

// splitmix64: integer-only PRNG, deterministic across platforms.
struct Rng {
    uint64_t state = 0;
    explicit Rng(uint64_t seed) : state(seed) {}

    uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    int nextInt(int n) { // [0, n)
        if (n <= 0) {
            return 0;
        }
        return static_cast<int>(next() % static_cast<uint64_t>(n));
    }

    int nextRange(int lo, int hi) { // [lo, hi]
        return lo + nextInt(hi - lo + 1);
    }
};

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return "";
    }
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool parseFloat(const std::string& s, float& out) {
    const char* p = s.c_str();
    char* end = nullptr;
    out = std::strtof(p, &end);
    return end != p;
}

bool parseInt(const std::string& s, int& out) {
    const char* p = s.c_str();
    char* end = nullptr;
    const long v = std::strtol(p, &end, 10);
    out = static_cast<int>(v);
    return end != p;
}

void pushVertex(std::vector<float>& verts, const glm::vec3& pos, const glm::vec3& color) {
    verts.push_back(pos.x);
    verts.push_back(pos.y);
    verts.push_back(pos.z);
    verts.push_back(color.r);
    verts.push_back(color.g);
    verts.push_back(color.b);
}

void pushQuad(std::vector<float>& verts, const glm::vec3& a, const glm::vec3& b,
              const glm::vec3& c, const glm::vec3& d, const glm::vec3& color) {
    pushVertex(verts, a, color);
    pushVertex(verts, b, color);
    pushVertex(verts, c, color);
    pushVertex(verts, a, color);
    pushVertex(verts, c, color);
    pushVertex(verts, d, color);
}

void pushBox(std::vector<float>& verts, const glm::vec3& min, const glm::vec3& max,
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

    pushQuad(verts, c0, c3, c2, c1, side);
    pushQuad(verts, c5, c6, c7, c4, side);
    pushQuad(verts, c4, c7, c3, c0, side);
    pushQuad(verts, c1, c2, c6, c5, side);
    pushQuad(verts, c3, c7, c6, c2, top);
    pushQuad(verts, c0, c1, c5, c4, bottom);
}

} // namespace

bool parseOtMapText(const std::string& text, GenParams& params, std::string& name) {
    GenParams p;
    std::string n;
    bool haveSeed = false;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        const size_t sp = t.find_first_of(" \t");
        const std::string key = (sp == std::string::npos) ? t : t.substr(0, sp);
        const std::string val = (sp == std::string::npos) ? "" : trim(t.substr(sp + 1));

        if (key == "name") {
            n = val;
            if (n.size() >= 2 && n.front() == '"' && n.back() == '"') {
                n = n.substr(1, n.size() - 2);
            }
        } else if (key == "seed") {
            const char* sp = val.c_str();
            char* end = nullptr;
            const long v = std::strtol(sp, &end, 10);
            if (end != sp) {
                p.seed = static_cast<uint32_t>(v);
                haveSeed = true;
            }
        } else if (key == "arena") {
            float v = 0;
            if (parseFloat(val, v) && v > 1.0f) {
                p.arena = v;
            }
        } else if (key == "obstacles") {
            int v = 0;
            if (parseInt(val, v) && v >= 0) {
                p.obstacles = v;
            }
        } else if (key == "spawns") {
            int v = 0;
            if (parseInt(val, v) && v >= 1) {
                p.spawns = std::min(v, kMaxSpawns);
            }
        }
    }

    if (!haveSeed) {
        return false;
    }
    if (n.empty()) {
        n = "DM-" + std::to_string(p.seed);
    }
    params = p;
    name = n;
    return true;
}

std::string serializeOtMapText(const GenParams& params, const std::string& name) {
    std::string out;
    out += "# OpenTournament Map v1\n";
    out += "name \"" + name + "\"\n";
    out += "seed " + std::to_string(params.seed) + "\n";
    out += "arena " + std::to_string(static_cast<int>(params.arena)) + "\n";
    out += "obstacles " + std::to_string(params.obstacles) + "\n";
    out += "spawns " + std::to_string(params.spawns) + "\n";
    return out;
}

GeneratedMap generate(const GenParams& params) {
    GeneratedMap out;
    out.name = "DM-" + std::to_string(params.seed);

    Rng rng(params.seed);

    // Material palette: floor, wall, crate, pillar, platform.
    const glm::vec3 base[5] = {
        {0.28f, 0.30f, 0.36f},
        {0.22f, 0.24f, 0.30f},
        {0.80f, 0.45f, 0.12f},
        {0.45f, 0.40f, 0.55f},
        {0.40f, 0.70f, 0.30f},
    };
    out.materials.resize(5);
    for (int i = 0; i < 5; ++i) {
        const float jitter = 0.85f + (rng.next() % 256) / 255.0f * 0.3f;
        out.materials[i] = base[i] * jitter;
    }

    const int kMatFloor = 0;
    const int kMatWall = 1;
    const int kMatCrate = 2;
    const int kMatPillar = 3;
    const int kMatPlatform = 4;

    const int A = static_cast<int>(params.arena); // integer half-extent
    const int wallH = 4;
    // World-unit scale: the generator works on a small integer grid and the
    // output is scaled up to match the player's Unreal-scale dimensions.
    constexpr int kScale = 50;

    // Track XZ footprints so cover and spawns avoid overlapping geometry.
    struct Footprint {
        int x0, z0, x1, z1;
    };
    std::vector<Footprint> footprint;

    auto addBox = [&](int x0, int y0, int z0, int x1, int y1, int z1, int mat) {
        out.boxes.push_back({glm::vec3(x0 * kScale, y0 * kScale, z0 * kScale),
                             glm::vec3(x1 * kScale, y1 * kScale, z1 * kScale), mat});
        footprint.push_back({x0, z0, x1, z1});
    };

    // Add a box and its mirror images across the X and Z axes (4-fold symmetry).
    auto addQuad = [&](int x0, int y0, int z0, int x1, int y1, int z1, int mat) {
        addBox(x0, y0, z0, x1, y1, z1, mat);
        addBox(-x1, y0, z0, -x0, y1, z1, mat);
        addBox(x0, y0, -z1, x1, y1, -z0, mat);
        addBox(-x1, y0, -z1, -x0, y1, -z0, mat);
    };

    // Stairs descending in -X from a platform edge (quadrant pieces).
    auto addQuadStairsX = [&](int edgeX, int z0, int z1, int top, int steps, int mat) {
        for (int i = 0; i < steps; ++i) {
            const int h = top - 1 - i;
            if (h <= 0) {
                break;
            }
            addQuad(edgeX - 1 - i, 0, z0, edgeX - i, h, z1, mat);
        }
    };
    // Stairs descending in -Z from a platform edge (quadrant pieces).
    auto addQuadStairsZ = [&](int x0, int x1, int edgeZ, int top, int steps, int mat) {
        for (int i = 0; i < steps; ++i) {
            const int h = top - 1 - i;
            if (h <= 0) {
                break;
            }
            addQuad(x0, 0, edgeZ - 1 - i, x1, h, edgeZ - i, mat);
        }
    };

    auto overlaps = [&](int x0, int z0, int x1, int z1, int margin) {
        for (const auto& f : footprint) {
            if (x0 - margin <= f.x1 && f.x0 <= x1 + margin &&
                z0 - margin <= f.z1 && f.z0 <= z1 + margin) {
                return true;
            }
        }
        return false;
    };

    // --- Floor and perimeter walls (thickness 1) ---
    // Floor is not registered in the footprint: cover sits on top of it.
    out.boxes.push_back({glm::vec3(-A * kScale, -kScale, -A * kScale),
                         glm::vec3(A * kScale, 0, A * kScale), kMatFloor});
    addBox(-A, 0, -A, A, wallH, -A + 1, kMatWall);    // z-min
    addBox(-A, 0, A - 1, A, wallH, A, kMatWall);      // z-max
    addBox(-A, 0, -A, -A + 1, wallH, A, kMatWall);    // x-min
    addBox(A - 1, 0, -A, A, wallH, A, kMatWall);      // x-max

    // --- Corner platforms with stairs (4-fold symmetric) ---
    // Platform occupies [A-15, A-7]^2, top at height 3, with two staircases
    // descending toward the center.
    {
        const int x0 = A - 15;
        const int x1 = A - 7;
        const int top = 3;
        addQuad(x0, 0, x0, x1, top, x1, kMatPlatform);
        addQuadStairsX(x0, x0, x1, top, 2, kMatPlatform); // -X edge
        addQuadStairsZ(x0, x1, x0, top, 2, kMatPlatform); // -Z edge
    }

    // --- Central low platform + corner crates (landmark / cover) ---
    addBox(-5, 0, -5, 5, 1, 5, kMatPlatform);
    addQuad(7, 0, 7, 9, 2, 9, kMatCrate);

    // --- Mid-field cover walls (4-fold symmetric) ---
    addQuad(12, 0, 13, 18, 2, 14, kMatWall);

    // --- Spawn points (symmetric, on the open floor) ---
    std::vector<glm::ivec2> spawnGrid;
    auto addSpawns = [&](int x, int z) {
        spawnGrid.push_back({x, z});
        spawnGrid.push_back({-x, z});
        spawnGrid.push_back({x, -z});
        spawnGrid.push_back({-x, -z});
    };
    addSpawns(A - 5, 10);
    addSpawns(10, A - 5);

    // --- Seeded cover clusters (4-fold symmetric, non-overlapping) ---
    const int clusters = std::clamp(params.obstacles, 0, 16);
    const int lo = 3;
    const int hi = A - 8;
    int placed = 0;
    int guard = 0;
    while (placed < clusters && guard++ < 4000) {
        int w, d, h, mat;
        const int type = rng.nextInt(4);
        if (type == 0) { // horizontal low wall
            w = rng.nextRange(4, 8); d = 1; h = 2; mat = kMatWall;
        } else if (type == 1) { // vertical low wall
            w = 1; d = rng.nextRange(4, 8); h = 2; mat = kMatWall;
        } else if (type == 2) { // crate
            w = 2; d = 2; h = 2; mat = kMatCrate;
        } else { // pillar
            w = 1; d = 1; h = rng.nextRange(3, 4); mat = kMatPillar;
        }

        if (hi - lo < w || hi - lo < d) {
            continue;
        }
        const int x0 = rng.nextRange(lo, hi - w);
        const int z0 = rng.nextRange(lo, hi - d);
        const int x1 = x0 + w;
        const int z1 = z0 + d;

        if (overlaps(x0, z0, x1, z1, 1)) {
            continue;
        }
        // Keep a clear zone around spawn points.
        bool clear = true;
        for (const auto& s : spawnGrid) {
            const int dx = (s.x < x0) ? (x0 - s.x) : (s.x > x1) ? (s.x - x1) : 0;
            const int dz = (s.y < z0) ? (z0 - s.y) : (s.y > z1) ? (s.y - z1) : 0;
            if (dx * dx + dz * dz < 4) {
                clear = false;
                break;
            }
        }
        if (!clear) {
            continue;
        }

        addQuad(x0, 0, z0, x1, h, z1, mat);
        ++placed;
    }

    // --- Spawns (facing toward the arena center) ---
    const int spawnCount = std::clamp(params.spawns, 1, kMaxSpawns);
    for (int i = 0; i < static_cast<int>(spawnGrid.size()) && i < spawnCount; ++i) {
        const glm::ivec2& s = spawnGrid[i];
        Spawn sp;
        sp.position = glm::vec3(static_cast<float>(s.x * kScale), 0.0f,
                                static_cast<float>(s.y * kScale));
        sp.yaw = std::atan2(-static_cast<float>(s.x), static_cast<float>(s.y));
        out.spawns.push_back(sp);
    }

    return out;
}

std::vector<float> buildMesh(const GeneratedMap& map) {
    std::vector<float> verts;
    for (const auto& box : map.boxes) {
        const int mat = box.material;
        const glm::vec3 color =
            (mat >= 0 && mat < static_cast<int>(map.materials.size()))
                ? map.materials[mat]
                : glm::vec3(0.5f, 0.5f, 0.5f);
        pushBox(verts, box.min, box.max, color);
    }
    return verts;
}

void buildCollision(const GeneratedMap& map, CollisionWorld& world) {
    world.clear();
    for (const auto& box : map.boxes) {
        world.addBox(box.min, box.max);
    }
}

} // namespace ot::map
