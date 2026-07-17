//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_audio_internal.h
// Purpose: Shared decode limits + the audio-format decode API (WAV/OGG/MP3 ->
//   PCM) used by the audio engine (rt_audio.c) and decoders (rt_audio_decode.c).
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stddef.h>
#include <stdint.h>

#define RT_AUDIO_MAX_DECODED_SOUND_BYTES ((size_t)100 * 1024 * 1024)
#define RT_AUDIO_MAX_SAMPLE_RATE 384000

/// @brief Sniff a file's audio container (WAV/OGG/MP3 constants; -1 unknown/unreadable).
int detect_audio_format(const char *filepath);

/// @brief Sniff an in-memory buffer's audio container (same constants as the file form).
int detect_audio_format_mem(const void *data, size_t size);

/// @brief Decode MP3 bytes to a malloc'd WAV image in *out_data/*out_len (caller frees).
/// @return 0 on success, non-zero on decode failure (outputs untouched).
int mp3_data_to_wav(const uint8_t *data, size_t size, uint8_t **out_data, size_t *out_len);

/// @brief Decode an MP3 file to a malloc'd WAV image (caller frees). @return 0 on success.
int mp3_file_to_wav(const char *filepath, uint8_t **out_data, size_t *out_len);

/// @brief Decode an OGG Vorbis file to a malloc'd WAV image (caller frees). @return 0 on success.
int ogg_file_to_wav(const char *filepath, uint8_t **out_data, size_t *out_len);

/// @brief Decode OGG Vorbis bytes to a malloc'd WAV image (caller frees). @return 0 on success.
int ogg_mem_to_wav(const void *data, size_t size, uint8_t **out_data, size_t *out_len);
