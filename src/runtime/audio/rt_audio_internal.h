//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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

int detect_audio_format(const char *filepath);
int detect_audio_format_mem(const void *data, size_t size);
int mp3_data_to_wav(const uint8_t *data, size_t size, uint8_t **out_data, size_t *out_len);
int mp3_file_to_wav(const char *filepath, uint8_t **out_data, size_t *out_len);
int ogg_file_to_wav(const char *filepath, uint8_t **out_data, size_t *out_len);
int ogg_mem_to_wav(const void *data, size_t size, uint8_t **out_data, size_t *out_len);
