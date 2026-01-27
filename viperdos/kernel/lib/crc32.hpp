//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file crc32.hpp
 * @brief CRC32 checksum computation for data integrity.
 *
 * @details
 * Provides CRC32 computation using the IEEE 802.3 polynomial (0xEDB88320).
 * Used by ViperFS for superblock and journal checksums to detect corruption.
 *
 * The implementation uses a 256-entry lookup table for performance,
 * requiring approximately 1KB of memory.
 */
#pragma once

#include "../include/types.hpp"

namespace lib {

/**
 * @brief Compute CRC32 checksum of data.
 *
 * @details
 * Computes the CRC32 using the standard IEEE 802.3 polynomial.
 * The result is inverted (XOR with 0xFFFFFFFF) as per the standard.
 *
 * @param data Pointer to data buffer.
 * @param len Length of data in bytes.
 * @return CRC32 checksum.
 */
u32 crc32(const void *data, usize len);

/**
 * @brief Update a running CRC32 with additional data.
 *
 * @details
 * Allows computing CRC32 incrementally over multiple buffers.
 * Start with crc=0xFFFFFFFF, then call crc32_update for each buffer,
 * and finally XOR the result with 0xFFFFFFFF.
 *
 * Example:
 * @code
 * u32 crc = 0xFFFFFFFF;
 * crc = crc32_update(crc, buf1, len1);
 * crc = crc32_update(crc, buf2, len2);
 * crc ^= 0xFFFFFFFF;  // Final XOR
 * @endcode
 *
 * @param crc Current CRC value (start with 0xFFFFFFFF).
 * @param data Pointer to data buffer.
 * @param len Length of data in bytes.
 * @return Updated CRC value (not yet finalized).
 */
u32 crc32_update(u32 crc, const void *data, usize len);

/**
 * @brief Compute CRC32 of a superblock, excluding the checksum field.
 *
 * @details
 * Helper for superblock validation. Computes CRC32 of the superblock
 * data while treating the checksum field as zero.
 *
 * @param sb_data Pointer to superblock data (4096 bytes).
 * @param checksum_offset Byte offset of checksum field in superblock.
 * @return CRC32 of superblock with checksum field zeroed.
 */
u32 crc32_superblock(const void *sb_data, usize checksum_offset);

} // namespace lib
