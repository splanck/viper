//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgMD5.hpp
// Purpose: Self-contained MD5 digest for the packaging library.
//          Ported from src/runtime/text/rt_hash.c with all GC deps removed.
//
// Key invariants:
//   - No runtime (viper_rt_*) dependencies — fully self-contained.
//   - Produces RFC 1321 compliant 16-byte digests.
//
// Ownership/Lifetime:
//   - Digest is written to caller-provided uint8_t[16] buffer.
//
// Links: src/runtime/text/rt_hash.c (original implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace viper::pkg {

/// @brief Compute MD5 digest of input data.
/// @param data Input bytes.
/// @param len Length of input.
/// @param digest Output buffer for 16-byte digest.
void md5(const uint8_t *data, size_t len, uint8_t digest[16]);

/// @brief Compute MD5 digest and return as 32-char lowercase hex string.
/// @param data Input bytes.
/// @param len Length of input.
/// @return Hex-encoded MD5 digest string.
std::string md5hex(const uint8_t *data, size_t len);

} // namespace viper::pkg
