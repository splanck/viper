//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_audio_codec.h
// Purpose: IMA ADPCM codec for the Viper Audio Format (.vaf). Provides 4:1
//   compression of 16-bit PCM audio with negligible quality loss for games.
//
// Key invariants:
//   - Step table has 89 entries; index table has 16 entries.
//   - Encode/decode are exact inverses: decode(encode(x)) ≈ x within ±2.
//   - .vaf header starts with magic "VAF1" (0x56414631).
//   - Block-based: each ADPCM block has a 4-byte preamble (predictor + index).
//
// Ownership/Lifetime:
//   - Decoded PCM buffers are malloc'd; caller must free.
//   - Streams must be closed with rt_vaf_stream_close.
//
// Links: src/runtime/audio/rt_audio_codec.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// IMA ADPCM 89-entry step size table
    extern const int16_t rt_adpcm_step_table[89];

    /// IMA ADPCM 16-entry index adjustment table
    extern const int8_t rt_adpcm_index_table[16];

    /// Encode a raw PCM buffer to an ADPCM block.
    /// Returns bytes written to output.
    int64_t rt_adpcm_encode_block(const int16_t *pcm,
                                  int64_t sample_count,
                                  uint8_t *output,
                                  int64_t output_capacity);

    /// Decode an ADPCM block to PCM.
    /// Returns samples decoded.
    int64_t rt_adpcm_decode_block(const uint8_t *adpcm,
                                  int64_t block_bytes,
                                  int16_t *output,
                                  int64_t output_capacity);

    /// Encode a WAV file to .vaf format.
    /// Returns 1 on success, 0 on failure.
    int8_t rt_audio_encode_vaf(rt_string input_wav_path, rt_string output_vaf_path);

    /// Decode a .vaf file to raw PCM.
    /// Returns malloc'd PCM buffer; caller must free. Sets out params.
    int16_t *rt_audio_decode_vaf(const char *path,
                                 int32_t *out_channels,
                                 int32_t *out_sample_rate,
                                 int64_t *out_sample_count);

    /// Check if a file is .vaf format by reading header magic.
    int8_t rt_audio_is_vaf(const char *path);

    /// Exposed to Zia as Viper.Sound.Encode
    int8_t rt_audio_encode(rt_string input_path, rt_string output_path);

#ifdef __cplusplus
}
#endif
