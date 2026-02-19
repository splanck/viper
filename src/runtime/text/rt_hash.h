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

    //=========================================================================
    // HMAC Functions
    //=========================================================================

    /// @brief Compute HMAC-MD5 of string data with string key.
    /// @param key The secret key string.
    /// @param data The data string to authenticate.
    /// @return 32-character lowercase hex string.
    rt_string rt_hash_hmac_md5(rt_string key, rt_string data);

    /// @brief Compute HMAC-MD5 of Bytes data with Bytes key.
    /// @param key The secret key as Bytes.
    /// @param data The data as Bytes.
    /// @return 32-character lowercase hex string.
    rt_string rt_hash_hmac_md5_bytes(void *key, void *data);

    /// @brief Compute HMAC-SHA1 of string data with string key.
    /// @param key The secret key string.
    /// @param data The data string to authenticate.
    /// @return 40-character lowercase hex string.
    rt_string rt_hash_hmac_sha1(rt_string key, rt_string data);

    /// @brief Compute HMAC-SHA1 of Bytes data with Bytes key.
    /// @param key The secret key as Bytes.
    /// @param data The data as Bytes.
    /// @return 40-character lowercase hex string.
    rt_string rt_hash_hmac_sha1_bytes(void *key, void *data);

    /// @brief Compute HMAC-SHA256 of string data with string key.
    /// @param key The secret key string.
    /// @param data The data string to authenticate.
    /// @return 64-character lowercase hex string.
    rt_string rt_hash_hmac_sha256(rt_string key, rt_string data);

    /// @brief Compute HMAC-SHA256 of Bytes data with Bytes key.
    /// @param key The secret key as Bytes.
    /// @param data The data as Bytes.
    /// @return 64-character lowercase hex string.
    rt_string rt_hash_hmac_sha256_bytes(void *key, void *data);

    //=========================================================================
    // Fast Non-Cryptographic Hash (FNV-1a)
    //=========================================================================

    /// @brief Compute fast FNV-1a hash of a string.
    /// @param str Input string.
    /// @return 64-bit hash value (as signed i64).
    int64_t rt_hash_fast(rt_string str);

    /// @brief Compute fast FNV-1a hash of a Bytes object.
    /// @param bytes Input bytes.
    /// @return 64-bit hash value (as signed i64).
    int64_t rt_hash_fast_bytes(void *bytes);

    /// @brief Compute fast FNV-1a hash of an integer.
    /// @param value Input integer.
    /// @return 64-bit hash value (as signed i64).
    int64_t rt_hash_fast_int(int64_t value);

    //=========================================================================
    // Internal HMAC functions (for PBKDF2)
    //=========================================================================

    /// @brief Compute raw HMAC-SHA256 (returns binary, not hex).
    /// @param key Key bytes.
    /// @param key_len Key length.
    /// @param data Data bytes.
    /// @param data_len Data length.
    /// @param out Output buffer (32 bytes).
    void rt_hash_hmac_sha256_raw(
        const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32]);

#ifdef __cplusplus
}
#endif
