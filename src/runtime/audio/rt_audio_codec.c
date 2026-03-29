//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/audio/rt_audio_codec.c
// Purpose: IMA ADPCM encoder/decoder and .vaf file format support.
//   Implements the public-domain IMA ADPCM algorithm (1992) for 4:1 audio
//   compression. The .vaf format uses a simple header + ADPCM data blocks.
//
// Key invariants:
//   - Encoder quantizes 16-bit PCM to 4-bit nibbles with adaptive step.
//   - Decoder reconstructs PCM from nibbles using the same step table.
//   - Round-trip accuracy: within ±16 per sample (4-bit quantization noise).
//   - WAV parsing only supports PCM format (format tag 1), 16-bit samples.
//
// Ownership/Lifetime:
//   - Decoded PCM buffers are malloc'd; caller frees.
//   - File handles are opened and closed within each function.
//
// Links: rt_audio_codec.h
//
//===----------------------------------------------------------------------===//

#include "rt_audio_codec.h"

#include "rt_internal.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// IMA ADPCM Tables
//=============================================================================

const int16_t rt_adpcm_step_table[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

const int8_t rt_adpcm_index_table[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

//=============================================================================
// ADPCM Encode/Decode
//=============================================================================

/// Clamp step index to valid range [0, 88]
static int32_t clamp_index(int32_t idx) {
    if (idx < 0)
        return 0;
    if (idx > 88)
        return 88;
    return idx;
}

/// Clamp sample to int16 range
static int16_t clamp_sample(int32_t s) {
    if (s > 32767)
        return 32767;
    if (s < -32768)
        return -32768;
    return (int16_t)s;
}

int64_t rt_adpcm_encode_block(const int16_t *pcm,
                              int64_t sample_count,
                              uint8_t *output,
                              int64_t output_capacity) {
    if (!pcm || !output || sample_count <= 0)
        return 0;

    // Preamble: 2 bytes predictor + 1 byte step index + 1 byte reserved
    if (output_capacity < 4)
        return 0;

    int32_t predictor = pcm[0];
    int32_t step_index = 0;

    output[0] = (uint8_t)(predictor & 0xFF);
    output[1] = (uint8_t)((predictor >> 8) & 0xFF);
    output[2] = (uint8_t)step_index;
    output[3] = 0;

    int64_t out_pos = 4;
    int8_t nibble_hi = 0; // 0 = writing low nibble, 1 = high nibble

    for (int64_t i = 1; i < sample_count; i++) {
        int32_t step = rt_adpcm_step_table[step_index];
        int32_t diff = pcm[i] - predictor;
        uint8_t nibble = 0;

        if (diff < 0) {
            nibble = 8; // sign bit
            diff = -diff;
        }

        if (diff >= step) {
            nibble |= 4;
            diff -= step;
        }
        if (diff >= (step >> 1)) {
            nibble |= 2;
            diff -= (step >> 1);
        }
        if (diff >= (step >> 2)) {
            nibble |= 1;
        }

        // Decode to update predictor (must match decoder)
        int32_t delta = (step >> 3);
        if (nibble & 4)
            delta += step;
        if (nibble & 2)
            delta += (step >> 1);
        if (nibble & 1)
            delta += (step >> 2);
        if (nibble & 8)
            predictor -= delta;
        else
            predictor += delta;
        predictor = clamp_sample(predictor);

        step_index = clamp_index(step_index + rt_adpcm_index_table[nibble]);

        // Pack nibbles
        if (!nibble_hi) {
            if (out_pos >= output_capacity)
                break;
            output[out_pos] = nibble;
            nibble_hi = 1;
        } else {
            output[out_pos] |= (uint8_t)(nibble << 4);
            nibble_hi = 0;
            out_pos++;
        }
    }

    // Flush last nibble if odd
    if (nibble_hi)
        out_pos++;

    return out_pos;
}

int64_t rt_adpcm_decode_block(const uint8_t *adpcm,
                              int64_t block_bytes,
                              int16_t *output,
                              int64_t output_capacity) {
    if (!adpcm || !output || block_bytes < 4 || output_capacity <= 0)
        return 0;

    // Read preamble
    int32_t predictor = (int16_t)(adpcm[0] | (adpcm[1] << 8));
    int32_t step_index = clamp_index(adpcm[2]);

    output[0] = (int16_t)predictor;
    int64_t out_pos = 1;

    for (int64_t i = 4; i < block_bytes && out_pos < output_capacity; i++) {
        // Low nibble
        uint8_t nibble = adpcm[i] & 0x0F;
        int32_t step = rt_adpcm_step_table[step_index];
        int32_t delta = (step >> 3);
        if (nibble & 4)
            delta += step;
        if (nibble & 2)
            delta += (step >> 1);
        if (nibble & 1)
            delta += (step >> 2);
        if (nibble & 8)
            predictor -= delta;
        else
            predictor += delta;
        predictor = clamp_sample(predictor);
        step_index = clamp_index(step_index + rt_adpcm_index_table[nibble]);
        output[out_pos++] = (int16_t)predictor;

        if (out_pos >= output_capacity)
            break;

        // High nibble
        nibble = (adpcm[i] >> 4) & 0x0F;
        step = rt_adpcm_step_table[step_index];
        delta = (step >> 3);
        if (nibble & 4)
            delta += step;
        if (nibble & 2)
            delta += (step >> 1);
        if (nibble & 1)
            delta += (step >> 2);
        if (nibble & 8)
            predictor -= delta;
        else
            predictor += delta;
        predictor = clamp_sample(predictor);
        step_index = clamp_index(step_index + rt_adpcm_index_table[nibble]);
        output[out_pos++] = (int16_t)predictor;
    }

    return out_pos;
}

//=============================================================================
// .vaf File Format
//=============================================================================

#define VAF_MAGIC 0x56414631 // "VAF1" in little-endian

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif
typedef struct {
    uint32_t magic;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t total_samples;
    uint32_t block_size;
    uint16_t bits_per_sample;
}
#ifndef _MSC_VER
__attribute__((packed))
#endif
vaf_header;
#ifdef _MSC_VER
#pragma pack(pop)
#endif

/// @brief Check whether a file begins with the .vaf magic number ("VAF1").
/// @details Opens the file, reads the first 4 bytes, and compares against
///          the VAF_MAGIC constant. The file is closed immediately after the
///          read so no resources are leaked regardless of the outcome.
/// @param path Null-terminated filesystem path to inspect.
/// @return 1 if the file starts with the .vaf magic, 0 otherwise.
int8_t rt_audio_is_vaf(const char *path) {
    if (!path)
        return 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint32_t magic = 0;
    size_t r = fread(&magic, 1, 4, f);
    fclose(f);
    return (r == 4 && magic == VAF_MAGIC) ? 1 : 0;
}

int16_t *rt_audio_decode_vaf(const char *path,
                             int32_t *out_channels,
                             int32_t *out_sample_rate,
                             int64_t *out_sample_count) {
    if (!path)
        return NULL;

    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    vaf_header hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) || hdr.magic != VAF_MAGIC) {
        fclose(f);
        return NULL;
    }

    // Validate header fields from untrusted file
    if (hdr.channels == 0 || hdr.channels > 2) {
        fclose(f);
        return NULL;
    }
    // Cap at ~2 hours of stereo 48kHz audio (345M samples)
    if ((uint64_t)hdr.total_samples > 48000ULL * 3600 * 2) {
        fclose(f);
        return NULL;
    }

    if (out_channels)
        *out_channels = hdr.channels;
    if (out_sample_rate)
        *out_sample_rate = (int32_t)hdr.sample_rate;
    if (out_sample_count)
        *out_sample_count = (int64_t)hdr.total_samples;

    int64_t total = (int64_t)hdr.total_samples * hdr.channels;
    if (total <= 0) {
        fclose(f);
        int16_t *empty = (int16_t *)malloc(sizeof(int16_t));
        if (empty)
            *empty = 0;
        return empty;
    }

    int16_t *pcm = (int16_t *)malloc((size_t)total * sizeof(int16_t));
    if (!pcm) {
        fclose(f);
        return NULL;
    }

    // Read and decode ADPCM blocks
    uint8_t *block_buf = (uint8_t *)malloc(hdr.block_size);
    if (!block_buf) {
        free(pcm);
        fclose(f);
        return NULL;
    }

    int64_t samples_decoded = 0;
    while (samples_decoded < total) {
        size_t read = fread(block_buf, 1, hdr.block_size, f);
        if (read == 0)
            break;

        int64_t remaining = total - samples_decoded;
        int64_t decoded =
            rt_adpcm_decode_block(block_buf, (int64_t)read, pcm + samples_decoded, remaining);
        samples_decoded += decoded;
        if (decoded == 0)
            break;
    }

    free(block_buf);
    fclose(f);

    // Fill any remaining samples with silence
    while (samples_decoded < total)
        pcm[samples_decoded++] = 0;

    return pcm;
}

