//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_guid.h
// Purpose: UUID version 4 (random) generation and manipulation per RFC 4122.
// Key invariants: GUIDs are formatted as lowercase hex with dashes;
//                 version 4 and variant bits are properly set; uses
//                 cryptographically secure random source where available.
// Ownership/Lifetime: Returned strings are newly allocated.
// Links: docs/viperlib.md
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
