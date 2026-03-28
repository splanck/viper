//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgDeflate.hpp
// Purpose: Self-contained DEFLATE compression and decompression for the
//          packaging library. Ported from src/runtime/io/rt_compress.c with
//          all GC dependencies removed. Uses std::vector for output buffers.
//
// Key invariants:
//   - No runtime (viper_rt_*) dependencies — fully self-contained.
//   - Compression levels 1-9 are supported (default 6).
//   - Implements RFC 1951 DEFLATE with LZ77 + fixed Huffman coding.
//   - Decompression supports stored, fixed Huffman, and dynamic Huffman blocks.
//
// Ownership/Lifetime:
//   - All output is returned as std::vector<uint8_t> (caller-owned).
//   - Input data is read-only and not retained.
//
// Links: src/runtime/io/rt_compress.c (original implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace viper::pkg {

/// @brief Error thrown when compression or decompression fails.
class DeflateError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// @brief Compress data using raw DEFLATE (RFC 1951).
/// @param data Input bytes to compress.
/// @param len Length of input data.
/// @param level Compression level 1-9 (default 6). Level 1 uses stored blocks.
/// @return Compressed DEFLATE stream.
/// @throws DeflateError on compression failure.
std::vector<uint8_t> deflate(const uint8_t *data, size_t len, int level = 6);

/// @brief Decompress a raw DEFLATE stream.
/// @param data Compressed DEFLATE data.
/// @param len Length of compressed data.
/// @return Decompressed bytes.
/// @throws DeflateError on invalid or truncated data.
std::vector<uint8_t> inflate(const uint8_t *data, size_t len);

} // namespace viper::pkg
