#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <enet/enet.h>

#include <glm/glm.hpp>

#include "game/CollisionWorld.h"
#include "game/ICollisionWorld.h"
#include "game/Player.h"
#include "net/NetCommon.h"

namespace ot {

using namespace net;

// Authoritative dedicated game server. Headless (no rendering).
class Server {
public:
    ~Server();

    bool start(uint16_t port, const std::string& mapPath,
               const std::string& ut2004Root = "C:\\UT2004",
               const std::string& ut99Root = "C:\\UnrealTournament");
    void stop();
    void run(float dt);

    bool isRunning() const { return m_host != nullptr; }

private:
    struct ServerPlayer {
        ENetPeer* peer = nullptr;
        uint32_t id = 0;
        Player player;
        PlayerInput input;
        float fireCooldown = 0.0f;
        char name[kMaxNameLen] = {0};
        int health = kMaxHealth;
        int score = 0;
        uint32_t lastAckedInput = 0;
        int team = 0;
        bool carryingFlag = false;
    };

    struct ServerFlag {
        int team = 0;
        glm::vec3 home{0.0f};
        FlagState state = FlagState::Home;
        glm::vec3 pos{0.0f};
        uint32_t carrierId = 0;
        float returnTimer = 0.0f;
    };

    void step();
    void handleEvent(const ENetEvent& event);
    void handlePacket(ENetPeer* peer, const ENetPacket* packet);

    ServerPlayer* createPlayer(ENetPeer* peer);
    void removePlayer(ServerPlayer* player);
    void respawn(ServerPlayer* player);
    void shoot(ServerPlayer& shooter);
    void updateFlags();
    void sendSnapshots();
    void sendWelcome(ServerPlayer* player);
    void sendMapData(ServerPlayer* player);
    void sendFlagState();
    glm::vec3 spawnPointForId(uint32_t id) const;
    glm::vec3 spawnForPlayer(const ServerPlayer& player) const;
    float spawnYawForId(uint32_t id) const;

    ENetHost* m_host = nullptr;
    std::unique_ptr<ICollisionWorld> m_world;
    std::vector<std::unique_ptr<ServerPlayer>> m_players;

    std::string m_mapText;
    std::vector<glm::vec3> m_spawns;
    std::vector<float> m_spawnYaws;

    bool m_ctf = false;
    std::vector<ServerFlag> m_flags;
    int m_teamScore[2] = {0, 0};

    uint32_t m_nextId = 1;
    uint32_t m_tick = 0;
    int m_ticksSinceSnapshot = 0;
    int m_ticksSinceFlagState = 0;
    float m_accumulator = 0.0f;
};

} // namespace ot
