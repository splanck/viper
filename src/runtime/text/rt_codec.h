//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_codec.h
// Purpose: Base64, Hex, and URL encoding/decoding utilities for string-based transformations used in network protocols, configuration, and data serialization.
//
// Key invariants:
//   - Base64 encoding uses the standard alphabet (A-Z, a-z, 0-9, +, /); output includes padding.
//   - URL encoding uses percent-encoding for reserved characters.
//   - Hex encoding uses lowercase hexadecimal digits.
//   - Decoding functions return NULL on invalid input.
//
// Ownership/Lifetime:
//   - Returned strings are newly allocated; caller must release.
//   - Input strings are borrowed; callers retain ownership.
//
// Links: src/runtime/text/rt_codec.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief URL-encode a string (percent-encoding).
    /// @param str String to encode.
    /// @return Newly allocated URL-encoded string.
    /// @details Leaves A-Z, a-z, 0-9, -_.~ unchanged; encodes all others as %XX.
    rt_string rt_codec_url_encode(rt_string str);

    /// @brief URL-decode a string (percent-decoding).
    /// @param str URL-encoded string to decode.
    /// @return Newly allocated decoded string.
    /// @details Decodes %XX sequences; treats + as space.
    rt_string rt_codec_url_decode(rt_string str);

    /// @brief Base64-encode a string's bytes.
    /// @param str String to encode.
    /// @return Newly allocated Base64 string.
    rt_string rt_codec_base64_enc(rt_string str);

    /// @brief Base64-decode a string.
    /// @param str Base64-encoded string.
    /// @return Newly allocated decoded string.
    /// @details Traps on invalid Base64 input.
    rt_string rt_codec_base64_dec(rt_string str);

    /// @brief Hex-encode a string's bytes.
    /// @param str String to encode.
    /// @return Newly allocated lowercase hex string.
    rt_string rt_codec_hex_enc(rt_string str);

    /// @brief Hex-encode raw byte data.
    /// @param data Pointer to byte data to encode.
    /// @param len Length of data in bytes.
    /// @return Newly allocated lowercase hex string.
    /// @details Used by rt_hash for hash output formatting.
    rt_string rt_codec_hex_enc_bytes(const uint8_t *data, size_t len);

    /// @brief Hex-decode a string.
    /// @param str Hex-encoded string.
    /// @return Newly allocated decoded string.
    /// @details Traps on invalid hex input (odd length or invalid chars).
    rt_string rt_codec_hex_dec(rt_string str);

#ifdef __cplusplus
}
#endif