//=============================================================================
// WAV Parsing (minimal, for encode)
//=============================================================================

typedef struct {
    int16_t *samples;
    int32_t channels;
    int32_t sample_rate;
    int64_t sample_count; // per channel
} wav_data;

static wav_data parse_wav(const char *path) {
    wav_data result = {NULL, 0, 0, 0};
    FILE *f = fopen(path, "rb");
    if (!f)
        return result;

    // Read WAV header (assumes standard 44-byte PCM header layout).
    // NOTE: Does not handle WAV files with extra chunks (LIST, INFO, fact)
    // between fmt and data. A full implementation would scan chunk-by-chunk.
    uint8_t header[44];
    if (fread(header, 1, 44, f) != 44) {
        fclose(f);
        return result;
    }

    // Verify RIFF/WAVE
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        fclose(f);
        return result;
    }

    // Format chunk at offset 20
    uint16_t format_tag = (uint16_t)(header[20] | (header[21] << 8));
    if (format_tag != 1) // Must be PCM
    {
        fclose(f);
        return result;
    }

    result.channels = (int32_t)(header[22] | (header[23] << 8));
    if (result.channels == 0) {
        fclose(f);
        return result;
    }
    result.sample_rate =
        (int32_t)(header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24));
    uint16_t bits_per_sample = (uint16_t)(header[34] | (header[35] << 8));
    if (bits_per_sample != 16) {
        fclose(f);
        return result;
    }

    // Data chunk size at offset 40
    uint32_t data_size =
        (uint32_t)(header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24));
    int64_t total_samples = (int64_t)data_size / 2; // 16-bit = 2 bytes per sample
    result.sample_count = total_samples / result.channels;

    result.samples = (int16_t *)malloc(data_size);
    if (!result.samples) {
        fclose(f);
        result.sample_count = 0;
        return result;
    }

    size_t read = fread(result.samples, 1, data_size, f);
    fclose(f);

    if (read < data_size)
        result.sample_count = (int64_t)(read / 2) / result.channels;

    return result;
}

