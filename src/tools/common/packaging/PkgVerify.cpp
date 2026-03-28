//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgVerify.cpp
// Purpose: Post-build verification of generated packages.
//
// Key invariants:
//   - Read-only: never modifies input data.
//   - Returns false on first structural error found.
//
// Ownership/Lifetime:
//   - Pure functions, no state.
//
// Links: PkgVerify.hpp
//
//===----------------------------------------------------------------------===//

#include "PkgVerify.hpp"

#include <cstring>

namespace viper::pkg {

namespace {

uint16_t rdLE16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t rdLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

// ============================================================================
// ZIP Verification
// ============================================================================

bool verifyZip(const std::vector<uint8_t> &data, std::ostream &err) {
    if (data.size() < 22) {
        err << "ZIP: file too small (" << data.size() << " bytes)\n";
        return false;
    }

    // Find end-of-central-directory record (scan backwards)
    // EOCD signature = 0x06054B50
    bool foundEocd = false;
    size_t eocdOff = 0;
    // EOCD can have a variable-length comment, so scan from end
    size_t searchStart = (data.size() > 65557) ? data.size() - 65557 : 0;
    for (size_t i = data.size() - 22; i >= searchStart; --i) {
        if (rdLE32(data.data() + i) == 0x06054B50) {
            foundEocd = true;
            eocdOff = i;
            break;
        }
        if (i == 0)
            break;
    }

    if (!foundEocd) {
        err << "ZIP: end-of-central-directory signature not found\n";
        return false;
    }

    // Read entry count from EOCD
    uint16_t totalEntries = rdLE16(data.data() + eocdOff + 10);
    uint32_t cdOffset = rdLE32(data.data() + eocdOff + 16);

    // Verify central directory entries
    size_t pos = cdOffset;
    uint16_t entriesFound = 0;
    while (pos + 46 <= data.size() && entriesFound < totalEntries) {
        if (rdLE32(data.data() + pos) != 0x02014B50) {
            err << "ZIP: invalid central directory header at offset " << pos << "\n";
            return false;
        }
        uint16_t nameLen = rdLE16(data.data() + pos + 28);
        uint16_t extraLen = rdLE16(data.data() + pos + 30);
        uint16_t commentLen = rdLE16(data.data() + pos + 32);

        // Verify the corresponding local header exists
        uint32_t localOff = rdLE32(data.data() + pos + 42);
        if (localOff + 30 > data.size()) {
            err << "ZIP: local header offset " << localOff << " out of bounds\n";
            return false;
        }
        if (rdLE32(data.data() + localOff) != 0x04034B50) {
            err << "ZIP: invalid local file header at offset " << localOff << "\n";
            return false;
        }

        pos += 46 + nameLen + extraLen + commentLen;
        entriesFound++;
    }

    if (entriesFound != totalEntries) {
        err << "ZIP: expected " << totalEntries << " entries, found " << entriesFound << "\n";
        return false;
    }

    return true;
}

// ============================================================================
// .deb (ar) Verification
// ============================================================================

bool verifyDeb(const std::vector<uint8_t> &data, std::ostream &err) {
    // Check ar magic
    if (data.size() < 8) {
        err << "DEB: file too small (" << data.size() << " bytes)\n";
        return false;
    }

    if (std::memcmp(data.data(), "!<arch>\n", 8) != 0) {
        err << "DEB: missing ar magic '!<arch>\\n'\n";
        return false;
    }

    // Parse first member header (60 bytes at offset 8)
    if (data.size() < 68) {
        err << "DEB: no member headers found\n";
        return false;
    }

    // Member name: first 16 bytes, "/" terminated
    // Check for "debian-binary/"
    if (std::memcmp(data.data() + 8, "debian-binary/", 14) != 0) {
        err << "DEB: first member is not 'debian-binary'\n";
        return false;
    }

    // File magic at end of header: "`\n"
    if (data[8 + 58] != '`' || data[8 + 59] != '\n') {
        err << "DEB: invalid member header terminator\n";
        return false;
    }

    // Parse size field (offset 48 in header, 10 bytes, decimal ASCII)
    char sizeBuf[11] = {};
    std::memcpy(sizeBuf, data.data() + 8 + 48, 10);
    size_t memberSize = 0;
    for (int i = 0; i < 10 && sizeBuf[i] != ' '; ++i)
        memberSize = memberSize * 10 + static_cast<size_t>(sizeBuf[i] - '0');

    // Content of debian-binary should be "2.0\n"
    size_t contentOff = 68; // 8 (ar magic) + 60 (header)
    if (contentOff + memberSize > data.size()) {
        err << "DEB: debian-binary content truncated\n";
        return false;
    }

    if (memberSize < 4 || std::memcmp(data.data() + contentOff, "2.0\n", 4) != 0) {
        err << "DEB: debian-binary content is not '2.0\\n'\n";
        return false;
    }

    // Scan for control.tar.gz and data.tar.gz members
    bool foundControl = false;
    bool foundData = false;
    size_t pos = 8; // after ar magic
    while (pos + 60 <= data.size()) {
        // Check header terminator
        if (data[pos + 58] != '`' || data[pos + 59] != '\n')
            break;

        // Read name (16 bytes)
        std::string name(reinterpret_cast<const char *>(data.data() + pos), 16);
        // Trim trailing spaces and "/"
        auto end = name.find('/');
        if (end != std::string::npos)
            name = name.substr(0, end);

        if (name == "control.tar.gz")
            foundControl = true;
        if (name == "data.tar.gz")
            foundData = true;

        // Read size
        char sb[11] = {};
        std::memcpy(sb, data.data() + pos + 48, 10);
        size_t sz = 0;
        for (int i = 0; i < 10 && sb[i] != ' '; ++i)
            sz = sz * 10 + static_cast<size_t>(sb[i] - '0');

        // Advance past header + data + optional padding
        pos += 60 + sz;
        if (sz % 2 != 0)
            pos++; // odd-size padding
    }

    if (!foundControl) {
        err << "DEB: 'control.tar.gz' member not found\n";
        return false;
    }
    if (!foundData) {
        err << "DEB: 'data.tar.gz' member not found\n";
        return false;
    }

    return true;
}

// ============================================================================
// PE Verification
// ============================================================================

bool verifyPE(const std::vector<uint8_t> &data, std::ostream &err) {
    if (data.size() < 64) {
        err << "PE: file too small (" << data.size() << " bytes)\n";
        return false;
    }

    // DOS header: "MZ" magic
    if (data[0] != 'M' || data[1] != 'Z') {
        err << "PE: missing DOS 'MZ' magic\n";
        return false;
    }

    // e_lfanew at offset 60: pointer to PE signature
    uint32_t peOff = rdLE32(data.data() + 60);
    if (peOff + 4 > data.size()) {
        err << "PE: e_lfanew (" << peOff << ") points past end of file\n";
        return false;
    }

    // PE signature: "PE\0\0"
    if (data[peOff] != 'P' || data[peOff + 1] != 'E' || data[peOff + 2] != 0 ||
        data[peOff + 3] != 0) {
        err << "PE: invalid PE signature at offset " << peOff << "\n";
        return false;
    }

    // COFF header at peOff+4
    uint32_t coffOff = peOff + 4;
    if (coffOff + 20 > data.size()) {
        err << "PE: COFF header truncated\n";
        return false;
    }

    uint16_t machine = rdLE16(data.data() + coffOff);
    if (machine != 0x8664 && machine != 0xAA64) {
        err << "PE: unexpected machine type 0x" << std::hex << machine << std::dec << "\n";
        return false;
    }

    uint16_t numSections = rdLE16(data.data() + coffOff + 2);
    uint16_t optHdrSize = rdLE16(data.data() + coffOff + 16);

    // Optional header
    uint32_t optOff = coffOff + 20;
    if (optOff + optHdrSize > data.size()) {
        err << "PE: optional header truncated\n";
        return false;
    }

    // PE32+ magic = 0x020B
    uint16_t optMagic = rdLE16(data.data() + optOff);
    if (optMagic != 0x020B) {
        err << "PE: expected PE32+ magic 0x020B, got 0x" << std::hex << optMagic << std::dec
            << "\n";
        return false;
    }

    // Verify section headers don't overlap
    uint32_t secTableOff = optOff + optHdrSize;

    struct SecInfo {
        uint32_t rawOff;
        uint32_t rawSize;
    };

    std::vector<SecInfo> sections;

    for (uint16_t i = 0; i < numSections; ++i) {
        uint32_t hdrOff = secTableOff + i * 40;
        if (hdrOff + 40 > data.size()) {
            err << "PE: section header " << i << " truncated\n";
            return false;
        }
        uint32_t rawSize = rdLE32(data.data() + hdrOff + 16);
        uint32_t rawOff = rdLE32(data.data() + hdrOff + 20);
        if (rawSize > 0)
            sections.push_back({rawOff, rawSize});
    }

    // Check for overlap
    for (size_t i = 0; i < sections.size(); ++i) {
        for (size_t j = i + 1; j < sections.size(); ++j) {
            uint32_t aEnd = sections[i].rawOff + sections[i].rawSize;
            uint32_t bEnd = sections[j].rawOff + sections[j].rawSize;
            bool overlap = (sections[i].rawOff < bEnd && sections[j].rawOff < aEnd);
            if (overlap) {
                err << "PE: sections " << i << " and " << j << " overlap\n";
                return false;
            }
        }
    }

    return true;
}

} // namespace viper::pkg
