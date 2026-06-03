//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tools/common/packaging/PkgHash.hpp
// Purpose: Small SHA helpers used by native package writers.
// Key invariants: Pure functions; identical input bytes always yield identical
//                 digests. Self-contained implementations with no external
//                 crypto dependency.
// Ownership/Lifetime: Callers own the input buffer and the returned values.
// Links: src/tools/common/packaging/PkgHash.cpp, XarWriter.cpp (checksums)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace viper::pkg {

/// @brief Compute the SHA-1 digest of a byte buffer.
/// @note Used only for archive integrity/identification (e.g. XAR TOC checksums),
///       not as a security primitive.
/// @param data Pointer to the input bytes (may be null only when @p len is 0).
/// @param len Number of bytes to hash.
/// @return The 20-byte SHA-1 digest.
std::array<uint8_t, 20> sha1Bytes(const uint8_t *data, size_t len);

/// @brief Compute the SHA-1 digest of a byte buffer as a lowercase hex string.
/// @param data Pointer to the input bytes (may be null only when @p len is 0).
/// @param len Number of bytes to hash.
/// @return A 40-character lowercase hexadecimal digest.
std::string sha1Hex(const uint8_t *data, size_t len);

/// @brief Compute the SHA-256 digest of a byte buffer.
/// @param data Pointer to the input bytes (may be null only when @p len is 0).
/// @param len Number of bytes to hash.
/// @return The 32-byte SHA-256 digest.
std::array<uint8_t, 32> sha256Bytes(const uint8_t *data, size_t len);

/// @brief Compute the SHA-256 digest of a byte buffer as a lowercase hex string.
/// @param data Pointer to the input bytes (may be null only when @p len is 0).
/// @param len Number of bytes to hash.
/// @return A 64-character lowercase hexadecimal digest.
std::string sha256Hex(const uint8_t *data, size_t len);

} // namespace viper::pkg
