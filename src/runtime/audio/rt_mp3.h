//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_mp3.h
// Purpose: MPEG-1/2/2.5 Layer III (MP3) audio decoder.
// Key invariants:
//   - Supports baseline MP3: MPEG-1 Layer III, mono and stereo
//   - Handles ID3v2 tags (skipped), common bitrates, all sample rates
//   - Output is interleaved 16-bit signed PCM
//   - From-scratch implementation — no external libraries
// Ownership/Lifetime:
//   - Caller owns mp3_decoder_t and must call mp3_decoder_free
//   - Output PCM buffer is owned by caller (malloc'd, caller frees)
// Links: rt_mp3_tables.h (Huffman tables, spec constants)
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Opaque MP3 decoder handle.
typedef struct mp3_decoder mp3_decoder_t;

/// @brief Create a new MP3 decoder.
mp3_decoder_t *mp3_decoder_new(void);

/// @brief Free an MP3 decoder and all its resources.
void mp3_decoder_free(mp3_decoder_t *dec);

/// @brief Decode an entire MP3 file from memory to PCM.
/// @param dec Decoder instance.
/// @param data File data (may include ID3v2 header).
/// @param len Length of file data.
/// @param out_pcm Receives malloc'd interleaved 16-bit PCM. Caller must free().
/// @param out_samples Receives total number of samples per channel.
/// @param out_channels Receives channel count (1 or 2).
/// @param out_sample_rate Receives sample rate (e.g., 44100).
/// @return 0 on success, -1 on failure.
int mp3_decode_file(mp3_decoder_t *dec, const uint8_t *data, size_t len,
                    int16_t **out_pcm, int *out_samples,
                    int *out_channels, int *out_sample_rate);

#ifdef __cplusplus
}
#endif
