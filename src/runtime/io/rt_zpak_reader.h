//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_zpak_reader.h
// Purpose: Reader for ZPAK (Zanna Pack Archive) binary format. Parses archives
//          from either memory buffers (embedded in .rodata) or file handles
//          (mounted .zpak pack files).
//
// Key invariants:
//   - zpak_open_memory() does not copy the blob; caller must keep it alive.
//   - zpak_open_file() opens and retains a FILE* handle until zpak_close().
//   - zpak_read_entry() returns a malloc'd buffer that the caller must free.
//   - TOC entries are sorted by name for binary search in zpak_find().
//
// Ownership/Lifetime:
//   - zpak_archive_t owns its TOC array and (if file-based) the FILE* handle.
//   - Returned entry data buffers are caller-owned (must be freed).
//
// Links: ZpakWriter.hpp (build-time writer), rt_asset.h (asset manager)
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief A single entry in a ZPAK table of contents.
typedef struct {
    char *name;           ///< Asset name (heap-allocated, owned by archive).
    uint64_t data_offset; ///< Byte offset of data in blob/file.
    uint64_t data_size;   ///< Original uncompressed size.
    uint64_t stored_size; ///< Size in archive (may differ if compressed).
    int compressed;       ///< 1 if entry is DEFLATE-compressed.
} zpak_entry_t;

/// @brief A parsed ZPAK archive (memory-backed or file-backed).
typedef struct {
    zpak_entry_t *entries; ///< Array of TOC entries (sorted by name).
    uint32_t count;       ///< Number of entries.

    const uint8_t *blob; ///< Non-NULL for memory-backed (embedded) archives.
    size_t blob_size;    ///< Size of memory blob.
    FILE *file;          ///< Non-NULL for file-backed (mounted) archives.
    uint64_t file_size;  ///< Size of file-backed archive, 0 for memory-backed.
} zpak_archive_t;

/// @brief Open a ZPAK archive from a memory buffer.
///
/// The buffer must remain valid for the lifetime of the archive.
/// Returns NULL if the buffer is invalid or too small.
///
/// @param data  Pointer to ZPAK data (not copied).
/// @param size  Size of buffer in bytes.
/// @return Parsed archive, or NULL on error.
zpak_archive_t *zpak_open_memory(const uint8_t *data, size_t size);

/// @brief Open a ZPAK archive from a file on disk.
///
/// The file handle is kept open until zpak_close().
/// Returns NULL if the file cannot be opened or is invalid.
///
/// @param path  Path to .zpak file.
/// @return Parsed archive, or NULL on error.
zpak_archive_t *zpak_open_file(const char *path);
zpak_archive_t *zpak_open_file_no_follow(const char *path);

/// @brief Find an entry by name.
///
/// Uses binary search on the sorted TOC.
///
/// @param archive  Parsed ZPAK archive.
/// @param name     Asset name to search for.
/// @return Pointer to entry (owned by archive), or NULL if not found.
const zpak_entry_t *zpak_find(const zpak_archive_t *archive, const char *name);

/// @brief Read entry data from the archive.
///
/// If the entry is compressed, the data is decompressed via DEFLATE.
/// Returns a heap-allocated buffer that the caller must free().
///
/// @param archive   Parsed ZPAK archive.
/// @param entry     Entry to read (from zpak_find).
/// @param out_size  Set to the uncompressed data size on success.
/// @return Heap-allocated data buffer, or NULL on error.
uint8_t *zpak_read_entry(const zpak_archive_t *archive, const zpak_entry_t *entry, size_t *out_size);

/// @brief Close and free a ZPAK archive.
///
/// Frees the TOC array and entry names. Closes the file handle if file-backed.
///
/// @param archive  Archive to close (may be NULL).
void zpak_close(zpak_archive_t *archive);

#ifdef __cplusplus
}
#endif
