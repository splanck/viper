//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperAUD WAV File Parser
//
// Parses RIFF WAV files containing PCM audio data. Supports:
// - 8-bit unsigned PCM
// - 16-bit signed PCM
// - Mono and stereo
// - Any sample rate (will be resampled by caller if needed)
//
// WAV file format (simplified):
//   Offset  Size  Description
//   0       4     "RIFF" chunk ID
//   4       4     Chunk size (file size - 8)
//   8       4     "WAVE" format
//   12      4     "fmt " subchunk ID
//   16      4     Subchunk size (16 for PCM)
//   20      2     Audio format (1 = PCM)
//   22      2     Number of channels
//   24      4     Sample rate
//   28      4     Byte rate
//   32      2     Block align
//   34      2     Bits per sample
//   36      4     "data" subchunk ID
//   40      4     Data size
//   44      ...   PCM data
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief WAV file parser for ViperAUD.

#include "vaud_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// WAV File Constants
//===----------------------------------------------------------------------===//

#define WAV_RIFF_ID 0x46464952 /* "RIFF" in little-endian */
#define WAV_WAVE_ID 0x45564157 /* "WAVE" in little-endian */
#define WAV_FMT_ID 0x20746D66  /* "fmt " in little-endian */
#define WAV_DATA_ID 0x61746164 /* "data" in little-endian */
#define WAV_FORMAT_PCM 1

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

/// @brief Read a 16-bit little-endian value from a buffer.
static inline uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/// @brief Read a 32-bit little-endian value from a buffer.
static inline uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/// @brief Convert 8-bit unsigned sample to 16-bit signed.
static inline int16_t u8_to_s16(uint8_t sample)
{
    return (int16_t)((sample - 128) << 8);
}

/// @brief Convert one PCM frame from raw bytes to stereo 16-bit signed samples.
/// @details Handles both 8-bit unsigned and 16-bit signed PCM, and mono/stereo
///          sources. Mono sources are duplicated to both output channels.
/// @param src Pointer to raw PCM data for one frame.
/// @param bits_per_sample Bits per sample (8 or 16).
/// @param channels Number of channels in source (1 or 2).
/// @param left Output: left channel sample.
/// @param right Output: right channel sample.
static inline void decode_pcm_frame(
    const uint8_t *src, int32_t bits_per_sample, int32_t channels, int16_t *left, int16_t *right)
{
    if (bits_per_sample == 8)
    {
        *left = u8_to_s16(src[0]);
        *right = (channels == 2) ? u8_to_s16(src[1]) : *left;
    }
    else /* 16-bit */
    {
        *left = (int16_t)read_u16_le(src);
        *right = (channels == 2) ? (int16_t)read_u16_le(src + 2) : *left;
    }
}

//===----------------------------------------------------------------------===//
// WAV Header Parsing
//===----------------------------------------------------------------------===//

/// @brief WAV format information extracted from header.
typedef struct
{
    int32_t sample_rate;
    int32_t channels;
    int32_t bits_per_sample;
    int64_t data_offset; /* Byte offset to PCM data */
    int64_t data_size;   /* Size of PCM data in bytes */
} vaud_wav_info;

/// @brief Parse WAV header from memory buffer.
/// @param data Pointer to WAV file data.
/// @param size Size of data in bytes.
/// @param info Output: parsed format information.
/// @return 1 on success, 0 on failure.
static int parse_wav_header(const uint8_t *data, size_t size, vaud_wav_info *info)
{
    if (size < 44)
    {
        vaud_set_error(VAUD_ERR_FORMAT, "WAV file too small");
        return 0;
    }

    /* Check RIFF header */
    if (read_u32_le(data) != WAV_RIFF_ID)
    {
        vaud_set_error(VAUD_ERR_FORMAT, "Not a RIFF file");
        return 0;
    }

    /* Check WAVE format */
    if (read_u32_le(data + 8) != WAV_WAVE_ID)
    {
        vaud_set_error(VAUD_ERR_FORMAT, "Not a WAVE file");
        return 0;
    }

    /* Find and parse fmt chunk */
    size_t offset = 12;
    int found_fmt = 0;
    int found_data = 0;

    while (offset + 8 <= size)
    {
        uint32_t chunk_id = read_u32_le(data + offset);
        uint32_t chunk_size = read_u32_le(data + offset + 4);

        if (chunk_id == WAV_FMT_ID)
        {
            if (chunk_size < 16 || offset + 8 + chunk_size > size)
            {
                vaud_set_error(VAUD_ERR_FORMAT, "Invalid fmt chunk");
                return 0;
            }

            const uint8_t *fmt = data + offset + 8;
            uint16_t audio_format = read_u16_le(fmt);

            if (audio_format != WAV_FORMAT_PCM)
            {
                vaud_set_error(VAUD_ERR_FORMAT, "Only PCM format is supported");
                return 0;
            }

            info->channels = read_u16_le(fmt + 2);
            info->sample_rate = (int32_t)read_u32_le(fmt + 4);
            info->bits_per_sample = read_u16_le(fmt + 14);

            /* H-7: guard against division-by-zero in resampling (malformed file) */
            if (info->sample_rate <= 0 || info->sample_rate > 384000)
            {
                vaud_set_error(VAUD_ERR_FORMAT, "Invalid WAV sample rate");
                return 0;
            }

            if (info->channels < 1 || info->channels > 2)
            {
                vaud_set_error(VAUD_ERR_FORMAT, "Only mono and stereo supported");
                return 0;
            }

            if (info->bits_per_sample != 8 && info->bits_per_sample != 16)
            {
                vaud_set_error(VAUD_ERR_FORMAT, "Only 8-bit and 16-bit PCM supported");
                return 0;
            }

            found_fmt = 1;
        }
        else if (chunk_id == WAV_DATA_ID)
        {
            info->data_offset = (int64_t)(offset + 8);
            info->data_size = (int64_t)chunk_size;
            found_data = 1;
        }

        /* Move to next chunk (chunks are word-aligned) */
        offset += 8 + chunk_size;
        if (chunk_size & 1)
            offset++; /* Pad byte */

        if (found_fmt && found_data)
            break;
    }

    if (!found_fmt)
    {
        vaud_set_error(VAUD_ERR_FORMAT, "Missing fmt chunk");
        return 0;
    }

    if (!found_data)
    {
        vaud_set_error(VAUD_ERR_FORMAT, "Missing data chunk");
        return 0;
    }

    return 1;
}

