#include "net/Server.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "core/Platform.h"
#include "game/BrushCollisionWorld.h"
#include "game/WeaponDef.h"
#include "map/BspMap.h"
#include "map/MapFormat.h"
#include "map/OtMap.h"

#if !OT_PLATFORM_ANDROID
#include "Convert.h"
#endif

namespace ot {

using namespace net;

#if !OT_PLATFORM_ANDROID
namespace {

bool endsWithExt(const std::string& s, const char* ext) {
    const size_t dot = s.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    return s.substr(dot) == ext;
}

}  // namespace
#endif

Server::~Server() {
    stop();
}

bool Server::start(uint16_t port, const std::string& mapPath, const std::string& ut2004Root,
                   const std::string& ut99Root) {
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
#if !OT_PLATFORM_ANDROID
        // Original maps are converted in memory at startup, so the server can
        // host a .ut2 (UT2004) or .unr (UT99) directly.
        const bool isUnr = endsWithExt(mapPath, ".unr");
        const bool isUt2 = endsWithExt(mapPath, ".ut2");
        if (isUnr || isUt2) {
            map::Map converted;
            const bool ok = isUnr ? ue1ToOtMap(mapPath, ut99Root, converted)
                                  : ue2ToOtMap(mapPath, ut2004Root, converted);
            if (ok) {
                std::vector<uint8_t> bytes;
                if (map::saveMap(converted, bytes)) {
                    m_mapText.assign(reinterpret_cast<const char*>(bytes.data()),
                                     bytes.size());
                    std::printf("[ot] converted %s (%zu nodes, %zu surfs)\n",
                                mapPath.c_str(), converted.nodes.size(),
                                converted.surfaces.size());
                }
            }
        }
#endif
        if (m_mapText.empty()) {
            std::ifstream file(mapPath, std::ios::binary);
            if (file) {
                std::ostringstream ss;
                ss << file.rdbuf();
                m_mapText = ss.str();
            }
        }
    }

