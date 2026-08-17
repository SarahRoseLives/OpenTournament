#include "net/Client.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game/CollisionWorld.h"
#include "game/ICollisionWorld.h"

namespace ot {

using namespace net;

Client::Client(Player& localPlayer) : m_player(localPlayer) {}

Client::~Client() {
    disconnect();
}

bool Client::connect(const char* host, uint16_t port) {
    if (enet_initialize() != 0) {
        SDL_Log("[ot] enet_initialize failed\n");
        return false;
    }

    m_mapReady = false;
    m_mapText.clear();
    m_mapTotal = 0;

    m_host = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!m_host) {
        SDL_Log("[ot] enet_host_create failed\n");
        return false;
    }

    ENetAddress address;
    if (enet_address_set_host(&address, host) != 0) {
        SDL_Log("[ot] failed to resolve host '%s'\n", host);
        return false;
    }
    address.port = port;

    m_server = enet_host_connect(m_host, &address, 2, 0);
    if (!m_server) {
        SDL_Log("[ot] failed to initiate connection\n");
        return false;
    }

    SDL_Log("[ot] connecting to %s:%u\n", host, port);
    return true;
}

void Client::disconnect() {
    m_remote.clear();
    m_history.clear();
    m_connected = false;
    if (m_server) {
        enet_peer_disconnect(m_server, 0);
        // Service once to flush the disconnect.
        ENetEvent event;
        while (enet_host_service(m_host, &event, 0) > 0) {
        }
        m_server = nullptr;
    }
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
        enet_deinitialize();
    }
}

void Client::update(float dt, const PlayerInput& baseInput, ICollisionWorld& world) {
    m_world = &world;
    m_time += dt;

    if (!m_host) {
        return;
    }

    ENetEvent event;
    while (enet_host_service(m_host, &event, 0) > 0) {
        handleEvent(event);
    }

    if (!m_connected || !m_server) {
        return;
    }

    m_accumulator += dt;
    while (m_accumulator >= kTick) {
        m_seq++;
        PlayerInput stepInput = baseInput;
        stepInput.sequence = m_seq;

        m_player.applyInput(stepInput, kTick, world);
        m_history.push_back({stepInput, m_player.center(), m_player.velocity()});
        if (m_history.size() > 1024) {
            m_history.pop_front();
        }
        sendInput(stepInput);

        m_accumulator -= kTick;
    }

    interpolate();
}

void Client::handleEvent(const ENetEvent& event) {
    switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            SDL_Log("[ot] connected to server\n");
            sendJoin();
            break;

        case ENET_EVENT_TYPE_RECEIVE: {
            PacketReader reader(event.packet->data, event.packet->dataLength);
            const auto type = static_cast<MsgType>(reader.byte());
            switch (type) {
                case MsgType::Welcome:
                    onWelcome(reader);
                    break;
                case MsgType::Snapshot:
                    onSnapshot(reader);
                    break;
                case MsgType::PlayerLeft:
                    onPlayerLeft(reader);
                    break;
                case MsgType::MapData:
                    onMapData(reader);
                    break;
                case MsgType::MapChunk:
                    onMapChunk(reader);
                    break;
                case MsgType::FlagState:
                    onFlagState(reader);
                    break;
                default:
                    break;
            }
            enet_packet_destroy(event.packet);
            break;
        }

        case ENET_EVENT_TYPE_DISCONNECT:
            SDL_Log("[ot] disconnected from server\n");
            m_connected = false;
            m_remote.clear();
            m_flags.clear();
            m_localCarrying = false;
            m_teamScore[0] = 0;
            m_teamScore[1] = 0;
            break;

        default:
            break;
    }
}

