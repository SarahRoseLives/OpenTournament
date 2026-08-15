#include "UE2.h"

#include <cstdio>
#include <cstring>

namespace ot::ue2 {

namespace {

uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int32_t rdi32(const uint8_t* p) {
    return static_cast<int32_t>(rd32(p));
}

} // namespace

// UE1/UE2 variable-length index (FCompactIndex).
int32_t Package::readCompact(const uint8_t*& p, const uint8_t* end) {
    if (p >= end) {
        return 0;
    }
    uint8_t b = *p++;
    const int sign = b & 0x80;
    int shift = 6;
    int r = b & 0x3F;
    if (b & 0x40) {
        do {
            if (p >= end) {
                return 0;
            }
            b = *p++;
            r |= (b & 0x7F) << shift;
            shift += 7;
        } while (b & 0x80);
    }
    return sign ? -r : r;
}

bool Package::open(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "[ue2] cannot open %s\n", path.c_str());
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    m_data.resize(static_cast<size_t>(sz));
    std::fread(m_data.data(), 1, m_data.size(), f);
    std::fclose(f);

    size_t nameOffset = 0;
    size_t importOffset = 0;
    size_t exportOffset = 0;
    if (!readHeader(nameOffset, importOffset, exportOffset)) {
        return false;
    }

    const uint8_t* end = m_data.data() + m_data.size();

    // Name table.
    m_names.reserve(m_nameCount);
    size_t pos = nameOffset;
    for (uint32_t i = 0; i < m_nameCount; ++i) {
        if (pos >= m_data.size()) {
            break;
        }
        const uint8_t len = m_data[pos++];
        if (pos + len > m_data.size()) {
            break;
        }
        std::string s(reinterpret_cast<const char*>(m_data.data() + pos), len);
        pos += len;
        if (!s.empty() && s.back() == '\0') {
            s.pop_back();
        }
        if (pos + 4 <= m_data.size()) {
            pos += 4; // flags (uint32)
        }
        m_names.push_back(std::move(s));
    }

    // Import table (variable-length entries).
    m_imports.reserve(m_importCount);
    {
        const uint8_t* p = m_data.data() + importOffset;
        for (uint32_t i = 0; i < m_importCount && p < end; ++i) {
            FObjectImport imp;
            imp.classPackage = readCompact(p, end);
            imp.className = readCompact(p, end);
            imp.package = rdi32(p);
            p += 4;
            imp.objectName = readCompact(p, end);
            m_imports.push_back(imp);
        }
    }

    // Export table (variable-length entries).
    m_exports.reserve(m_exportCount);
    {
        const uint8_t* p = m_data.data() + exportOffset;
        for (uint32_t i = 0; i < m_exportCount && p < end; ++i) {
            FObjectExport e;
            e.cls = readCompact(p, end);
            e.super = readCompact(p, end);
            e.package = rdi32(p);
            p += 4;
            e.objectName = readCompact(p, end);
            e.objectFlags = rd32(p);
            p += 4;
            e.serialSize = readCompact(p, end);
            e.serialOffset = (e.serialSize != 0) ? readCompact(p, end) : 0;
            m_exports.push_back(e);
        }
    }

    return true;
}

bool Package::readHeader(size_t& nameOffset, size_t& importOffset, size_t& exportOffset) {
    if (m_data.size() < 64) {
        return false;
    }
    const uint32_t tag = rd32(m_data.data());
    if (tag != 0x9E2A83C1u) {
        std::fprintf(stderr, "[ue2] bad package tag 0x%08X\n", tag);
        return false;
    }
    m_fileVersion = rd16(m_data.data() + 4);
    m_licenseeVersion = rd16(m_data.data() + 6);
    m_packageFlags = rd32(m_data.data() + 8);
    m_nameCount = rd32(m_data.data() + 12);
    nameOffset = rd32(m_data.data() + 16);
    m_exportCount = rd32(m_data.data() + 20);
    exportOffset = rd32(m_data.data() + 24);
    m_importCount = rd32(m_data.data() + 28);
    importOffset = rd32(m_data.data() + 32);
    return true;
}

const std::string& Package::name(int32_t index) const {
    static const std::string kNone = "None";
    if (index < 0 || index >= static_cast<int32_t>(m_names.size())) {
        return kNone;
    }
    return m_names[index];
}

std::string Package::resolveIndex(int32_t v) const {
    if (v == 0) {
        return "None";
    }
    if (v < 0) {
        const int32_t i = -v - 1;
        if (i < static_cast<int32_t>(m_imports.size())) {
            return name(m_imports[i].objectName);
        }
        return "None";
    }
    const int32_t i = v - 1;
    if (i < static_cast<int32_t>(m_exports.size())) {
        return name(m_exports[i].objectName);
    }
    return "None";
}

std::string Package::exportClass(int exportIndex) const {
    return resolveIndex(m_exports[exportIndex].cls);
}

std::string Package::exportName(int exportIndex) const {
    return name(m_exports[exportIndex].objectName);
}

int Package::findExport(const std::string& className, int outer) const {
    for (int i = 0; i < static_cast<int>(m_exports.size()); ++i) {
        if (exportClass(i) != className) {
            continue;
        }
        // `package` is a 1-based index into the export table (0 = none).
        const int outerIdx = m_exports[i].package - 1;
        if (outer < 0 || outerIdx == outer) {
            return i;
        }
    }
    return -1;
}

} // namespace ot::ue2
