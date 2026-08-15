#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ot::ue2 {

struct FObjectImport {
    int32_t classPackage = 0; // FName index (compact on disk)
    int32_t className = 0;    // FName index (compact)
    int32_t package = 0;      // int32 (outer)
    int32_t objectName = 0;   // FName index (compact)
};

struct FObjectExport {
    int32_t cls = 0;          // FPackageIndex (compact)
    int32_t super = 0;        // FPackageIndex (compact)
    int32_t package = 0;      // int32 (outer, 1-based export index)
    int32_t objectName = 0;   // FName index (compact)
    uint32_t objectFlags = 0; // uint32
    int32_t serialSize = 0;   // compact
    int32_t serialOffset = 0; // compact (present only if serialSize != 0)
};

class Package {
public:
    bool open(const std::string& path);

    const std::string& name(int32_t index) const;

    // Resolve an FPackageIndex to an object name.
    std::string resolveIndex(int32_t v) const;

    // Class name of an export.
    std::string exportClass(int exportIndex) const;

    // Object name of an export.
    std::string exportName(int exportIndex) const;

    // Find the export index of the object with the given class name and
    // (optionally) outer export index.
    int findExport(const std::string& className, int outer = -1) const;

    int exportCount() const { return static_cast<int>(m_exports.size()); }
    const FObjectExport& exp(int i) const { return m_exports[i]; }

    int importCount() const { return static_cast<int>(m_imports.size()); }
    const FObjectImport& imp(int i) const { return m_imports[i]; }

    const uint8_t* data() const { return m_data.data(); }
    size_t size() const { return m_data.size(); }
    uint16_t fileVersion() const { return m_fileVersion; }
    uint32_t nameCount() const { return m_nameCount; }

    static int32_t readCompact(const uint8_t*& p, const uint8_t* end);

private:
    bool readHeader(size_t& nameOffset, size_t& importOffset, size_t& exportOffset);

    std::vector<uint8_t> m_data;
    uint16_t m_fileVersion = 0;
    uint16_t m_licenseeVersion = 0;
    uint32_t m_packageFlags = 0;
    uint32_t m_nameCount = 0;
    uint32_t m_exportCount = 0;
    uint32_t m_importCount = 0;
    std::vector<std::string> m_names;
    std::vector<FObjectImport> m_imports;
    std::vector<FObjectExport> m_exports;
};

} // namespace ot::ue2
