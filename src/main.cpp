#include <SDL.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/Platform.h"
#include "game/BrushCollisionWorld.h"
#include "game/Level.h"
#include "game/Player.h"
#include "game/Weapon.h"
#include "input/Input.h"
#include "map/OtMap.h"
#include "map/QuakeMap.h"
#include "net/Client.h"
#include "net/Server.h"
#include "render/Mesh.h"
#include "render/Renderer.h"

namespace {

using Clock = std::chrono::steady_clock;

double nowSeconds() {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

void pushVertex(std::vector<float>& v, const glm::vec3& p, const glm::vec3& c) {
    v.push_back(p.x);
    v.push_back(p.y);
    v.push_back(p.z);
    v.push_back(c.r);
    v.push_back(c.g);
    v.push_back(c.b);
}

void pushQuad(std::vector<float>& v, const glm::vec3& a, const glm::vec3& b,
              const glm::vec3& c, const glm::vec3& d, const glm::vec3& color) {
    pushVertex(v, a, color);
    pushVertex(v, b, color);
    pushVertex(v, c, color);
    pushVertex(v, a, color);
    pushVertex(v, c, color);
    pushVertex(v, d, color);
}

std::vector<float> buildCenteredBox(float hx, float hy, float hz, const glm::vec3& color) {
    std::vector<float> v;
    const glm::vec3 c0(-hx, -hy, -hz);
    const glm::vec3 c1(hx, -hy, -hz);
    const glm::vec3 c2(hx, hy, -hz);
    const glm::vec3 c3(-hx, hy, -hz);
    const glm::vec3 c4(-hx, -hy, hz);
    const glm::vec3 c5(hx, -hy, hz);
    const glm::vec3 c6(hx, hy, hz);
    const glm::vec3 c7(-hx, hy, hz);

    const glm::vec3 side = color * 0.8f;
    pushQuad(v, c0, c3, c2, c1, side);
    pushQuad(v, c5, c6, c7, c4, side);
    pushQuad(v, c4, c7, c3, c0, side);
    pushQuad(v, c1, c2, c6, c5, side);
    pushQuad(v, c3, c7, c6, c2, color);
    pushQuad(v, c0, c1, c5, c4, color * 0.5f);
    return v;
}

void pushQuad2D(std::vector<float>& v, float x0, float y0, float x1, float y1,
                const glm::vec3& c) {
    pushVertex(v, glm::vec3(x0, y0, 0.0f), c);
    pushVertex(v, glm::vec3(x1, y0, 0.0f), c);
    pushVertex(v, glm::vec3(x1, y1, 0.0f), c);
    pushVertex(v, glm::vec3(x0, y0, 0.0f), c);
    pushVertex(v, glm::vec3(x1, y1, 0.0f), c);
    pushVertex(v, glm::vec3(x0, y1, 0.0f), c);
}

std::vector<float> buildCrosshair(float size, const glm::vec3& color) {
    std::vector<float> v;
    const float length = 0.035f * size;
    const float thickness = 0.004f * size;
    pushQuad2D(v, -length, -thickness, length, thickness, color);
    pushQuad2D(v, -thickness, -length, thickness, length, color);
    return v;
}

std::vector<float> buildHealthBar(float fraction) {
    std::vector<float> v;
    const float x0 = -0.95f;
    const float y0 = -0.90f;
    const float width = 0.3f;
    const float height = 0.03f;
    const float fill = width * fraction;

    pushQuad2D(v, x0, y0, x0 + width, y0 + height, glm::vec3(0.1f, 0.1f, 0.1f));
    const glm::vec3 color = fraction > 0.5f ? glm::vec3(0.2f, 1.0f, 0.35f)
                           : fraction > 0.25f ? glm::vec3(1.0f, 0.8f, 0.2f)
                                              : glm::vec3(1.0f, 0.25f, 0.2f);
    pushQuad2D(v, x0, y0, x0 + fill, y0 + height, color);
    return v;
}

const glm::vec3 kPlayerColors[8] = {
    {1.0f, 0.30f, 0.30f}, {0.30f, 1.0f, 0.30f}, {0.35f, 0.55f, 1.0f}, {1.0f, 1.0f, 0.30f},
    {1.0f, 0.30f, 1.0f}, {0.30f, 1.0f, 1.0f}, {1.0f, 0.60f, 0.20f}, {0.60f, 0.40f, 1.0f},
};

std::string readServerIpFile() {
#if OT_PLATFORM_ANDROID
    const char* ext = SDL_AndroidGetExternalStoragePath();
    SDL_Log("[ot] external storage path: %s", ext ? ext : "(null)");
    if (ext) {
        const std::string path = std::string(ext) + "/opentournament_server.txt";
        SDL_Log("[ot] trying server ip file: %s", path.c_str());
        SDL_RWops* file = SDL_RWFromFile(path.c_str(), "r");
        if (file) {
            char buf[64] = {0};
            const size_t n = SDL_RWread(file, buf, 1, sizeof(buf) - 1);
            SDL_RWclose(file);
            std::string s(buf, n);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
                s.pop_back();
            }
            if (!s.empty()) {
                SDL_Log("[ot] server ip from file: %s", s.c_str());
                return s;
            }
        } else {
            SDL_Log("[ot] could not open server ip file");
        }
    }
#endif
    return "";
}

std::string readFileText(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

bool writeFileText(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file << text;
    return true;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool loadGeneratedMap(const std::string& path, ot::map::GenParams& params,
                      std::string& name, ot::map::GeneratedMap& generated) {
    const std::string text = readFileText(path);
    if (text.empty()) {
        return false;
    }
    if (!ot::map::parseOtMapText(text, params, name)) {
        return false;
    }
    generated = ot::map::generate(params);
    return true;
}

glm::vec3 materialColor(uint32_t index, const std::string& name) {
    uint32_t h = 2166136261u;
    for (char c : name) {
        h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    }
    h ^= index * 2654435761u;
    const float r = ((h >> 16) & 0xff) / 255.0f;
    const float g = ((h >> 8) & 0xff) / 255.0f;
    const float b = (h & 0xff) / 255.0f;
    return glm::vec3(0.25f + 0.75f * r, 0.25f + 0.75f * g, 0.25f + 0.75f * b);
}

std::vector<float> buildMapVertices(const ot::map::TriangleMesh& mesh,
                                    const std::vector<std::string>& materials) {
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.35f));
    std::vector<float> verts;
    verts.reserve(mesh.positions.size() * 6);
    for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
        const glm::vec3& p0 = mesh.positions[i];
        const glm::vec3& p1 = mesh.positions[i + 1];
        const glm::vec3& p2 = mesh.positions[i + 2];
        glm::vec3 n = mesh.normals[i / 3];
        if (glm::length(n) < 1e-6f) {
            n = glm::vec3(0, 1, 0);
        }
        const int mat = mesh.materialIndex[i / 3];
        const glm::vec3 base = materialColor(static_cast<uint32_t>(mat), materials[mat]);
        const float shade = 0.5f + 0.5f * std::fabs(glm::dot(n, lightDir));
        const glm::vec3 color = base * shade;
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

int runViewer(const std::string& mapPath) {
#if OT_PLATFORM_WINDOWS
    std::freopen("viewer.log", "w", stdout);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#endif
    std::vector<float> verts;
    glm::vec3 bmin(1e30f), bmax(-1e30f);
    size_t bad = 0;

    if (endsWith(mapPath, ".otmap")) {
        ot::map::GenParams params;
        std::string name;
        ot::map::GeneratedMap generated;
        if (!loadGeneratedMap(mapPath, params, name, generated)) {
            std::printf("[ot] failed to load map: %s\n", mapPath.c_str());
            return 1;
        }
        verts = ot::map::buildMesh(generated);
        for (const auto& box : generated.boxes) {
            bmin = glm::min(bmin, box.min);
            bmax = glm::max(bmax, box.max);
        }
        std::printf("[ot] map: %s (seed %u), %zu boxes, %zu spawns, %zu triangles\n",
                    name.c_str(), params.seed, generated.boxes.size(), generated.spawns.size(),
                    verts.size() / 18);
        for (size_t i = 0; i < generated.spawns.size() && i < 8; ++i) {
            std::printf("[ot] spawn %zu: (%.0f %.0f %.0f) yaw=%.2f\n", i,
                        generated.spawns[i].position.x, generated.spawns[i].position.y,
                        generated.spawns[i].position.z, generated.spawns[i].yaw);
        }
    } else {
        const std::string text = readFileText(mapPath);
        if (text.empty()) {
            std::printf("[ot] cannot read map: %s\n", mapPath.c_str());
            return 1;
        }
        ot::map::QuakeMapData mapData;
        if (!ot::map::parseQuakeMap(text, mapData)) {
            std::printf("[ot] failed to parse map\n");
            return 1;
        }
        ot::map::TriangleMesh mesh;
        ot::map::triangulateBrushes(mapData, mesh);
        std::printf("[ot] map: %zu brushes, %zu spawns, %zu materials, %zu triangles\n",
                    mapData.brushes.size(), mapData.spawns.size(), mapData.materials.size(),
                    mesh.positions.size() / 3);
        verts = buildMapVertices(mesh, mapData.materials);
        for (const auto& p : mesh.positions) {
            if (std::isnan(p.x) || std::isnan(p.y) || std::isnan(p.z)) { ++bad; continue; }
            bmin = glm::min(bmin, p);
            bmax = glm::max(bmax, p);
        }
        std::printf("[ot] mesh bounds: (%.0f %.0f %.0f) .. (%.0f %.0f %.0f), nan=%zu\n",
                    bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z, bad);
        for (size_t i = 0; i < mapData.spawns.size() && i < 8; ++i) {
            std::printf("[ot] spawn %zu: (%.0f %.0f %.0f) yaw=%.0f\n", i,
                        mapData.spawns[i].position.x, mapData.spawns[i].position.y,
                        mapData.spawns[i].position.z, mapData.spawns[i].yaw);
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::printf("[ot] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "OpenTournament - Viewer",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::printf("[ot] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
#if !OT_PLATFORM_ANDROID
    SDL_SetRelativeMouseMode(SDL_TRUE);
#endif

    ot::Renderer renderer;
    if (!renderer.init(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    ot::Input input;
    input.init();

    // Build a colored mesh with simple fake lighting.
    ot::Mesh meshObj;
    meshObj.upload(verts);

    ot::Camera camera;
    camera.fov = glm::radians(75.0f);
    camera.zFar = 20000.0f;

    const glm::vec3 center = (bmin + bmax) * 0.5f;
    camera.position = center + glm::vec3(0.0f, 1200.0f, -1800.0f);
    {
        const glm::vec3 dir = glm::normalize(center - camera.position);
        camera.pitch = std::asin(dir.y);
        camera.yaw = std::atan2(dir.x, -dir.z);
    }

    Uint64 lastCounter = SDL_GetPerformanceCounter();
    const double counterFrequency = static_cast<double>(SDL_GetPerformanceFrequency());

    bool running = true;
    while (running) {
        const Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(static_cast<double>(now - lastCounter) / counterFrequency);
        lastCounter = now;
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }
            input.handleEvent(event);
        }

        const glm::vec2 look = input.lookDelta(dt);
        camera.rotate(look.x, look.y);

        const glm::vec2 move = input.moveAxis();
        const Uint8* kb = SDL_GetKeyboardState(nullptr);
        float speed = kb[SDL_SCANCODE_LSHIFT] ? 2000.0f : 600.0f;
        glm::vec3 dir(0.0f);
        dir += camera.forward() * move.y;
        dir += camera.right() * move.x;
        if (kb[SDL_SCANCODE_SPACE]) dir += glm::vec3(0, 1, 0);
        if (kb[SDL_SCANCODE_LCTRL] || kb[SDL_SCANCODE_C]) dir -= glm::vec3(0, 1, 0);
        if (glm::length(dir) > 0.0001f) {
            dir = glm::normalize(dir);
            camera.position += dir * speed * dt;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if (height > 0) {
            camera.aspect = static_cast<float>(width) / static_cast<float>(height);
        }

        renderer.beginFrame();
        renderer.draw(meshObj, camera.viewProj());
        renderer.endFrame();
    }

    meshObj.destroy();
    input.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

int runWalk(const std::string& mapPath) {
#if OT_PLATFORM_WINDOWS
    std::freopen("walk.log", "w", stdout);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#endif
    std::vector<float> verts;
    std::unique_ptr<ot::ICollisionWorld> world;
    bool hasSpawn = false;
    glm::vec3 spawnPos(0.0f);
    float spawnYaw = 0.0f;

    if (endsWith(mapPath, ".otmap")) {
        ot::map::GenParams params;
        std::string name;
        ot::map::GeneratedMap generated;
        if (!loadGeneratedMap(mapPath, params, name, generated)) {
            std::printf("[ot] failed to load map: %s\n", mapPath.c_str());
            return 1;
        }
        verts = ot::map::buildMesh(generated);
        auto w = std::make_unique<ot::CollisionWorld>();
        ot::map::buildCollision(generated, *w);
        world = std::move(w);
        if (!generated.spawns.empty()) {
            hasSpawn = true;
            spawnPos = generated.spawns[0].position;
            spawnYaw = generated.spawns[0].yaw;
        }
        std::printf("[ot] map: %s (seed %u), %zu boxes, %zu spawns, %zu triangles\n",
                    name.c_str(), params.seed, generated.boxes.size(), generated.spawns.size(),
                    verts.size() / 18);
    } else {
        const std::string text = readFileText(mapPath);
        if (text.empty()) {
            std::printf("[ot] cannot read map: %s\n", mapPath.c_str());
            return 1;
        }
        ot::map::QuakeMapData mapData;
        if (!ot::map::parseQuakeMap(text, mapData)) {
            std::printf("[ot] failed to parse map\n");
            return 1;
        }
        ot::map::TriangleMesh mesh;
        ot::map::triangulateBrushes(mapData, mesh);
        auto w = std::make_unique<ot::BrushCollisionWorld>();
        ot::map::buildBrushCollision(mapData, *w);
        world = std::move(w);
        verts = buildMapVertices(mesh, mapData.materials);
        if (!mapData.spawns.empty()) {
            hasSpawn = true;
            spawnPos = mapData.spawns[0].position;
            spawnYaw = glm::radians(mapData.spawns[0].yaw);
        }
        std::printf("[ot] map: %zu brushes, %zu triangles, %zu collision brushes, %zu spawns\n",
                    mapData.brushes.size(), mesh.positions.size() / 3,
                    static_cast<ot::BrushCollisionWorld*>(world.get())->brushCount(),
                    mapData.spawns.size());
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::printf("[ot] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "OpenTournament - Walk", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::printf("[ot] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
#if !OT_PLATFORM_ANDROID
    SDL_SetRelativeMouseMode(SDL_TRUE);
#endif

    ot::Renderer renderer;
    if (!renderer.init(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    ot::Input input;
    input.init();

    ot::Mesh meshObj;
    meshObj.upload(verts);

    ot::Player player;
    if (hasSpawn) {
        player.spawn(spawnPos + glm::vec3(0, ot::Player::kHalfHeight, 0), spawnYaw);
    } else {
        player.spawn(glm::vec3(0, 64, 0), 0.0f);
    }

    ot::Weapon weapon;
    ot::Mesh crosshairMesh;
    ot::Mesh tracerMesh;

    const float baseFov = glm::radians(75.0f);
    const float aimFov = glm::radians(50.0f);

    Uint64 lastCounter = SDL_GetPerformanceCounter();
    const double counterFrequency = static_cast<double>(SDL_GetPerformanceFrequency());

    bool running = true;
    float logTimer = 0.0f;
    while (running) {
        const Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(static_cast<double>(now - lastCounter) / counterFrequency);
        lastCounter = now;
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        logTimer -= dt;
        if (logTimer <= 0.0f) {
            logTimer = 2.0f;
            const glm::vec3 c = player.center();
            std::printf("[ot] pos (%.1f %.1f %.1f)\n", c.x, c.y, c.z);
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }
            input.handleEvent(event);
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if (height > 0) {
            player.camera().aspect = static_cast<float>(width) / static_cast<float>(height);
        }

        player.update(dt, input, *world);
        weapon.update(dt, input, player.camera(), *world);
        player.camera().fov = glm::mix(baseFov, aimFov, weapon.aimFactor());

        renderer.beginFrame();
        const glm::mat4 viewProj = player.camera().viewProj();
        renderer.draw(meshObj, viewProj);

        const auto& tracers = weapon.tracers();
        if (!tracers.empty()) {
            std::vector<float> lineVerts;
            const glm::vec3 tracerColor(0.35f, 0.85f, 1.0f);
            for (const auto& t : tracers) {
                pushVertex(lineVerts, t.start, tracerColor);
                pushVertex(lineVerts, t.end, tracerColor);
            }
            tracerMesh.uploadLines(lineVerts);
            renderer.drawLines(tracerMesh, viewProj);
        }

        const float aim = weapon.aimFactor();
        const float crosshairSize = 1.0f - 0.35f * aim;
        const glm::vec3 crosshairColor = weapon.hitFlash() ? glm::vec3(1.0f, 0.9f, 0.2f)
                                                           : glm::vec3(0.2f, 1.0f, 0.4f);
        crosshairMesh.upload(buildCrosshair(crosshairSize, crosshairColor));
        renderer.drawOverlay(crosshairMesh);

        renderer.endFrame();
    }

    crosshairMesh.destroy();
    tracerMesh.destroy();
    meshObj.destroy();
    input.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

uint32_t randomSeed() {
    return static_cast<uint32_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

int runGen(const std::string& seedStr) {
#if OT_PLATFORM_WINDOWS
    std::freopen("gen.log", "w", stdout);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#endif

    ot::map::GenParams params;
    params.seed = seedStr.empty() ? randomSeed()
                                  : static_cast<uint32_t>(std::strtoul(seedStr.c_str(), nullptr, 10));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::printf("[ot] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "OpenTournament - Generate", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::printf("[ot] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
#if !OT_PLATFORM_ANDROID
    SDL_SetRelativeMouseMode(SDL_TRUE);
#endif

    ot::Renderer renderer;
    if (!renderer.init(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    ot::Input input;
    input.init();

    ot::Level level;
    auto regen = [&]() {
        const ot::map::GeneratedMap generated = ot::map::generate(params);
        level.buildFromMap(generated);
        std::printf("[ot] generated %s (seed %u)\n", generated.name.c_str(), params.seed);
    };
    auto saveCurrent = [&]() {
        const std::string name = "DM-" + std::to_string(params.seed);
        const std::string text = ot::map::serializeOtMapText(params, name);
        try {
            std::filesystem::create_directories("maps");
        } catch (...) {
        }
        const std::string path = "maps/" + name + ".otmap";
        if (writeFileText(path, text)) {
            std::printf("[ot] saved %s\n", path.c_str());
        } else {
            std::printf("[ot] failed to save %s\n", path.c_str());
        }
    };
    regen();

    ot::Player player;
    player.spawn(glm::vec3(0, ot::Player::kHalfHeight, 0), 0.0f);

    ot::Weapon weapon;
    ot::Mesh crosshairMesh;
    ot::Mesh tracerMesh;

    const float baseFov = glm::radians(75.0f);
    const float aimFov = glm::radians(50.0f);

    Uint64 lastCounter = SDL_GetPerformanceCounter();
    const double counterFrequency = static_cast<double>(SDL_GetPerformanceFrequency());

    bool running = true;
    while (running) {
        const Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(static_cast<double>(now - lastCounter) / counterFrequency);
        lastCounter = now;
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                } else if (event.key.keysym.scancode == SDL_SCANCODE_F5) {
                    params.seed = randomSeed();
                    regen();
                    player.spawn(glm::vec3(0, ot::Player::kHalfHeight, 0), 0.0f);
                } else if (event.key.keysym.scancode == SDL_SCANCODE_F2) {
                    saveCurrent();
                }
            }
            input.handleEvent(event);
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if (height > 0) {
            player.camera().aspect = static_cast<float>(width) / static_cast<float>(height);
        }

        player.update(dt, input, level.world());
        weapon.update(dt, input, player.camera(), level.world());
        player.camera().fov = glm::mix(baseFov, aimFov, weapon.aimFactor());

        renderer.beginFrame();
        const glm::mat4 viewProj = player.camera().viewProj();
        renderer.draw(level.mapMesh(), viewProj);

        const auto& tracers = weapon.tracers();
        if (!tracers.empty()) {
            std::vector<float> lineVerts;
            const glm::vec3 tracerColor(0.35f, 0.85f, 1.0f);
            for (const auto& t : tracers) {
                pushVertex(lineVerts, t.start, tracerColor);
                pushVertex(lineVerts, t.end, tracerColor);
            }
            tracerMesh.uploadLines(lineVerts);
            renderer.drawLines(tracerMesh, viewProj);
        }

        const float aim = weapon.aimFactor();
        const float crosshairSize = 1.0f - 0.35f * aim;
        const glm::vec3 crosshairColor = weapon.hitFlash() ? glm::vec3(1.0f, 0.9f, 0.2f)
                                                           : glm::vec3(0.2f, 1.0f, 0.4f);
        crosshairMesh.upload(buildCrosshair(crosshairSize, crosshairColor));
        renderer.drawOverlay(crosshairMesh);

        renderer.endFrame();
    }

    crosshairMesh.destroy();
    tracerMesh.destroy();
    level.destroy();
    input.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string mode = "offline";
#if OT_PLATFORM_ANDROID
    mode = "client";
#endif
    std::string serverHost = "127.0.0.1";
    uint16_t port = ot::net::kDefaultPort;
    std::string mapPath;
    std::string genSeed;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "server") {
            mode = "server";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                mapPath = argv[++i];
            }
        } else if (arg == "client") {
            mode = "client";
            if (i + 1 < argc) {
                serverHost = argv[++i];
            }
        } else if (arg == "viewer") {
            mode = "viewer";
            if (i + 1 < argc) {
                mapPath = argv[++i];
            }
        } else if (arg == "walk") {
            mode = "walk";
            if (i + 1 < argc) {
                mapPath = argv[++i];
            }
        } else if (arg == "gen") {
            mode = "gen";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                genSeed = argv[++i];
            }
        } else if (arg == "-m" && i + 1 < argc) {
            mapPath = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
    }

    // --- Interactive map generator ---
    if (mode == "gen") {
        return runGen(genSeed);
    }

    // --- Map viewer ---
    if (mode == "viewer") {
        if (mapPath.empty()) {
            std::printf("[ot] usage: opentournament viewer <file.map|.otmap>\n");
            return 1;
        }
        return runViewer(mapPath);
    }

    // --- Walk the map (collision + gravity + shooting) ---
    if (mode == "walk") {
        if (mapPath.empty()) {
            std::printf("[ot] usage: opentournament walk <file.map|.otmap>\n");
            return 1;
        }
        return runWalk(mapPath);
    }

    // --- Dedicated server (headless) ---
    if (mode == "server") {
#if OT_PLATFORM_WINDOWS
        std::freopen("server.log", "w", stdout);
        std::freopen("server_err.log", "w", stderr);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
#endif
        ot::Server server;
        if (!server.start(port, mapPath)) {
            return 1;
        }
        double last = nowSeconds();
        while (true) {
            const double now = nowSeconds();
            float dt = static_cast<float>(now - last);
            last = now;
            if (dt > 0.1f) {
                dt = 0.1f;
            }
            server.run(dt);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // --- SDL setup for client/offline ---
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::printf("[ot] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow(
        "OpenTournament",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::printf("[ot] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

#if !OT_PLATFORM_ANDROID
    SDL_SetRelativeMouseMode(SDL_TRUE);
#endif

    ot::Renderer renderer;
    if (!renderer.init(window)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    ot::Input input;
    input.init();

    ot::Level level;
    level.build();

    ot::Player player;
    player.spawn(glm::vec3(0.0f, 2.0f, 12.0f), 0.0f);

    ot::Weapon weapon;

    ot::Client client(player);
    if (mode == "client") {
        if (serverHost == "127.0.0.1") {
            const std::string fromFile = readServerIpFile();
            if (!fromFile.empty()) {
                serverHost = fromFile;
            }
        }
        client.connect(serverHost.c_str(), port);
        SDL_Log("[ot] client mode, server host resolved: %s:%u", serverHost.c_str(), port);
    }
    std::vector<ot::Mesh> remoteBoxes;
    for (int i = 0; i < 8; ++i) {
        ot::Mesh mesh;
        mesh.upload(buildCenteredBox(ot::Player::kHalfWidth, ot::Player::kHalfHeight,
                                     ot::Player::kHalfWidth, kPlayerColors[i]));
        remoteBoxes.push_back(mesh);
    }

    ot::Mesh crosshairMesh;
    ot::Mesh tracerMesh;
    ot::Mesh hudMesh;

    const float baseFov = glm::radians(70.0f);
    const float aimFov = glm::radians(45.0f);

    Uint64 lastCounter = SDL_GetPerformanceCounter();
    const double counterFrequency = static_cast<double>(SDL_GetPerformanceFrequency());

    bool running = true;
    bool mapApplied = false;
    while (running) {
        const Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(static_cast<double>(now - lastCounter) / counterFrequency);
        lastCounter = now;
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN &&
                       event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }
            input.handleEvent(event);
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if (height > 0) {
            player.camera().aspect = static_cast<float>(width) / static_cast<float>(height);
        }

        if (mode == "client") {
            const glm::vec2 look = input.lookDelta(dt);
            player.camera().rotate(look.x, look.y);

            const glm::vec2 move = input.moveAxis();
            ot::PlayerInput pi;
            pi.moveX = move.x;
            pi.moveY = move.y;
            pi.yaw = player.camera().yaw;
            pi.pitch = player.camera().pitch;
            pi.fire = input.fireHeld();
            pi.aim = input.aimHeld();
            pi.jump = input.jumpHeld();

            client.update(dt, pi, level.world());

            if (client.mapReceived() && !mapApplied) {
                ot::map::GenParams params;
                std::string name;
                if (ot::map::parseOtMapText(client.mapText(), params, name)) {
                    const ot::map::GeneratedMap generated = ot::map::generate(params);
                    level.buildFromMap(generated);
                    SDL_Log("[ot] applied map %s (seed %u)\n", name.c_str(), params.seed);
                }
                mapApplied = true;
            }

            weapon.update(dt, input, player.camera(), level.world());
        } else {
            player.update(dt, input, level.world());
            weapon.update(dt, input, player.camera(), level.world());
        }

        player.camera().fov = glm::mix(baseFov, aimFov, weapon.aimFactor());

        renderer.beginFrame();

        const glm::mat4 viewProj = player.camera().viewProj();
        if (mapApplied) {
            renderer.draw(level.mapMesh(), viewProj);
        } else {
            renderer.draw(level.floorMesh(), viewProj);
            renderer.draw(level.boxMesh(), viewProj);
        }

        if (mode == "client") {
            for (const auto& remote : client.remotePlayers()) {
                if (remote.id == 0 || remote.id == client.localId()) {
                    continue;
                }
                const uint32_t idx = (remote.id - 1) % 8;
                const glm::mat4 model = glm::translate(glm::mat4(1.0f), remote.position);
                renderer.draw(remoteBoxes[idx], viewProj * model);
            }
        }

        const auto& tracers = weapon.tracers();
        if (!tracers.empty()) {
            std::vector<float> lineVerts;
            const glm::vec3 tracerColor(0.35f, 0.85f, 1.0f);
            for (const auto& t : tracers) {
                pushVertex(lineVerts, t.start, tracerColor);
                pushVertex(lineVerts, t.end, tracerColor);
            }
            tracerMesh.uploadLines(lineVerts);
            renderer.drawLines(tracerMesh, viewProj);
        }

        // Overlay: crosshair.
        const float aim = weapon.aimFactor();
        const float crosshairSize = 1.0f - 0.35f * aim;
        const glm::vec3 crosshairColor = weapon.hitFlash()
            ? glm::vec3(1.0f, 0.9f, 0.2f)
            : glm::vec3(0.2f, 1.0f, 0.4f);
        crosshairMesh.upload(buildCrosshair(crosshairSize, crosshairColor));
        renderer.drawOverlay(crosshairMesh);

        // Overlay: health bar (client only).
        if (mode == "client" && client.isConnected()) {
            const float fraction = static_cast<float>(client.localHealth()) /
                                   static_cast<float>(ot::net::kMaxHealth);
            hudMesh.upload(buildHealthBar(fraction));
            renderer.drawOverlay(hudMesh);
        }

        renderer.endFrame();
    }

    client.disconnect();
    for (auto& mesh : remoteBoxes) {
        mesh.destroy();
    }
    crosshairMesh.destroy();
    tracerMesh.destroy();
    hudMesh.destroy();
    level.destroy();
    input.shutdown();
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
