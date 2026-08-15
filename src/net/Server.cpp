#include "net/Server.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "map/OtMap.h"

namespace ot {

using namespace net;

Server::~Server() {
    stop();
}

bool Server::start(uint16_t port, const std::string& mapPath) {
    if (enet_initialize() != 0) {
        std::printf("[ot] enet_initialize failed\n");
        return false;
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    m_host = enet_host_create(&address, kMaxPlayers, 2, 0, 0);
    if (!m_host) {
        std::printf("[ot] enet_host_create failed\n");
        return false;
    }

    // Load and generate the map to host (or fall back to the default arena).
    if (!mapPath.empty()) {
        std::ifstream file(mapPath, std::ios::binary);
        if (file) {
            std::ostringstream ss;
            ss << file.rdbuf();
            m_mapText = ss.str();
        }
        if (m_mapText.empty()) {
            std::printf("[ot] cannot read map %s; using default arena\n", mapPath.c_str());
            m_world.buildDefault();
        } else {
            map::GenParams params;
            std::string name;
            if (map::parseOtMapText(m_mapText, params, name)) {
                const map::GeneratedMap generated = map::generate(params);
                map::buildCollision(generated, m_world);
                m_spawns.clear();
                m_spawns.reserve(generated.spawns.size());
                for (const auto& s : generated.spawns) {
                    m_spawns.push_back(s.position);
                }
                std::printf("[ot] hosting map %s (seed %u, %zu boxes, %zu spawns)\n",
                            name.c_str(), params.seed, generated.boxes.size(), m_spawns.size());
            } else {
                std::printf("[ot] failed to parse map %s; using default arena\n", mapPath.c_str());
                m_world.buildDefault();
            }
        }
    } else {
        m_world.buildDefault();
    }

    std::printf("[ot] server listening on port %u\n", port);
    return true;
}

void Server::stop() {
    m_players.clear();
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
        enet_deinitialize();
    }
}

void Server::run(float dt) {
    if (!m_host) {
        return;
    }

    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        handleEvent(event);
    }

    m_accumulator += dt;
    while (m_accumulator >= kTick) {
        step();
        m_accumulator -= kTick;
    }
}

void Server::handleEvent(const ENetEvent& event) {
    switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            break;

        case ENET_EVENT_TYPE_RECEIVE:
            handlePacket(event.peer, event.packet);
            enet_packet_destroy(event.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT: {
            ServerPlayer* player = static_cast<ServerPlayer*>(event.peer->data);
            if (player) {
                std::printf("[ot] player %u disconnected\n", player->id);
                removePlayer(player);
            }
            break;
        }

        default:
            break;
    }
}

void Server::handlePacket(ENetPeer* peer, const ENetPacket* packet) {
    PacketReader reader(packet->data, packet->dataLength);
    const auto type = static_cast<MsgType>(reader.byte());
    if (!reader.ok()) {
        return;
    }

    switch (type) {
        case MsgType::Join: {
            ServerPlayer* player = createPlayer(peer);
            if (player) {
                reader.bytes(player->name, kMaxNameLen - 1);
                player->name[kMaxNameLen - 1] = '\0';
                sendWelcome(player);
                sendMapData(player);
            }
            break;
        }

        case MsgType::Input: {
            ServerPlayer* player = static_cast<ServerPlayer*>(peer->data);
            if (!player) {
                break;
            }
            PlayerInput input;
            input.sequence = reader.u32();
            input.moveX = reader.f32();
            input.moveY = reader.f32();
            input.yaw = reader.f32();
            input.pitch = reader.f32();
            const uint8_t flags = reader.byte();
            input.fire = (flags & 0x01) != 0;
            input.aim = (flags & 0x02) != 0;
            input.jump = (flags & 0x04) != 0;

            player->input = input;
            player->lastAckedInput = input.sequence;
            break;
        }

        default:
            break;
    }
}

Server::ServerPlayer* Server::createPlayer(ENetPeer* peer) {
    auto player = std::make_unique<ServerPlayer>();
    player->peer = peer;
    player->id = m_nextId++;
    player->health = kMaxHealth;
    player->player.spawn(spawnPointForId(player->id) + glm::vec3(0, Player::kHalfHeight, 0), 0.0f);

    ServerPlayer* raw = player.get();
    peer->data = raw;
    m_players.push_back(std::move(player));
    std::printf("[ot] player %u joined\n", raw->id);
    return raw;
}