//===----------------------------------------------------------------------===//
// PCM Conversion
//===----------------------------------------------------------------------===//

/// @brief Convert PCM data to internal format (16-bit stereo).
/// @param data Source PCM data.
/// @param info Format information.
/// @param out_samples Output: allocated stereo 16-bit samples.
/// @param out_frames Output: number of frames.
/// @return 1 on success, 0 on failure.
static int convert_pcm_to_stereo_s16(const uint8_t *data,
                                     const vaud_wav_info *info,
                                     int16_t **out_samples,
                                     int64_t *out_frames)
{
    int32_t bytes_per_sample = info->bits_per_sample / 8;
    int32_t bytes_per_frame = bytes_per_sample * info->channels;
    int64_t frame_count = info->data_size / bytes_per_frame;

    /* Allocate output buffer (always stereo) */
    int16_t *samples = (int16_t *)malloc((size_t)(frame_count * 2 * sizeof(int16_t)));
    if (!samples)
    {
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate sample buffer");
        return 0;
    }

    const uint8_t *src = data + info->data_offset;
    int16_t *dst = samples;

    for (int64_t i = 0; i < frame_count; i++)
    {
        int16_t left, right;
        decode_pcm_frame(src, info->bits_per_sample, info->channels, &left, &right);
        *dst++ = left;
        *dst++ = right;
        src += bytes_per_frame;
    }

    *out_samples = samples;
    *out_frames = frame_count;
    return 1;
}

//===----------------------------------------------------------------------===//
// Public Functions
//===----------------------------------------------------------------------===//

int vaud_wav_load_file(const char *path,
                       int16_t **out_samples,
                       int64_t *out_frames,
                       int32_t *out_sample_rate,
                       int32_t *out_channels)
{
    if (!path || !out_samples || !out_frames || !out_sample_rate || !out_channels)
    {
        vaud_set_error(VAUD_ERR_INVALID_PARAM, "NULL parameter");
        return 0;
    }

    /* Open file */
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        vaud_set_error(VAUD_ERR_FILE, "Failed to open WAV file");
        return 0;
    }

    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 100 * 1024 * 1024) /* Max 100MB for sound effects */
    {
        fclose(file);
        vaud_set_error(VAUD_ERR_FILE, "Invalid file size");
        return 0;
    }

    /* Read entire file */
    uint8_t *data = (uint8_t *)malloc((size_t)file_size);
    if (!data)
    {
        fclose(file);
        vaud_set_error(VAUD_ERR_ALLOC, "Failed to allocate file buffer");
        return 0;
    }

    size_t read_size = fread(data, 1, (size_t)file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size)
    {
        free(data);
        vaud_set_error(VAUD_ERR_FILE, "Failed to read WAV file");
        return 0;
    }

    /* Parse and convert */
    int result = vaud_wav_load_mem(
        data, (size_t)file_size, out_samples, out_frames, out_sample_rate, out_channels);
    free(data);
    return result;
}

int vaud_wav_load_mem(const void *data,
                      size_t size,
                      int16_t **out_samples,
                      int64_t *out_frames,
                      int32_t *out_sample_rate,
                      int32_t *out_channels)
{
    if (!data || !out_samples || !out_frames || !out_sample_rate || !out_channels)
    {
        vaud_set_error(VAUD_ERR_INVALID_PARAM, "NULL parameter");
        return 0;
    }

    /* Parse header */
    vaud_wav_info info;
    if (!parse_wav_header((const uint8_t *)data, size, &info))
    {
        return 0;
    }

    /* Verify data is within bounds */
    if (info.data_offset + info.data_size > (int64_t)size)
    {
        vaud_set_error(VAUD_ERR_FORMAT, "Data chunk extends beyond file");
        return 0;
    }

    /* Convert to internal format */
    if (!convert_pcm_to_stereo_s16((const uint8_t *)data, &info, out_samples, out_frames))
    {
        return 0;
    }

    *out_sample_rate = info.sample_rate;
    *out_channels = info.channels;
    return 1;
}

