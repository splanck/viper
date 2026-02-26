//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_guid.h
// Purpose: UUID version 4 (random) generation and manipulation per RFC 4122, producing lowercase
// hex strings in standard 8-4-4-4-12 hyphenated format.
//
// Key invariants:
//   - Generates RFC 4122 version 4 UUIDs with proper version and variant bits.
//   - Output is always lowercase hex with hyphens: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx.
//   - Uses a cryptographically secure random source where available.
//   - rt_guid_is_valid validates the format but not the version/variant bits.
//
// Ownership/Lifetime:
//   - Returned strings are newly allocated; caller must release.
//   - No persistent state; each call generates an independent GUID.
//
// Links: src/runtime/text/rt_guid.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Generate a new random UUID v4.
    /// @return Newly allocated string in format xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx.
    rt_string rt_guid_new(void);

    /// @brief Return the nil UUID (all zeros).
    /// @return Static string "00000000-0000-0000-0000-000000000000".
    rt_string rt_guid_empty(void);

    /// @brief Check if string is a valid GUID format.
    /// @param str String to validate.
    /// @return 1 if valid, 0 otherwise.
    int8_t rt_guid_is_valid(rt_string str);

    /// @brief Convert GUID string to 16-byte array.
    /// @param str GUID string to convert.
    /// @return Bytes object containing 16 bytes.
    void *rt_guid_to_bytes(rt_string str);

    /// @brief Convert 16-byte array to GUID string.
    /// @param bytes Bytes object containing exactly 16 bytes.
    /// @return Newly allocated GUID string.
    rt_string rt_guid_from_bytes(void *bytes);

#ifdef __cplusplus
}
#endif
