//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgGzip.cpp
// Purpose: GZIP compression — 10-byte header + DEFLATE + 8-byte CRC/size
//          trailer. Ported from rt_compress.c gzip_data().
//
// Key invariants:
//   - Header uses method=DEFLATE(08), flags=0, mtime=0, OS=0xFF(unknown).
//   - CRC-32 is computed on the uncompressed input data.
//   - Size in trailer is original size mod 2^32.
//
// Ownership/Lifetime:
//   - Returned vector owns all memory.
//
// Links: PkgDeflate.hpp (DEFLATE), src/runtime/core/rt_crc32.h (CRC-32)
//
//===----------------------------------------------------------------------===//

#include "PkgGzip.hpp"
#include "PkgDeflate.hpp"

#include <cstring>

// rt_crc32 has no GC dependency — link directly
extern "C" {
uint32_t rt_crc32_compute(const uint8_t *data, size_t len);
}

namespace viper::pkg {

std::vector<uint8_t> gzip(const uint8_t *data, size_t len, int level) {
    // Compress with raw DEFLATE
    auto deflated = deflate(data, len, level);

    // CRC-32 of original data
    uint32_t crc = rt_crc32_compute(data, len);

    // Assemble: 10-byte header + deflated + 8-byte trailer
    size_t totalLen = 10 + deflated.size() + 8;
    std::vector<uint8_t> result(totalLen);
    uint8_t *out = result.data();

    // GZIP header (RFC 1952)
    out[0] = 0x1F; // Magic
    out[1] = 0x8B; // Magic
    out[2] = 0x08; // Compression method (deflate)
    out[3] = 0x00; // Flags (none)
    out[4] = 0x00; // MTIME
    out[5] = 0x00;
    out[6] = 0x00;
    out[7] = 0x00;
    out[8] = 0x00; // XFL
    out[9] = 0xFF; // OS (unknown)

    // Compressed data
    std::memcpy(out + 10, deflated.data(), deflated.size());

    // Trailer: CRC-32 + original size (both little-endian)
    size_t tp = 10 + deflated.size();
    out[tp + 0] = crc & 0xFF;
    out[tp + 1] = (crc >> 8) & 0xFF;
    out[tp + 2] = (crc >> 16) & 0xFF;
    out[tp + 3] = (crc >> 24) & 0xFF;
    out[tp + 4] = len & 0xFF;
    out[tp + 5] = (len >> 8) & 0xFF;
    out[tp + 6] = (len >> 16) & 0xFF;
    out[tp + 7] = (len >> 24) & 0xFF;

    return result;
}

} // namespace viper::pkg
