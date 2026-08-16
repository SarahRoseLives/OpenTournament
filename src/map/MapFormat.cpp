#include "map/MapFormat.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace ot::map {

namespace {

class Writer {
public:
    void u8(uint8_t v) { m.push_back(v); }
    void i8(int8_t v) { m.push_back(static_cast<uint8_t>(v)); }
    void u32(uint32_t v) {
        m.push_back(static_cast<uint8_t>(v & 0xff));
        m.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        m.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        m.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u32(bits);
    }
    void bytes(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        m.insert(m.end(), p, p + n);
    }
    void patchU32(size_t offset, uint32_t v) {
        if (offset + 4 <= m.size()) {
            m[offset + 0] = static_cast<uint8_t>(v & 0xff);
            m[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
            m[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xff);
            m[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xff);
        }
    }
    const std::vector<uint8_t>& data() const { return m; }

private:
    std::vector<uint8_t> m;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}
    bool ok() const { return m_pos <= m_size; }
    uint8_t u8() { return m_pos < m_size ? m_data[m_pos++] : 0; }
    int8_t i8() { return static_cast<int8_t>(u8()); }
    uint32_t u32() {
        if (m_pos + 4 > m_size) { m_pos = m_size + 1; return 0; }
        uint32_t v = static_cast<uint32_t>(m_data[m_pos]) |
                     (static_cast<uint32_t>(m_data[m_pos + 1]) << 8) |
                     (static_cast<uint32_t>(m_data[m_pos + 2]) << 16) |
                     (static_cast<uint32_t>(m_data[m_pos + 3]) << 24);
        m_pos += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
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

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_pos = 0;
};

} // namespace

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

bool saveMap(const Map& map, std::vector<uint8_t>& out) {
    // Build geometry section.
    std::vector<uint8_t> geometry;
    {
        Writer w;
        w.u32(static_cast<uint32_t>(map.points.size()));
        for (const auto& p : map.points) {
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
        }
        w.u32(static_cast<uint32_t>(map.nodes.size()));
        for (const auto& n : map.nodes) {
            w.f32(n.planeX); w.f32(n.planeY); w.f32(n.planeZ); w.f32(n.planeW);
            w.i32(n.vertPool);
            w.i32(n.surf);
            w.i32(n.vertex);
            w.i32(n.collisionBound);
            w.i8(n.zone[0]); w.i8(n.zone[1]);
            w.i8(n.leaf[0]); w.i8(n.leaf[1]);
            w.u8(n.numVertices);
            w.u8(n.nodeFlags);
        }
        w.u32(static_cast<uint32_t>(map.verts.size()));
        for (const auto& v : map.verts) {
            w.i32(v.pointIndex);
            w.i32(v.side);
            w.f32(v.u);
            w.f32(v.v);
        }
        w.u32(static_cast<uint32_t>(map.surfaces.size()));
        for (const auto& s : map.surfaces) {
            w.i32(s.materialIndex);
            w.i32(s.textureIndex);
            w.u32(s.polyFlags);
            w.i32(s.pBase);
            w.f32(s.normalX); w.f32(s.normalY); w.f32(s.normalZ);
            w.f32(s.texUX); w.f32(s.texUY); w.f32(s.texUZ);
            w.f32(s.texVX); w.f32(s.texVY); w.f32(s.texVZ);
            w.i32(s.brushPoly);
            w.i32(s.actor);
            w.f32(s.planeX); w.f32(s.planeY); w.f32(s.planeZ); w.f32(s.planeW);
        }
        geometry = w.data();
    }

    // Player starts.
    std::vector<uint8_t> starts;
    {
        Writer w;
        w.u32(static_cast<uint32_t>(map.spawnPoints.size()));
        for (size_t i = 0; i < map.spawnPoints.size(); ++i) {
            w.f32(map.spawnPoints[i].x);
            w.f32(map.spawnPoints[i].y);
            w.f32(map.spawnPoints[i].z);
            const float yaw = i < map.spawnYaw.size() ? map.spawnYaw[i] : 0.0f;
            w.f32(yaw);
        }
        starts = w.data();
    }

    // Materials.
    std::vector<uint8_t> materials;
    {
        Writer w;
        w.u32(static_cast<uint32_t>(map.materials.size()));
        for (const auto& m : map.materials) {
            w.u32(static_cast<uint32_t>(m.size()));
            w.bytes(m.data(), m.size());
        }
        materials = w.data();
    }

    // Textures.
    std::vector<uint8_t> textures;
    {
        Writer w;
        w.u32(static_cast<uint32_t>(map.textures.size()));
        for (const auto& t : map.textures) {
            w.u32(static_cast<uint32_t>(t.name.size()));
            w.bytes(t.name.data(), t.name.size());
            w.i32(t.width);
            w.i32(t.height);
            w.u32(static_cast<uint32_t>(t.rgba.size()));
            w.bytes(t.rgba.data(), t.rgba.size());
        }
        textures = w.data();
    }

    // Assemble the file.
    std::vector<uint8_t> file;
    file.resize(kHeaderSize);
    // Header fields.
    auto patch = [&](size_t off, uint32_t v) {
        file[off] = static_cast<uint8_t>(v & 0xff);
        file[off + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
        file[off + 2] = static_cast<uint8_t>((v >> 16) & 0xff);
        file[off + 3] = static_cast<uint8_t>((v >> 24) & 0xff);
    };
    patch(0, kMagic);
    patch(4, kVersion);
    std::memset(file.data() + 16, 0, 64);
    std::strncpy(reinterpret_cast<char*>(file.data() + 16), map.name, 63);

    struct Section { uint32_t id; const std::vector<uint8_t>* data; };
    std::vector<Section> sections = {
        {static_cast<uint32_t>(SectionId::Geometry), &geometry},
        {static_cast<uint32_t>(SectionId::PlayerStarts), &starts},
        {static_cast<uint32_t>(SectionId::Materials), &materials},
        {static_cast<uint32_t>(SectionId::Textures), &textures},
    };

    // Section table size: 4 (count) + sections * 12.
    const size_t tableSize = 4 + sections.size() * 12;
    size_t cursor = kHeaderSize + tableSize;
    std::vector<size_t> offsets(sections.size());
    for (size_t i = 0; i < sections.size(); ++i) {
        offsets[i] = cursor;
        cursor += sections[i].data->size();
    }

    // Write section table into a buffer, then splice everything.
    Writer table;
    table.u32(static_cast<uint32_t>(sections.size()));
    for (size_t i = 0; i < sections.size(); ++i) {
        table.u32(sections[i].id);
        table.u32(static_cast<uint32_t>(offsets[i]));
        table.u32(static_cast<uint32_t>(sections[i].data->size()));
    }

    std::vector<uint8_t> full;
    full.reserve(cursor);
    full.insert(full.end(), file.begin(), file.end());
    full.insert(full.end(), table.data().begin(), table.data().end());
    for (const auto& s : sections) {
        full.insert(full.end(), s.data->begin(), s.data->end());
    }

    const uint32_t totalSize = static_cast<uint32_t>(full.size());
    const uint32_t crc = crc32(full.data() + kHeaderSize, full.size() - kHeaderSize);

    // Patch the total size and CRC into the assembled buffer (not `file`, which
    // was already copied into `full`).
    auto patchFull = [&](size_t off, uint32_t v) {
        full[off] = static_cast<uint8_t>(v & 0xff);
        full[off + 1] = static_cast<uint8_t>((v >> 8) & 0xff);
        full[off + 2] = static_cast<uint8_t>((v >> 16) & 0xff);
        full[off + 3] = static_cast<uint8_t>((v >> 24) & 0xff);
    };
    patchFull(8, totalSize);
    patchFull(12, crc);

    out = std::move(full);
    return true;
}

bool saveMap(const Map& map, const std::string& path) {
    std::vector<uint8_t> full;
    if (!saveMap(map, full)) {
        return false;
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    std::fwrite(full.data(), 1, full.size(), f);
    std::fclose(f);
    return true;
}

bool loadMap(Map& map, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < static_cast<long>(kHeaderSize)) {
        std::fclose(f);
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return loadMap(map, buf.data(), buf.size());
}

bool loadMap(Map& map, const uint8_t* data, size_t bufSize) {
    const uint8_t* buf = data;

    Reader r(buf, bufSize);
    if (r.u32() != kMagic) {
        return false;
    }
    const uint32_t version = r.u32();
    (void)version;
    const uint32_t totalSize = r.u32();
    const uint32_t crc = r.u32();
    if (totalSize != bufSize) {
        return false;
    }
    if (crc32(buf + kHeaderSize, bufSize - kHeaderSize) != crc) {
        return false;
    }

    std::memcpy(map.name, buf + 16, 64);
    map.name[63] = '\0';

    Reader table(buf + kHeaderSize, bufSize - kHeaderSize);
    const uint32_t sectionCount = table.u32();
    for (uint32_t i = 0; i < sectionCount; ++i) {
        const uint32_t id = table.u32();
        const uint32_t offset = table.u32();
        const uint32_t size = table.u32();
        Reader s(buf + offset, size);

        if (id == static_cast<uint32_t>(SectionId::Geometry)) {
            const uint32_t pointCount = s.u32();
            map.points.resize(pointCount);
            for (auto& p : map.points) {
                p.x = s.f32(); p.y = s.f32(); p.z = s.f32();
            }
            const uint32_t nodeCount = s.u32();
            map.nodes.resize(nodeCount);
            for (auto& n : map.nodes) {
                n.planeX = s.f32(); n.planeY = s.f32(); n.planeZ = s.f32(); n.planeW = s.f32();
                n.vertPool = s.i32();
                n.surf = s.i32();
                n.vertex = s.i32();
                n.collisionBound = s.i32();
                n.zone[0] = s.i8(); n.zone[1] = s.i8();
                n.leaf[0] = s.i8(); n.leaf[1] = s.i8();
                n.numVertices = s.u8();
                n.nodeFlags = s.u8();
            }
            const uint32_t vertCount = s.u32();
            map.verts.resize(vertCount);
            for (auto& v : map.verts) {
                v.pointIndex = s.i32();
                v.side = s.i32();
                v.u = s.f32();
                v.v = s.f32();
            }
            const uint32_t surfCount = s.u32();
            map.surfaces.resize(surfCount);
            for (auto& sf : map.surfaces) {
                sf.materialIndex = s.i32();
                sf.textureIndex = s.i32();
                sf.polyFlags = s.u32();
                sf.pBase = s.i32();
                sf.normalX = s.f32(); sf.normalY = s.f32(); sf.normalZ = s.f32();
                sf.texUX = s.f32(); sf.texUY = s.f32(); sf.texUZ = s.f32();
                sf.texVX = s.f32(); sf.texVY = s.f32(); sf.texVZ = s.f32();
                sf.brushPoly = s.i32();
                sf.actor = s.i32();
                sf.planeX = s.f32(); sf.planeY = s.f32(); sf.planeZ = s.f32(); sf.planeW = s.f32();
            }
        } else if (id == static_cast<uint32_t>(SectionId::PlayerStarts)) {
            const uint32_t count = s.u32();
            map.spawnPoints.resize(count);
            map.spawnYaw.resize(count);
            for (uint32_t k = 0; k < count; ++k) {
                map.spawnPoints[k].x = s.f32();
                map.spawnPoints[k].y = s.f32();
                map.spawnPoints[k].z = s.f32();
                map.spawnYaw[k] = s.f32();
            }
        } else if (id == static_cast<uint32_t>(SectionId::Materials)) {
            const uint32_t count = s.u32();
            map.materials.resize(count);
            for (uint32_t k = 0; k < count; ++k) {
                const uint32_t len = s.u32();
                std::string str(len, '\0');
                s.bytes(&str[0], len);
                map.materials[k] = str;
            }
        } else if (id == static_cast<uint32_t>(SectionId::Textures)) {
            const uint32_t count = s.u32();
            map.textures.resize(count);
            for (uint32_t k = 0; k < count; ++k) {
                const uint32_t nameLen = s.u32();
                std::string name(nameLen, '\0');
                s.bytes(&name[0], nameLen);
                map.textures[k].name = name;
                map.textures[k].width = s.i32();
                map.textures[k].height = s.i32();
                const uint32_t rgbaLen = s.u32();
                map.textures[k].rgba.resize(rgbaLen);
                if (rgbaLen > 0) {
                    s.bytes(map.textures[k].rgba.data(), rgbaLen);
                }
            }
        }
    }

    return true;
}

} // namespace ot::map
