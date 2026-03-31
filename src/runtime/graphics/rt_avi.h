//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_avi.h
// Purpose: AVI RIFF container parser — extracts interleaved video frames
//   and audio chunks from AVI files for use by VideoPlayer.
//
// Key invariants:
//   - AVI is a RIFF format: flat chunk structure with FOURCC tags.
//   - Video frames identified by 'XXdc' or 'XXdb' chunks (compressed/DIB).
//   - Audio chunks identified by 'XXwb' chunks (wave bytes).
//   - Parser operates on a memory buffer (caller owns the data).
//
// Links: rt_videoplayer.h, rt_pixels.h (JPEG decode)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Video stream info extracted from AVI header.
typedef struct {
    uint32_t fourcc;       /* codec FOURCC (e.g., 'MJPG', 'mjpg') */
    int32_t width, height;
    int32_t frame_count;
    double fps;            /* from avih.dwMicroSecPerFrame */
    double duration;       /* frame_count / fps */
} avi_video_info_t;

/// @brief Audio stream info extracted from AVI header.
typedef struct {
    int32_t sample_rate;
    int32_t channels;
    int32_t bits_per_sample;
    int32_t block_align;
} avi_audio_info_t;

/// @brief Descriptor for one chunk in the movi list.
typedef struct {
    const uint8_t *data;   /* pointer into file buffer (not owned) */
    uint32_t size;         /* chunk payload size */
    int8_t is_video;       /* 1=video, 0=audio */
} avi_chunk_t;

/// @brief Parsed AVI file context.
typedef struct {
    const uint8_t *file_data; /* file buffer (not owned) */
    size_t file_len;
    avi_video_info_t video;
    avi_audio_info_t audio;
    int8_t has_audio;
    /* Chunk index: all movi chunks in playback order */
    avi_chunk_t *chunks;
    int32_t chunk_count;
    int32_t chunk_capacity;
    /* Video-only index for frame-based access */
    int32_t *video_indices; /* chunk_count entries mapping frame# → chunk index */
    int32_t video_frame_count;
} avi_context_t;

/// @brief Parse an AVI file from a memory buffer.
/// @return 0 on success, -1 on error.
int avi_parse(avi_context_t *ctx, const uint8_t *data, size_t len);

/// @brief Free internal allocations (does NOT free file_data).
void avi_free(avi_context_t *ctx);

/// @brief Get video frame data at given frame index.
/// @return Pointer to compressed frame data, or NULL. Sets *out_size.
const uint8_t *avi_get_video_frame(const avi_context_t *ctx,
                                    int32_t frame_index,
                                    uint32_t *out_size);

#ifdef __cplusplus
}
#endif
