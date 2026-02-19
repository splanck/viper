//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_crc32.h
// Purpose: CRC32 checksum computation (IEEE 802.3 polynomial).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Initialize the CRC32 lookup table.
    /// @details Thread-safe initialization using a simple flag.
    ///          Called automatically by rt_crc32_compute if needed.
    void rt_crc32_init(void);

    /// @brief Compute CRC32 checksum of data.
    /// @details Uses the IEEE 802.3 polynomial (0xEDB88320, bit-reversed).
    ///          This is the same polynomial used by Ethernet, ZIP, PNG, etc.
    /// @param data Pointer to data buffer.
    /// @param len Length of data in bytes.
    /// @return 32-bit CRC checksum.
    uint32_t rt_crc32_compute(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
