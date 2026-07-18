//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_zpak_format.h
// Purpose: Define the versioned, byte-level ZPAK asset-container constants
//          shared by the build-time writer and runtime reader.
//
// Key invariants:
//   - Every integer in a ZPAK file is encoded little-endian.
//   - Version 1 entries have 28 fixed bytes after their names.
//   - Version 2 appends one four-byte CRC-32 to each version 1 entry record.
//   - Unknown header and entry flag bits are rejected by current readers.
//
// Ownership/Lifetime:
//   - This header defines constants only and owns no process or heap state.
//
// Links: src/runtime/io/rt_zpak_reader.c,
//        src/tools/common/asset/ZpakWriter.cpp,
//        docs/adr/0134-zpak-v2-validation-and-entry-checksums.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

/// @brief Number of bytes in every supported ZPAK header.
enum { RT_ZPAK_HEADER_SIZE = 32 };

/// @brief Legacy ZPAK version without per-entry integrity checksums.
enum { RT_ZPAK_VERSION_1 = 1 };

/// @brief Checksummed ZPAK version emitted by current writers.
enum { RT_ZPAK_VERSION_2 = 2 };

/// @brief Format version that a new ZPAK writer must emit.
enum { RT_ZPAK_VERSION_CURRENT = RT_ZPAK_VERSION_2 };

enum {
    /// Header flag indicating that at least one entry uses DEFLATE.
    RT_ZPAK_HEADER_FLAG_COMPRESSED = UINT16_C(1) << 0,
    /// Version 2 header flag declaring one CRC-32 on every TOC entry.
    RT_ZPAK_HEADER_FLAG_ENTRY_CRC32 = UINT16_C(1) << 1,
    /// Complete set of header flags understood for version 1.
    RT_ZPAK_V1_HEADER_FLAGS = RT_ZPAK_HEADER_FLAG_COMPRESSED,
    /// Complete set of header flags understood for version 2.
    RT_ZPAK_V2_HEADER_FLAGS = RT_ZPAK_HEADER_FLAG_COMPRESSED | RT_ZPAK_HEADER_FLAG_ENTRY_CRC32,
};

/// @brief Entry flag indicating that stored bytes contain a DEFLATE stream.
enum { RT_ZPAK_ENTRY_FLAG_COMPRESSED = UINT16_C(1) << 0 };

/// @brief Complete set of per-entry flags understood by current readers.
enum { RT_ZPAK_ENTRY_FLAGS_KNOWN = RT_ZPAK_ENTRY_FLAG_COMPRESSED };

/// @brief Fixed version 1 TOC bytes following an entry's variable-length name.
enum { RT_ZPAK_V1_ENTRY_FIXED_SIZE = 28 };

/// @brief Fixed version 2 TOC bytes following an entry's variable-length name.
enum { RT_ZPAK_V2_ENTRY_FIXED_SIZE = 32 };

/// @brief Smallest legal version 1 entry record, including length and one name byte.
enum { RT_ZPAK_V1_ENTRY_MIN_SIZE = 2 + 1 + RT_ZPAK_V1_ENTRY_FIXED_SIZE };

/// @brief Smallest legal version 2 entry record, including length and one name byte.
enum { RT_ZPAK_V2_ENTRY_MIN_SIZE = 2 + 1 + RT_ZPAK_V2_ENTRY_FIXED_SIZE };

/// @brief Maximum accepted encoded table-of-contents size in bytes.
#define RT_ZPAK_MAX_TOC_SIZE UINT64_C(67108864)