void Client::sendJoin() {
    PacketWriter writer;
    writer.byte(static_cast<uint8_t>(MsgType::Join));
    char name[kMaxNameLen] = {0};
    std::strncpy(name, "Player", kMaxNameLen - 1);
    writer.bytes(name, kMaxNameLen);

    ENetPacket* packet = enet_packet_create(writer.data(), writer.size(),
                                            ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(m_server, 0, packet);
    enet_host_flush(m_host);
}

void Client::sendInput(const PlayerInput& input) {
    PacketWriter writer;
    writer.byte(static_cast<uint8_t>(MsgType::Input));
    writer.u32(input.sequence);
    writer.f32(input.moveX);
    writer.f32(input.moveY);
    writer.f32(input.yaw);
    writer.f32(input.pitch);
    uint8_t flags = 0;
    if (input.fire) flags |= 0x01;
    if (input.aim) flags |= 0x02;
    if (input.jump) flags |= 0x04;
    writer.byte(flags);
    writer.byte(static_cast<uint8_t>(input.weapon));

    ENetPacket* packet = enet_packet_create(writer.data(), writer.size(),
                                            ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send(m_server, 1, packet);
}

void Client::onWelcome(PacketReader& reader) {
    m_localId = reader.u32();
    const float x = reader.f32();
    const float y = reader.f32();
    const float z = reader.f32();
    const float yaw = reader.f32();
    m_localTeam = static_cast<int>(reader.byte());

    m_player.setState(glm::vec3(x, y, z), yaw, 0.0f);
    m_history.clear();
    m_accumulator = 0.0f;
    m_connected = true;
    SDL_Log("[ot] welcome, local id %u team %d\n", m_localId, m_localTeam);
}

void Client::onSnapshot(PacketReader& reader) {
    const uint32_t serverTick = reader.u32();
    (void)serverTick;
    const uint32_t lastAcked = reader.u32();
    const uint8_t count = reader.byte();

    std::vector<PlayerState> states;
    states.reserve(count);
    for (int i = 0; i < count && reader.ok(); ++i) {
        PlayerState state;
        state.id = reader.u32();
        state.px = reader.f32();
        state.py = reader.f32();
        state.pz = reader.f32();
        state.yaw = reader.f32();
        state.pitch = reader.f32();
        state.health = reader.i16();
        state.score = reader.i16();
        state.team = reader.byte();
        state.carrying = reader.byte();
        states.push_back(state);
    }

    for (const auto& state : states) {
        if (state.id == m_localId) {
            m_localHealth = state.health;
            m_localScore = state.score;
            m_localTeam = static_cast<int>(state.team);
            m_localCarrying = state.carrying != 0;
            reconcile(state, lastAcked);
        } else {
            RemotePlayer* remote = nullptr;
            for (auto& rp : m_remote) {
                if (rp.id == state.id) {
                    remote = &rp;
                    break;
                }
            }
            if (!remote) {
                m_remote.push_back(RemotePlayer{state.id, {}, {state.px, state.py, state.pz},
                                                state.yaw, state.pitch, state.health, state.score,
                                                static_cast<int>(state.team),
                                                state.carrying != 0});
                remote = &m_remote.back();
            }
            remote->buffer.push_back({m_time, state});
            if (remote->buffer.size() > 128) {
                remote->buffer.pop_front();
            }
        }
    }
}

void Client::onPlayerLeft(PacketReader& reader) {
    const uint32_t id = reader.u32();
    m_remote.erase(std::remove_if(m_remote.begin(), m_remote.end(),
                                  [id](const RemotePlayer& rp) { return rp.id == id; }),
                   m_remote.end());
    SDL_Log("[ot] player %u left\n", id);
}

void Client::onMapData(PacketReader& reader) {
    const uint32_t total = reader.u32();
    if (!reader.ok() || total == 0 || total > 256u * 1024u * 1024u) {
        return;
    }
    m_mapTotal = total;
    m_mapText.clear();
    m_mapText.reserve(total);
    m_mapReady = false;
    SDL_Log("[ot] receiving map (%u bytes)\n", total);
}

void Client::onMapChunk(PacketReader& reader) {
    const uint32_t offset = reader.u32();
    const size_t len = reader.remaining();
    if (offset != m_mapText.size() || len == 0) {
        return;  // Chunks must arrive in order on the reliable channel.
    }
    const size_t old = m_mapText.size();
    m_mapText.resize(old + len);
    reader.bytes(&m_mapText[old], len);
    if (m_mapText.size() >= m_mapTotal) {
        m_mapReady = true;
        SDL_Log("[ot] received map (%zu bytes)\n", m_mapText.size());
    }
}

void Client::onFlagState(PacketReader& reader) {
    const uint8_t count = reader.byte();
    m_flags.clear();
    for (int i = 0; i < count && reader.ok(); ++i) {
        RemoteFlag flag;
        flag.team = static_cast<int>(reader.byte());
        flag.state = static_cast<FlagState>(reader.byte());
        flag.carrierId = reader.u32();
        flag.pos.x = reader.f32();
        flag.pos.y = reader.f32();
        flag.pos.z = reader.f32();
        m_flags.push_back(flag);
    }
    if (reader.ok()) {
        m_teamScore[0] = reader.i16();
        m_teamScore[1] = reader.i16();
    }
}

void Client::reconcile(const PlayerState& state, uint32_t lastAcked) {
    auto it = std::find_if(m_history.begin(), m_history.end(), [lastAcked](const PredictedInput& p) {
        return p.input.sequence == lastAcked;
    });
    if (it == m_history.end()) {
        return;
    }

    const glm::vec3 serverCenter(state.px, state.py, state.pz);
    if (glm::distance(it->center, serverCenter) > 0.02f) {
        m_player.setState(serverCenter, state.yaw, state.pitch);
        for (auto j = std::next(it); j != m_history.end() && m_world; ++j) {
            m_player.applyInput(j->input, kTick, *m_world);
        }
    }

    m_history.erase(m_history.begin(), std::next(it));
}

void Client::interpolate() {
    const double renderTime = m_time - kInterpDelay;

    for (auto& remote : m_remote) {
        auto& buffer = remote.buffer;
        if (buffer.empty()) {
            continue;
        }

        const PlayerState* a = nullptr;
        const PlayerState* b = nullptr;
        float alpha = 0.0f;

        if (buffer.size() == 1 || renderTime <= buffer.front().time) {
            a = b = &buffer.front().state;
        } else if (renderTime >= buffer.back().time) {
            a = b = &buffer.back().state;
        } else {
            for (size_t i = 0; i + 1 < buffer.size(); ++i) {
                if (buffer[i].time <= renderTime && renderTime < buffer[i + 1].time) {
                    const double span = buffer[i + 1].time - buffer[i].time;
                    alpha = span > 0.0 ? static_cast<float>((renderTime - buffer[i].time) / span) : 0.0f;
                    a = &buffer[i].state;
                    b = &buffer[i + 1].state;
                    break;
                }
            }
        }

        if (a && b) {
            remote.position = glm::vec3(
                glm::mix(a->px, b->px, alpha),
                glm::mix(a->py, b->py, alpha),
                glm::mix(a->pz, b->pz, alpha));
            remote.yaw = a->yaw;
            remote.pitch = a->pitch;
            remote.health = b->health;
            remote.score = b->score;
            remote.team = static_cast<int>(b->team);
            remote.carrying = b->carrying != 0;
        }
    }
}

} // namespace ot
