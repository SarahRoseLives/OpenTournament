#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace ot::net {

constexpr uint16_t kDefaultPort = 7777;
constexpr float kTick = 1.0f / 60.0f;
constexpr int kSnapshotEveryTicks = 2;   // 30 Hz snapshots
constexpr float kInterpDelay = 0.1f;     // seconds of interpolation buffer
constexpr uint32_t kMaxNameLen = 16;
constexpr int kMaxPlayers = 8;

constexpr int kMaxHealth = 100;
constexpr int kShotDamage = 34;
constexpr float kFireInterval = 0.12f;
constexpr float kShotRange = 300.0f;

enum class MsgType : uint8_t {
    Join = 0,
    Welcome = 1,
    Input = 2,
    Snapshot = 3,
    PlayerLeft = 4,
    MapData = 5,
    MapChunk = 6,
};

constexpr uint32_t kMapChunkSize = 32 * 1024;

struct PlayerState {
    uint32_t id = 0;
    float px = 0.0f;  // AABB center
    float py = 0.0f;
    float pz = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    int16_t health = static_cast<int16_t>(kMaxHealth);
    int16_t score = 0;
};

// Little-endian byte writer (both target platforms are little-endian).
class PacketWriter {
public:
    void byte(uint8_t v) { m.push_back(v); }

    void u16(uint16_t v) {
        m.push_back(static_cast<uint8_t>(v & 0xff));
        m.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    }

    void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }

    void u32(uint32_t v) {
        m.push_back(static_cast<uint8_t>(v & 0xff));
        m.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        m.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        m.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    }

    void f32(float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }

    void bytes(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        m.insert(m.end(), p, p + n);
    }

    const uint8_t* data() const { return m.data(); }
    size_t size() const { return m.size(); }

private:
    std::vector<uint8_t> m;
};

class PacketReader {
public:
    PacketReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    bool ok() const { return m_pos <= m_size; }

    uint8_t byte() {
        if (m_pos + 1 > m_size) { m_pos = m_size + 1; return 0; }
        return m_data[m_pos++];
    }

    uint16_t u16() {
        if (m_pos + 2 > m_size) { m_pos = m_size + 1; return 0; }
        uint16_t v = static_cast<uint16_t>(m_data[m_pos]) |
                     static_cast<uint16_t>(m_data[m_pos + 1] << 8);
        m_pos += 2;
        return v;
    }

    int16_t i16() { return static_cast<int16_t>(u16()); }

    uint32_t u32() {
        if (m_pos + 4 > m_size) { m_pos = m_size + 1; return 0; }
        uint32_t v = static_cast<uint32_t>(m_data[m_pos]) |
                     (static_cast<uint32_t>(m_data[m_pos + 1]) << 8) |
                     (static_cast<uint32_t>(m_data[m_pos + 2]) << 16) |
                     (static_cast<uint32_t>(m_data[m_pos + 3]) << 24);
        m_pos += 4;
        return v;
    }

    float f32() {
        uint32_t bits = u32();
        float v = 0.0f;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    void bytes(void* out, size_t n) {
        if (m_pos + n > m_size) { m_pos = m_size + 1; return; }
        std::memcpy(out, m_data + m_pos, n);
        m_pos += n;
    }

    size_t remaining() const {
        return m_pos <= m_size ? m_size - m_pos : 0;
    }

private:
    const uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_pos = 0;
};

} // namespace ot::net