void Server::removePlayer(ServerPlayer* player) {
    for (auto it = m_players.begin(); it != m_players.end(); ++it) {
        if (it->get() == player) {
            m_players.erase(it);
            break;
        }
    }

    PacketWriter writer;
    writer.byte(static_cast<uint8_t>(MsgType::PlayerLeft));
    writer.u32(player->id);
    ENetPacket* packet = enet_packet_create(writer.data(), writer.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(m_host, 0, packet);
    enet_host_flush(m_host);
}

void Server::respawn(ServerPlayer* player) {
    player->health = kMaxHealth;
    player->player.setState(spawnPointForId(player->id) + glm::vec3(0, Player::kHalfHeight, 0),
                            0.0f, 0.0f);
}

void Server::step() {
    m_tick++;

    for (auto& player : m_players) {
        player->player.applyInput(player->input, kTick, m_world);

        player->fireCooldown -= kTick;
        if (player->input.fire && player->fireCooldown <= 0.0f) {
            shoot(*player);
            player->fireCooldown = kFireInterval;
        }
    }

    m_ticksSinceSnapshot++;
    if (m_ticksSinceSnapshot >= kSnapshotEveryTicks) {
        m_ticksSinceSnapshot = 0;
        sendSnapshots();
    }
}

void Server::shoot(ServerPlayer& shooter) {
    const glm::vec3 dir = shooter.player.camera().forward();
    const glm::vec3 origin = shooter.player.camera().position + dir * 0.4f;

    RayHit worldHit = m_world.raycast(origin, dir, kShotRange);

    ServerPlayer* victim = nullptr;
    float victimDistance = kShotRange;

    const glm::vec3 half(Player::kHalfWidth, Player::kHalfHeight, Player::kHalfWidth);
    for (auto& other : m_players) {
        if (other.get() == &shooter) {
            continue;
        }
        AABB box{other->player.center() - half, other->player.center() + half};
        const RayHit hit = CollisionWorld::rayBox(origin, dir, box, kShotRange);
        if (hit.hit && hit.distance < victimDistance) {
            victimDistance = hit.distance;
            victim = other.get();
        }
    }

    if (!victim) {
        return;
    }
    if (worldHit.hit && worldHit.distance < victimDistance) {
        return; // a wall is in the way
    }

    victim->health -= kShotDamage;
    if (victim->health <= 0) {
        victim->health = 0;
        shooter.score++;
        std::printf("[ot] player %u killed player %u\n", shooter.id, victim->id);
        respawn(victim);
    }
}

void Server::sendSnapshots() {
    for (auto& recipient : m_players) {
        PacketWriter writer;
        writer.byte(static_cast<uint8_t>(MsgType::Snapshot));
        writer.u32(m_tick);
        writer.u32(recipient->lastAckedInput);
        writer.byte(static_cast<uint8_t>(m_players.size()));

        for (auto& player : m_players) {
            writer.u32(player->id);
            const glm::vec3& c = player->player.center();
            writer.f32(c.x);
            writer.f32(c.y);
            writer.f32(c.z);
            writer.f32(player->player.camera().yaw);
            writer.f32(player->player.camera().pitch);
            writer.i16(static_cast<int16_t>(player->health));
            writer.i16(static_cast<int16_t>(player->score));
        }

        ENetPacket* packet = enet_packet_create(writer.data(), writer.size(),
                                                ENET_PACKET_FLAG_UNSEQUENCED);
        enet_peer_send(recipient->peer, 1, packet);
    }
    enet_host_flush(m_host);
}

void Server::sendWelcome(ServerPlayer* player) {
    const glm::vec3& c = player->player.center();

    PacketWriter writer;
    writer.byte(static_cast<uint8_t>(MsgType::Welcome));
    writer.u32(player->id);
    writer.f32(c.x);
    writer.f32(c.y);
    writer.f32(c.z);
    writer.f32(player->player.camera().yaw);

    ENetPacket* packet = enet_packet_create(writer.data(), writer.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(player->peer, 0, packet);
    enet_host_flush(m_host);
}

void Server::sendMapData(ServerPlayer* player) {
    if (m_mapText.empty()) {
        return;
    }
    PacketWriter writer;
    writer.byte(static_cast<uint8_t>(MsgType::MapData));
    writer.u32(static_cast<uint32_t>(m_mapText.size()));
    writer.bytes(m_mapText.data(), m_mapText.size());

    ENetPacket* packet = enet_packet_create(writer.data(), writer.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(player->peer, 0, packet);
    enet_host_flush(m_host);
}

glm::vec3 Server::spawnPointForId(uint32_t id) const {
    if (m_spawns.empty()) {
        return glm::vec3(0.0f, 0.0f, 12.0f);
    }
    return m_spawns[id % m_spawns.size()];
}

} // namespace ot