int vaud_wav_open_stream(const char *path,
                         void **out_file,
                         int64_t *out_data_offset,
                         int64_t *out_data_size,
                         int64_t *out_frames,
                         int32_t *out_sample_rate,
                         int32_t *out_channels,
                         int32_t *out_bits)
{
    if (!path || !out_file || !out_data_offset || !out_data_size || !out_frames ||
        !out_sample_rate || !out_channels || !out_bits)
    {
        vaud_set_error(VAUD_ERR_INVALID_PARAM, "NULL parameter");
        return 0;
    }

    /* Open file */
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        vaud_set_error(VAUD_ERR_FILE, "Failed to open music file");
        return 0;
    }

    /* Read header (first 256 bytes should be enough for any WAV header) */
    uint8_t header[256];
    size_t header_size = fread(header, 1, sizeof(header), file);
    if (header_size < 44)
    {
        fclose(file);
        vaud_set_error(VAUD_ERR_FORMAT, "WAV file too small");
        return 0;
    }

    /* Parse header */
    vaud_wav_info info;
    if (!parse_wav_header(header, header_size, &info))
    {
        fclose(file);
        return 0;
    }

    /* Calculate frame count */
    int32_t bytes_per_sample = info.bits_per_sample / 8;
    int32_t bytes_per_frame = bytes_per_sample * info.channels;
    int64_t frame_count = info.data_size / bytes_per_frame;

    *out_file = file;
    *out_data_offset = info.data_offset;
    *out_data_size = info.data_size;
    *out_frames = frame_count;
    *out_sample_rate = info.sample_rate;
    *out_channels = info.channels;
    *out_bits = info.bits_per_sample;

    /* Seek to start of data */
    fseek(file, (long)info.data_offset, SEEK_SET);

    return 1;
}

int32_t vaud_wav_read_frames(
    void *file, int16_t *samples, int32_t frames, int32_t channels, int32_t bits_per_sample)
{
    if (!file || !samples || frames <= 0)
        return 0;

    FILE *f = (FILE *)file;
    int32_t bytes_per_sample = bits_per_sample / 8;
    int32_t bytes_per_frame = bytes_per_sample * channels;
    size_t buffer_size = (size_t)(frames * bytes_per_frame);

    /* Temporary buffer for reading raw data */
    uint8_t *temp = (uint8_t *)malloc(buffer_size);
    if (!temp)
        return 0;

    size_t bytes_read = fread(temp, 1, buffer_size, f);
    int32_t frames_read = (int32_t)(bytes_read / (size_t)bytes_per_frame);

    /* Convert to 16-bit stereo */
    const uint8_t *src = temp;
    int16_t *dst = samples;

    for (int32_t i = 0; i < frames_read; i++)
    {
        int16_t left, right;
        decode_pcm_frame(src, bits_per_sample, channels, &left, &right);
        *dst++ = left;
        *dst++ = right;
        src += bytes_per_frame;
    }

    free(temp);
    return frames_read;
}

//===----------------------------------------------------------------------===//
// Resampling
//===----------------------------------------------------------------------===//

int64_t vaud_resample_output_frames(int64_t in_frames, int32_t in_rate, int32_t out_rate)
{
    if (in_rate == out_rate)
        return in_frames;
    return (in_frames * out_rate + in_rate - 1) / in_rate;
}

void vaud_resample(const int16_t *input,
                   int64_t in_frames,
                   int32_t in_rate,
                   int16_t *output,
                   int64_t out_frames,
                   int32_t out_rate,
                   int32_t channels)
{
    if (!input || !output || in_frames <= 0 || out_frames <= 0)
        return;

    /* Simple linear interpolation resampler */
    double ratio = (double)in_rate / (double)out_rate;

    for (int64_t out_idx = 0; out_idx < out_frames; out_idx++)
    {
        double in_pos = out_idx * ratio;
        int64_t in_idx = (int64_t)in_pos;
        double frac = in_pos - in_idx;

        /* Clamp to valid range */
        if (in_idx >= in_frames - 1)
        {
            in_idx = in_frames - 1;
            frac = 0.0;
        }

        /* Interpolate each channel */
        for (int32_t ch = 0; ch < channels; ch++)
        {
            int16_t s0 = input[in_idx * channels + ch];
            int16_t s1 = (in_idx + 1 < in_frames) ? input[(in_idx + 1) * channels + ch] : s0;
            int32_t interp = (int32_t)(s0 * (1.0 - frac) + s1 * frac);

            /* Clamp to 16-bit range */
            if (interp > 32767)
                interp = 32767;
            if (interp < -32768)
                interp = -32768;

            output[out_idx * channels + ch] = (int16_t)interp;
        }
    }
}
