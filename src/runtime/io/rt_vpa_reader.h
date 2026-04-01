//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_vpa_reader.h
// Purpose: Reader for VPA (Viper Pack Archive) binary format. Parses archives
//          from either memory buffers (embedded in .rodata) or file handles
//          (mounted .vpa pack files).
//
// Key invariants:
//   - vpa_open_memory() does not copy the blob; caller must keep it alive.
//   - vpa_open_file() opens and retains a FILE* handle until vpa_close().
//   - vpa_read_entry() returns a malloc'd buffer that the caller must free.
//   - TOC entries are sorted by name for binary search in vpa_find().
//
// Ownership/Lifetime:
//   - vpa_archive_t owns its TOC array and (if file-based) the FILE* handle.
//   - Returned entry data buffers are caller-owned (must be freed).
//
// Links: VpaWriter.hpp (build-time writer), rt_asset.h (asset manager)
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief A single entry in a VPA table of contents.
typedef struct {
    char *name;            ///< Asset name (heap-allocated, owned by archive).
    uint64_t data_offset;  ///< Byte offset of data in blob/file.
    uint64_t data_size;    ///< Original uncompressed size.
    uint64_t stored_size;  ///< Size in archive (may differ if compressed).
    int compressed;        ///< 1 if entry is DEFLATE-compressed.
} vpa_entry_t;

/// @brief A parsed VPA archive (memory-backed or file-backed).
typedef struct {
    vpa_entry_t *entries;  ///< Array of TOC entries (sorted by name).
    uint32_t count;        ///< Number of entries.

    const uint8_t *blob;   ///< Non-NULL for memory-backed (embedded) archives.
    size_t blob_size;      ///< Size of memory blob.
    FILE *file;            ///< Non-NULL for file-backed (mounted) archives.
} vpa_archive_t;

/// @brief Open a VPA archive from a memory buffer.
///
/// The buffer must remain valid for the lifetime of the archive.
/// Returns NULL if the buffer is invalid or too small.
///
/// @param data  Pointer to VPA data (not copied).
/// @param size  Size of buffer in bytes.
/// @return Parsed archive, or NULL on error.
vpa_archive_t *vpa_open_memory(const uint8_t *data, size_t size);

/// @brief Open a VPA archive from a file on disk.
///
/// The file handle is kept open until vpa_close().
/// Returns NULL if the file cannot be opened or is invalid.
///
/// @param path  Path to .vpa file.
/// @return Parsed archive, or NULL on error.
vpa_archive_t *vpa_open_file(const char *path);

/// @brief Find an entry by name.
///
/// Uses binary search on the sorted TOC.
///
/// @param archive  Parsed VPA archive.
/// @param name     Asset name to search for.
/// @return Pointer to entry (owned by archive), or NULL if not found.
const vpa_entry_t *vpa_find(const vpa_archive_t *archive, const char *name);

/// @brief Read entry data from the archive.
///
/// If the entry is compressed, the data is decompressed via DEFLATE.
/// Returns a heap-allocated buffer that the caller must free().
///
/// @param archive   Parsed VPA archive.
/// @param entry     Entry to read (from vpa_find).
/// @param out_size  Set to the uncompressed data size on success.
/// @return Heap-allocated data buffer, or NULL on error.
uint8_t *vpa_read_entry(const vpa_archive_t *archive, const vpa_entry_t *entry,
                        size_t *out_size);

/// @brief Close and free a VPA archive.
///
/// Frees the TOC array and entry names. Closes the file handle if file-backed.
///
/// @param archive  Archive to close (may be NULL).
void vpa_close(vpa_archive_t *archive);

#ifdef __cplusplus
}
#endif
