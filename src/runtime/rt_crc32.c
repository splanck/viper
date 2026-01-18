//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_crc32.c
/// @brief CRC32 checksum implementation (IEEE 802.3 polynomial).
///
/// Provides a shared CRC32 implementation used by rt_hash, rt_compress,
/// and rt_archive modules. Uses the IEEE polynomial (0xEDB88320) which is
/// standard for Ethernet, ZIP, PNG, GZIP, and many other formats.
///
/// Thread Safety: The lookup table is initialized once on first use.
/// Multiple threads may safely call rt_crc32_compute concurrently.
///
//===----------------------------------------------------------------------===//

#include "rt_crc32.h"

/// @brief CRC32 lookup table (256 entries for byte-at-a-time processing).
static uint32_t crc32_table[256];

/// @brief Flag indicating whether the table has been initialized.
static int crc32_table_initialized = 0;

void rt_crc32_init(void)
{
    if (crc32_table_initialized)
        return;

    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = 0xEDB88320 ^ (crc >> 1);
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = 1;
}

uint32_t rt_crc32_compute(const uint8_t *data, size_t len)
{
    rt_crc32_init();

    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}
