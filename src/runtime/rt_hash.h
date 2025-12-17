//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_hash.h
// Purpose: Cryptographic hash functions (MD5, SHA1, SHA256) and checksums (CRC32).
// Key invariants: Hash outputs are lowercase hex strings; CRC32 returns integer.
// Ownership/Lifetime: Returned strings are newly allocated.
// Links: docs/viperlib.md
//
// Security Note: MD5 and SHA1 are cryptographically broken and should NOT be
//                used for security-sensitive applications. Use SHA256 or better
//                for security purposes. These are provided for checksums,
//                fingerprinting, and legacy compatibility.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Compute MD5 hash of a string.
    /// @param str Input string.
    /// @return 32-character lowercase hex string.
    rt_string rt_hash_md5(rt_string str);

    /// @brief Compute MD5 hash of a Bytes object.
    /// @param bytes Input bytes.
    /// @return 32-character lowercase hex string.
    rt_string rt_hash_md5_bytes(void *bytes);

    /// @brief Compute SHA1 hash of a string.
    /// @param str Input string.
    /// @return 40-character lowercase hex string.
    rt_string rt_hash_sha1(rt_string str);

    /// @brief Compute SHA1 hash of a Bytes object.
    /// @param bytes Input bytes.
    /// @return 40-character lowercase hex string.
    rt_string rt_hash_sha1_bytes(void *bytes);

    /// @brief Compute SHA256 hash of a string.
    /// @param str Input string.
    /// @return 64-character lowercase hex string.
    rt_string rt_hash_sha256(rt_string str);

    /// @brief Compute SHA256 hash of a Bytes object.
    /// @param bytes Input bytes.
    /// @return 64-character lowercase hex string.
    rt_string rt_hash_sha256_bytes(void *bytes);

    /// @brief Compute CRC32 checksum of a string.
    /// @param str Input string.
    /// @return CRC32 value as integer.
    int64_t rt_hash_crc32(rt_string str);

    /// @brief Compute CRC32 checksum of a Bytes object.
    /// @param bytes Input bytes.
    /// @return CRC32 value as integer.
    int64_t rt_hash_crc32_bytes(void *bytes);

#ifdef __cplusplus
}
#endif