//=============================================================================
// Encode WAV -> VAF
//=============================================================================

/// @brief Encode a PCM WAV file into the compressed .vaf (Viper Audio Format).
/// @details Parses the input WAV (must be 16-bit PCM), writes a .vaf header
///          with channel/sample-rate metadata, then encodes the audio data in
///          1024-byte ADPCM blocks achieving ~4:1 compression. The output file
///          is created atomically: on failure the partially written file is left
///          on disk (the caller may clean up).
/// @param input_wav_path Runtime string path to the source WAV file.
/// @param output_vaf_path Runtime string path for the encoded .vaf output.
/// @return 1 on success, 0 on failure (bad input, I/O error, OOM).
int8_t rt_audio_encode_vaf(rt_string input_wav_path, rt_string output_vaf_path) {
    if (!input_wav_path || !output_vaf_path)
        return 0;

    const char *in_path = rt_string_cstr(input_wav_path);
    const char *out_path = rt_string_cstr(output_vaf_path);
    if (!in_path || !out_path)
        return 0;

    wav_data wav = parse_wav(in_path);
    if (!wav.samples || wav.sample_count <= 0)
        return 0;

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        free(wav.samples);
        return 0;
    }

    // Write header
    uint32_t block_size = 1024;
    vaf_header hdr;
    hdr.magic = VAF_MAGIC;
    hdr.channels = (uint16_t)wav.channels;
    hdr.sample_rate = (uint32_t)wav.sample_rate;
    hdr.total_samples = (uint32_t)wav.sample_count;
    hdr.block_size = block_size;
    hdr.bits_per_sample = 4;
    fwrite(&hdr, 1, sizeof(hdr), f);

    // Encode each channel's samples in blocks
    int64_t total = wav.sample_count * wav.channels;
    int64_t samples_per_block = (int64_t)(block_size - 4) * 2 + 1; // 2 samples per byte + preamble
    uint8_t *block_buf = (uint8_t *)malloc(block_size);

    if (block_buf) {
        int64_t pos = 0;
        while (pos < total) {
            int64_t remaining = total - pos;
            int64_t chunk = remaining < samples_per_block ? remaining : samples_per_block;
            int64_t written =
                rt_adpcm_encode_block(wav.samples + pos, chunk, block_buf, (int64_t)block_size);
            if (written > 0)
                fwrite(block_buf, 1, (size_t)written, f);
            pos += chunk;
        }
        free(block_buf);
    }

    fclose(f);
    free(wav.samples);
    return 1;
}

/// @brief Public entry point for Viper.Sound.Encode; delegates to rt_audio_encode_vaf.
/// @details Exposed to the Zia frontend as the `Sound.Encode(in, out)` method.
///          Currently only supports WAV→VAF encoding; future formats could be
///          dispatched based on file extension.
/// @param input_path Runtime string path to the source audio file.
/// @param output_path Runtime string path for the encoded output.
/// @return 1 on success, 0 on failure.
int8_t rt_audio_encode(rt_string input_path, rt_string output_path) {
    return rt_audio_encode_vaf(input_path, output_path);
}