    // Detect the binary BSP format (magic "OTMP") vs the text procedural format.
    const bool isBsp =
        m_mapText.size() >= 4 &&
        (static_cast<uint32_t>(static_cast<unsigned char>(m_mapText[0])) |
         (static_cast<uint32_t>(static_cast<unsigned char>(m_mapText[1])) << 8) |
         (static_cast<uint32_t>(static_cast<unsigned char>(m_mapText[2])) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(m_mapText[3])) << 24)) ==
            map::kMagic;

    if (isBsp) {
        map::Map bsp;
        if (map::loadMap(bsp, reinterpret_cast<const uint8_t*>(m_mapText.data()),
                         m_mapText.size())) {
            auto world = std::make_unique<BrushCollisionWorld>();
            map::buildBspCollision(bsp, *world);
            m_world = std::move(world);

            // Use the real PlayerStart spawn points from the map.
            m_spawns.clear();
            m_spawnYaws.clear();
            for (const auto& sp : bsp.spawnPoints) {
                m_spawns.push_back(glm::vec3(sp.x, sp.y, sp.z));
            }
            m_spawnYaws = bsp.spawnYaw;
            // Fallback: spawn at the map center, on the floor below it.
            if (m_spawns.empty()) {
                glm::vec3 bmin, bmax;
                map::computeBounds(bsp, bmin, bmax);
                glm::vec3 probe = (bmin + bmax) * 0.5f;
                probe.y = bmax.y + 50.0f;
                const RayHit hit = m_world->raycast(probe, glm::vec3(0.0f, -1.0f, 0.0f), 300000.0f);
                if (hit.hit) {
                    m_spawns.push_back(glm::vec3(probe.x, hit.point.y + 0.2f, probe.z));
                } else {
                    m_spawns.push_back(probe);
                }
                m_spawnYaws.push_back(0.0f);
            }
            std::printf("[ot] hosting bsp map (%zu nodes, %zu surfs, %zu spawns)\n",
                        bsp.nodes.size(), bsp.surfaces.size(), m_spawns.size());

            // CTF flag bases, if the map carries them (2-team CTF).
            m_ctf = bsp.flags.size() == 2;
            m_flags.clear();
            if (m_ctf) {
                for (const auto& fp : bsp.flags) {
                    ServerFlag flag;
                    flag.team = fp.team;
                    flag.home = glm::vec3(fp.position.x, fp.position.y, fp.position.z);
                    flag.pos = flag.home;
                    flag.state = FlagState::Home;
                    m_flags.push_back(flag);
                }
                m_teamScore[0] = 0;
                m_teamScore[1] = 0;
                std::printf("[ot] hosting CTF (%zu flag bases, score cap %d)\n",
                            m_flags.size(), kScoreCap);
            }
        } else {
            std::printf("[ot] failed to load bsp map; using default arena\n");
            auto world = std::make_unique<CollisionWorld>();
            world->buildDefault();
            m_world = std::move(world);
        }
    } else if (!m_mapText.empty()) {
        map::GenParams params;
        std::string name;
        if (map::parseOtMapText(m_mapText, params, name)) {
            const map::GeneratedMap generated = map::generate(params);
            auto world = std::make_unique<CollisionWorld>();
            map::buildCollision(generated, *world);
            m_world = std::move(world);
            m_spawns.clear();
            m_spawns.reserve(generated.spawns.size());
            for (const auto& s : generated.spawns) {
                m_spawns.push_back(s.position);
            }
            std::printf("[ot] hosting map %s (seed %u, %zu boxes, %zu spawns)\n",
                        name.c_str(), params.seed, generated.boxes.size(), m_spawns.size());
        } else {
            std::printf("[ot] failed to parse map %s; using default arena\n", mapPath.c_str());
            auto world = std::make_unique<CollisionWorld>();
            world->buildDefault();
            m_world = std::move(world);
        }
    } else {
        auto world = std::make_unique<CollisionWorld>();
        world->buildDefault();
        m_world = std::move(world);
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
                if (m_ctf) {
                    sendFlagState();
                }
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
            input.weapon = static_cast<int>(reader.byte());

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

    if (m_ctf) {
        // Balance teams: join the smaller one.
        int counts[2] = {0, 0};
        for (const auto& p : m_players) {
            counts[p->team]++;
        }
        player->team = counts[0] <= counts[1] ? 0 : 1;
    }

    player->health = kMaxHealth;
    player->player.spawn(spawnForPlayer(*player) + glm::vec3(0, Player::kHalfHeight, 0),
                         spawnYawForId(player->id));

    ServerPlayer* raw = player.get();
    peer->data = raw;
    m_players.push_back(std::move(player));
    std::printf("[ot] player %u joined (team %d%s)\n", raw->id, raw->team,
                m_ctf ? "" : " [dm]");
    return raw;
}

void Server::removePlayer(ServerPlayer* player) {
    // Drop any flag the departing player carried.
    if (m_ctf) {
        for (auto& flag : m_flags) {
            if (flag.state == FlagState::Carried && flag.carrierId == player->id) {
                flag.state = FlagState::Dropped;
                flag.pos = player->player.center();
                flag.returnTimer = kFlagReturnTime;
            }
        }
        sendFlagState();
    }

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
    player->input.weapon = 0;
    player->player.setState(spawnForPlayer(*player) + glm::vec3(0, Player::kHalfHeight, 0),
                            spawnYawForId(player->id), 0.0f);
}

glm::vec3 Server::spawnForPlayer(const ServerPlayer& player) const {
    if (m_ctf && player.team >= 0 && player.team < 2) {
        for (const auto& flag : m_flags) {
            if (flag.team == player.team) {
                return flag.home;
            }
        }
    }
    return spawnPointForId(player.id);
}

