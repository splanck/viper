//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_compress.h
// Purpose: DEFLATE/GZIP compression and decompression (RFC 1951, RFC 1952) with no external
// dependencies, providing Bytes-in/Bytes-out API.
//
// Key invariants:
//   - No external dependencies; implements DEFLATE internally.
//   - Compression level is an integer in [0, 9]; 0 = no compression, 9 = maximum.
//   - Decompression returns NULL on invalid or corrupt input.
//   - GZIP adds a standard header/trailer; raw DEFLATE does not.
//
// Ownership/Lifetime:
//   - Returned Bytes objects are GC-managed; callers should not free them directly.
//   - Input Bytes are not consumed or modified.
//
// Links: src/runtime/io/rt_compress.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // DEFLATE Compression/Decompression (RFC 1951)
    //=========================================================================

    /// @brief Compress data using DEFLATE algorithm with default level.
    /// @param data Bytes object containing data to compress.
    /// @return New Bytes object with compressed data.
    /// @note Default compression level is 6.
    /// @note Traps if data is NULL.
    void *rt_compress_deflate(void *data);

    /// @brief Compress data using DEFLATE algorithm with specified level.
    /// @param data Bytes object containing data to compress.
    /// @param level Compression level 1-9 (1=fast, 9=best compression).
    /// @return New Bytes object with compressed data.
    /// @note Traps if data is NULL or level is out of range.
    void *rt_compress_deflate_lvl(void *data, int64_t level);

    /// @brief Decompress DEFLATE-compressed data.
    /// @param data Bytes object containing compressed data.
    /// @return New Bytes object with decompressed data.
    /// @note Traps if data is NULL, corrupted, or truncated.
    void *rt_compress_inflate(void *data);

    //=========================================================================
    // GZIP Compression/Decompression (RFC 1952)
    //=========================================================================

    /// @brief Compress data using GZIP format with default level.
    /// @param data Bytes object containing data to compress.
    /// @return New Bytes object with gzip-compressed data.
    /// @note Default compression level is 6.
    /// @note Traps if data is NULL.
    void *rt_compress_gzip(void *data);

    /// @brief Compress data using GZIP format with specified level.
    /// @param data Bytes object containing data to compress.
    /// @param level Compression level 1-9 (1=fast, 9=best compression).
    /// @return New Bytes object with gzip-compressed data.
    /// @note Traps if data is NULL or level is out of range.
    void *rt_compress_gzip_lvl(void *data, int64_t level);

    /// @brief Decompress GZIP-compressed data.
    /// @param data Bytes object containing gzip data.
    /// @return New Bytes object with decompressed data.
    /// @note Traps if data is NULL, corrupted, truncated, or CRC mismatch.
    void *rt_compress_gunzip(void *data);

    //=========================================================================
    // String Convenience Methods
    //=========================================================================

    /// @brief Compress a string using DEFLATE.
    /// @param text String to compress (as UTF-8 bytes).
    /// @return New Bytes object with compressed data.
    /// @note Traps if text is NULL.
    void *rt_compress_deflate_str(rt_string text);

    /// @brief Decompress DEFLATE data to a string.
    /// @param data Bytes object containing compressed data.
    /// @return String from decompressed UTF-8 bytes.
    /// @note Traps if data is NULL or corrupted.
    rt_string rt_compress_inflate_str(void *data);

    /// @brief Compress a string using GZIP.
    /// @param text String to compress (as UTF-8 bytes).
    /// @return New Bytes object with gzip data.
    /// @note Traps if text is NULL.
    void *rt_compress_gzip_str(rt_string text);

    /// @brief Decompress GZIP data to a string.
    /// @param data Bytes object containing gzip data.
    /// @return String from decompressed UTF-8 bytes.
    /// @note Traps if data is NULL, corrupted, or CRC mismatch.
    rt_string rt_compress_gunzip_str(void *data);

#ifdef __cplusplus
}
#endif
