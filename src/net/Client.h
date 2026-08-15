#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include <enet/enet.h>

#include <glm/glm.hpp>

#include "game/Player.h"
#include "net/NetCommon.h"

namespace ot {

using namespace net;

class CollisionWorld;

struct RemoteSnapshot {
    double time = 0.0;
    PlayerState state;
};

struct RemotePlayer {
    uint32_t id = 0;
    std::deque<RemoteSnapshot> buffer;
    glm::vec3 position{0.0f};  // interpolated AABB center
    float yaw = 0.0f;
    float pitch = 0.0f;
    int health = kMaxHealth;
    int score = 0;
};

// Network client with client-side prediction, reconciliation, and entity
// interpolation for remote players.
class Client {
public:
    explicit Client(Player& localPlayer);
    ~Client();

    bool connect(const char* host, uint16_t port);
    void disconnect();
    bool isConnected() const { return m_connected; }

    void update(float dt, const PlayerInput& baseInput, CollisionWorld& world);

    uint32_t localId() const { return m_localId; }
    int localHealth() const { return m_localHealth; }
    int localScore() const { return m_localScore; }
    const std::vector<RemotePlayer>& remotePlayers() const { return m_remote; }

    bool mapReceived() const { return m_mapReady; }
    const std::string& mapText() const { return m_mapText; }

private:
    struct PredictedInput {
        PlayerInput input;
        glm::vec3 center;
        glm::vec3 velocity;
    };

    void handleEvent(const ENetEvent& event);
    void sendJoin();
    void sendInput(const PlayerInput& input);
    void onWelcome(PacketReader& reader);
    void onSnapshot(PacketReader& reader);
    void onPlayerLeft(PacketReader& reader);
    void onMapData(PacketReader& reader);
    void reconcile(const PlayerState& state, uint32_t lastAcked);
    void interpolate();

    Player& m_player;
    CollisionWorld* m_world = nullptr;

    ENetHost* m_host = nullptr;
    ENetPeer* m_server = nullptr;

    bool m_connected = false;
    uint32_t m_localId = 0;
    uint32_t m_seq = 0;
    float m_accumulator = 0.0f;
    double m_time = 0.0;

    int m_localHealth = kMaxHealth;
    int m_localScore = 0;

    bool m_mapReady = false;
    std::string m_mapText;

    std::deque<PredictedInput> m_history;
    std::vector<RemotePlayer> m_remote;
};

} // namespace ot
