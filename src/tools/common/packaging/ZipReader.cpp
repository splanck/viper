//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/ZipReader.cpp
// Purpose: Minimal ZIP archive reader — parses central directory and extracts
//          entries using stored or DEFLATE decompression.
//
// Key invariants:
//   - Scans backwards for EOCD signature (handles ZIP comments).
//   - Validates CRC-32 after extraction.
//   - Thread-safe: no mutable state after construction.
//
// Ownership/Lifetime:
//   - References external buffer. Does not copy input data.
//
// Links: ZipReader.hpp, PkgDeflate.hpp
//
//===----------------------------------------------------------------------===//

#include "ZipReader.hpp"
#include "PkgDeflate.hpp"

#include <cstring>

extern "C" {
uint32_t rt_crc32_compute(const uint8_t *data, size_t len);
}

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

ZipReader::ZipReader(const uint8_t *data, size_t len) : data_(data), len_(len) {
    if (!data || len < 22)
        throw ZipReadError("ZIP: buffer too small");
    parseCentralDirectory();
}

void ZipReader::parseCentralDirectory() {
    // Scan backwards for EOCD signature 0x06054B50
    // EOCD can have up to 65535 bytes of comment, so search range is limited
    size_t searchStart = (len_ > 65557) ? len_ - 65557 : 0;
    size_t eocdOff = 0;
    bool found = false;

    for (size_t i = len_ - 22; i >= searchStart; --i) {
        if (rdLE32(data_ + i) == 0x06054B50) {
            eocdOff = i;
            found = true;
            break;
        }
        if (i == 0)
            break;
    }

    if (!found)
        throw ZipReadError("ZIP: end-of-central-directory signature not found");

    uint16_t totalEntries = rdLE16(data_ + eocdOff + 10);
    uint32_t cdOffset = rdLE32(data_ + eocdOff + 16);

    if (cdOffset >= len_)
        throw ZipReadError("ZIP: central directory offset out of bounds");

    // Parse central directory entries
    size_t pos = cdOffset;
    for (uint16_t i = 0; i < totalEntries; ++i) {
        if (pos + 46 > len_)
            throw ZipReadError("ZIP: central directory entry truncated");

        if (rdLE32(data_ + pos) != 0x02014B50)
            throw ZipReadError("ZIP: invalid central directory header signature");

        ZipEntry entry;
        entry.method = rdLE16(data_ + pos + 10);
        entry.crc32 = rdLE32(data_ + pos + 16);
        entry.compressedSize = rdLE32(data_ + pos + 20);
        entry.uncompressedSize = rdLE32(data_ + pos + 24);

        uint16_t nameLen = rdLE16(data_ + pos + 28);
        uint16_t extraLen = rdLE16(data_ + pos + 30);
        uint16_t commentLen = rdLE16(data_ + pos + 32);
        entry.localHeaderOffset = rdLE32(data_ + pos + 42);

        if (pos + 46 + nameLen > len_)
            throw ZipReadError("ZIP: entry name truncated");

        entry.name.assign(reinterpret_cast<const char *>(data_ + pos + 46), nameLen);
        entries_.push_back(std::move(entry));

        pos += 46 + nameLen + extraLen + commentLen;
    }
}

const ZipEntry *ZipReader::find(const std::string &name) const {
    for (const auto &e : entries_) {
        if (e.name == name)
            return &e;
    }
    return nullptr;
}

std::vector<uint8_t> ZipReader::extract(const ZipEntry &entry) const {
    // Navigate to local file header
    size_t lhOff = entry.localHeaderOffset;
    if (lhOff + 30 > len_)
        throw ZipReadError("ZIP: local header offset out of bounds");

    if (rdLE32(data_ + lhOff) != 0x04034B50)
        throw ZipReadError("ZIP: invalid local file header signature");

    uint16_t lhNameLen = rdLE16(data_ + lhOff + 26);
    uint16_t lhExtraLen = rdLE16(data_ + lhOff + 28);
    size_t dataOff = lhOff + 30 + lhNameLen + lhExtraLen;

    if (dataOff + entry.compressedSize > len_)
        throw ZipReadError("ZIP: entry data extends past buffer");

    std::vector<uint8_t> result;

    if (entry.method == 0) {
        // Stored — direct copy
        result.assign(data_ + dataOff, data_ + dataOff + entry.compressedSize);
    } else if (entry.method == 8) {
        // DEFLATE
        result = inflate(data_ + dataOff, entry.compressedSize);
    } else {
        throw ZipReadError("ZIP: unsupported compression method " + std::to_string(entry.method));
    }

    // Verify CRC-32 (skip for directories which have crc=0 and size=0)
    if (entry.uncompressedSize > 0) {
        uint32_t actualCrc = rt_crc32_compute(result.data(), result.size());
        if (actualCrc != entry.crc32)
            throw ZipReadError("ZIP: CRC-32 mismatch for '" + entry.name + "'");
    }

    return result;
}

} // namespace viper::pkg