void Server::step() {
    m_tick++;

    if (m_ctf) {
        updateFlags();
    }

    for (auto& player : m_players) {
        player->player.applyInput(player->input, kTick, *m_world);

        player->fireCooldown -= kTick;
        if (player->input.fire && player->fireCooldown <= 0.0f) {
            shoot(*player);
            player->fireCooldown = weaponDef(player->input.weapon).fireInterval;
        }
    }

    m_ticksSinceSnapshot++;
    if (m_ticksSinceSnapshot >= kSnapshotEveryTicks) {
        m_ticksSinceSnapshot = 0;
        sendSnapshots();
    }

    // Refresh flag state to clients a few times per second so late joiners
    // (and any dropped reliable packets) converge quickly.
    m_ticksSinceFlagState++;
    if (m_ctf && m_ticksSinceFlagState >= 8) {
        m_ticksSinceFlagState = 0;
        sendFlagState();
    }
}

void Server::shoot(ServerPlayer& shooter) {
    const WeaponDef& def = weaponDef(shooter.input.weapon);
    const glm::vec3 dir = shooter.player.camera().forward();
    const glm::vec3 origin = shooter.player.camera().position + dir * 0.4f;

    RayHit worldHit = m_world->raycast(origin, dir, def.range);

    ServerPlayer* victim = nullptr;
    float victimDistance = def.range;

    const glm::vec3 half(Player::kHalfWidth, Player::kHalfHeight, Player::kHalfWidth);
    for (auto& other : m_players) {
        if (other.get() == &shooter) {
            continue;
        }
        AABB box{other->player.center() - half, other->player.center() + half};
        const RayHit hit = CollisionWorld::rayBox(origin, dir, box, def.range);
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

    victim->health -= def.damage;
    if (victim->health <= 0) {
        victim->health = 0;
        shooter.score++;
        std::printf("[ot] player %u killed player %u\n", shooter.id, victim->id);

        // The victim drops any flag they were carrying.
        victim->carryingFlag = false;
        if (m_ctf) {
            for (auto& flag : m_flags) {
                if (flag.state == FlagState::Carried && flag.carrierId == victim->id) {
                    flag.state = FlagState::Dropped;
                    flag.pos = victim->player.center();
                    flag.carrierId = 0;
                    flag.returnTimer = kFlagReturnTime;
                    std::printf("[ot] flag (team %d) dropped at (%.1f %.1f %.1f)\n",
                                flag.team, flag.pos.x, flag.pos.y, flag.pos.z);
                    sendFlagState();
                }
            }
        }

        respawn(victim);
    }
}

void Server::sendFlagState() {
    if (m_players.empty()) {
        return;
    }
    PacketWriter writer;
    writer.byte(static_cast<uint8_t>(MsgType::FlagState));
    writer.byte(static_cast<uint8_t>(m_flags.size()));
    for (const auto& flag : m_flags) {
        writer.byte(static_cast<uint8_t>(flag.team));
        writer.byte(static_cast<uint8_t>(flag.state));
        writer.u32(flag.carrierId);
        writer.f32(flag.pos.x);
        writer.f32(flag.pos.y);
        writer.f32(flag.pos.z);
    }
    writer.i16(static_cast<int16_t>(m_teamScore[0]));
    writer.i16(static_cast<int16_t>(m_teamScore[1]));

    ENetPacket* packet = enet_packet_create(writer.data(), writer.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(m_host, 0, packet);
    enet_host_flush(m_host);
}

void Server::updateFlags() {
    bool changed = false;

    // Auto-return dropped flags.
    for (auto& flag : m_flags) {
        if (flag.state == FlagState::Dropped) {
            flag.returnTimer -= kTick;
            if (flag.returnTimer <= 0.0f) {
                flag.state = FlagState::Home;
                flag.pos = flag.home;
                std::printf("[ot] flag (team %d) returned home\n", flag.team);
                changed = true;
            }
        }
    }

    for (auto& player : m_players) {
        const glm::vec3& c = player->player.center();

        // Touching your own flag (lying on the ground at your base) returns it.
        for (auto& flag : m_flags) {
            if (flag.team != player->team) {
                continue;
            }
            if (flag.state != FlagState::Dropped) {
                continue;
            }
            if (glm::length(c - flag.pos) > kFlagPickupRadius) {
                continue;
            }
            flag.state = FlagState::Home;
            flag.pos = flag.home;
            flag.returnTimer = 0.0f;
            std::printf("[ot] player %u returned own flag (team %d)\n",
                        player->id, player->team);
            changed = true;
        }

        if (!player->carryingFlag) {
            // Pick up a Home/Dropped enemy flag when close to it.
            for (auto& flag : m_flags) {
                if (flag.team == player->team) {
                    continue;
                }
                if (flag.state != FlagState::Home && flag.state != FlagState::Dropped) {
                    continue;
                }
                if (glm::length(c - flag.pos) > kFlagPickupRadius) {
                    continue;
                }
                flag.state = FlagState::Carried;
                flag.carrierId = player->id;
                player->carryingFlag = true;
                std::printf("[ot] player %u picked up flag (team %d)\n",
                            player->id, flag.team);
                changed = true;
                break;
            }
        } else {
            // Capture: reach your own base while holding the enemy flag.
            for (auto& flag : m_flags) {
                if (flag.team != player->team) {
                    continue;
                }
                if (glm::length(c - flag.home) > kFlagPickupRadius) {
                    continue;
                }
                player->carryingFlag = false;
                m_teamScore[player->team]++;
                std::printf("[ot] TEAM %d SCORES (now %d)\n",
                            player->team, m_teamScore[player->team]);

                for (auto& f : m_flags) {
                    f.state = FlagState::Home;
                    f.pos = f.home;
                    f.carrierId = 0;
                }
                changed = true;

                if (m_teamScore[player->team] >= kScoreCap) {
                    std::printf("[ot] TEAM %d WINS THE MATCH\n", player->team);
                    m_teamScore[0] = 0;
                    m_teamScore[1] = 0;
                    for (auto& p : m_players) {
                        p->carryingFlag = false;
                    }
                }
                break;
            }
        }
    }

    if (changed) {
        sendFlagState();
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
            writer.byte(static_cast<uint8_t>(player->team));
            writer.byte(player->carryingFlag ? 1 : 0);
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
    writer.byte(static_cast<uint8_t>(player->team));

    ENetPacket* packet = enet_packet_create(writer.data(), writer.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(player->peer, 0, packet);
    enet_host_flush(m_host);
}

void Server::sendMapData(ServerPlayer* player) {
    if (m_mapText.empty()) {
        return;
    }
    const uint32_t total = static_cast<uint32_t>(m_mapText.size());

    // Header: announce the total size, then stream the map in fixed chunks so
    // large maps (multiple MB) survive the reliable packet path.
    PacketWriter header;
    header.byte(static_cast<uint8_t>(MsgType::MapData));
    header.u32(total);
    ENetPacket* hp = enet_packet_create(header.data(), header.size(), ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(player->peer, 0, hp);

    for (uint32_t offset = 0; offset < total; offset += kMapChunkSize) {
        const uint32_t n = (total - offset) < kMapChunkSize ? (total - offset) : kMapChunkSize;
        PacketWriter chunk;
        chunk.byte(static_cast<uint8_t>(MsgType::MapChunk));
        chunk.u32(offset);
        chunk.bytes(m_mapText.data() + offset, n);
        ENetPacket* packet = enet_packet_create(chunk.data(), chunk.size(),
                                                ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(player->peer, 0, packet);
    }
    enet_host_flush(m_host);
}

glm::vec3 Server::spawnPointForId(uint32_t id) const {
    if (m_spawns.empty()) {
        return glm::vec3(0.0f, 0.0f, 12.0f);
    }
    return m_spawns[id % m_spawns.size()];
}

float Server::spawnYawForId(uint32_t id) const {
    if (m_spawnYaws.empty()) {
        return 0.0f;
    }
    return m_spawnYaws[id % m_spawnYaws.size()];
}

} // namespace ot
