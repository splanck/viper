//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_zstd.h
// Purpose: From-scratch Zstandard (RFC 8878) decompressor — decode-only, no
//   dictionary support. Unblocks KTX2 supercompression scheme 2 (the default
//   output of the standard glTF texture toolchain) without any external
//   dependency.
// Key invariants:
//   - Single-shot API modeled on rt_compress_inflate_raw: caller provides the
//     whole frame, receives a malloc-owned buffer, and frees it with free().
//   - max_output bounds the decoded size against corrupt/hostile headers.
//   - Frames requiring a dictionary are rejected (KTX2 never uses them).
//   - Content checksums (xxhash64 low 32 bits) are verified when present.
// Ownership/Lifetime:
//   - *out_data is malloc-allocated on success; the caller owns and frees it.
// Links: src/runtime/io/rt_compress.h (DEFLATE counterpart),
//   misc/plans/3d_overhaul/03-texture-pipeline.md
//
//===----------------------------------------------------------------------===//
#ifndef RT_ZSTD_H
#define RT_ZSTD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Decompress one complete Zstandard frame.
/// @param data Frame bytes (must start with the Zstandard magic number).
/// @param len Byte length of @p data.
/// @param max_output Upper bound for the decompressed size (guards allocation).
/// @param out_data Receives a malloc-owned buffer with the decompressed bytes.
/// @param out_len Receives the decompressed byte count.
/// @return 1 on success; 0 on malformed input, unsupported features
///         (dictionaries), checksum mismatch, or output larger than
///         @p max_output. On failure no buffer is returned.
int rt_zstd_decompress_raw(
    const uint8_t *data, size_t len, size_t max_output, uint8_t **out_data, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* RT_ZSTD_H */
