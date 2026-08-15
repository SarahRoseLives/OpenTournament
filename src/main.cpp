#include <SDL.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "core/Platform.h"
#include "game/Level.h"
#include "game/Player.h"
#include "game/Weapon.h"
#include "input/Input.h"
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

} // namespace

int main(int argc, char* argv[]) {
    std::string mode = "offline";
#if OT_PLATFORM_ANDROID
    mode = "client";
#endif
    std::string serverHost = "127.0.0.1";
    uint16_t port = ot::net::kDefaultPort;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "server") {
            mode = "server";
        } else if (arg == "client") {
            mode = "client";
            if (i + 1 < argc) {
                serverHost = argv[++i];
            }
        } else if (arg == "-p" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
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
        if (!server.start(port)) {
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

            weapon.update(dt, input, player.camera(), level.world());
        } else {
            player.update(dt, input, level.world());
            weapon.update(dt, input, player.camera(), level.world());
        }

        player.camera().fov = glm::mix(baseFov, aimFov, weapon.aimFactor());

        renderer.beginFrame();

        const glm::mat4 viewProj = player.camera().viewProj();
        renderer.draw(level.floorMesh(), viewProj);
        renderer.draw(level.boxMesh(), viewProj);

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
