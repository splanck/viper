//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgGzip.hpp
// Purpose: GZIP compression (RFC 1952) for the packaging library.
//          Wraps DEFLATE output with GZIP header and CRC-32/size trailer.
//
// Key invariants:
//   - No runtime (viper_rt_*) dependencies — uses PkgDeflate + rt_crc32.
//   - Produces standard GZIP streams decompressible by gunzip/zlib.
//
// Ownership/Lifetime:
//   - Output returned as std::vector<uint8_t> (caller-owned).
//
// Links: PkgDeflate.hpp, src/runtime/core/rt_crc32.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace viper::pkg
{

/// @brief Compress data with GZIP wrapper (RFC 1952).
/// @param data Input bytes to compress.
/// @param len Length of input data.
/// @param level DEFLATE compression level 1-9 (default 6).
/// @return GZIP-compressed stream.
std::vector<uint8_t> gzip(const uint8_t *data, size_t len, int level = 6);

} // namespace viper::pkg
