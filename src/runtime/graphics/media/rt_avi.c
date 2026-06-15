//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_avi.c
// Purpose: AVI RIFF container parser. Walks the RIFF chunk tree to extract
//   video stream info (codec, dimensions, frame rate), audio stream info
//   (sample rate, channels, bit depth), and builds an index of interleaved
//   movi chunks for frame-by-frame access.
//
// Key invariants:
//   - RIFF chunks are 2-byte aligned (pad byte after odd-sized chunks).
//   - Stream numbers are 2-digit ASCII in chunk FOURCCs (e.g., '00dc').
//   - Parser validates all offsets to prevent out-of-bounds reads.
//   - No memory allocated for the file data itself (caller owns it).
//
// Links: rt_avi.h, rt_videoplayer.c
//
//===----------------------------------------------------------------------===//

#include "rt_avi.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*==========================================================================
 * Little-endian helpers
 *=========================================================================*/

/// @brief Read a 32-bit little-endian unsigned integer from a byte pointer.
static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/// @brief Read a 16-bit little-endian unsigned integer from a byte pointer.
static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/// @brief Build a 32-bit FOURCC tag from four ASCII characters (packed little-endian).
static uint32_t make_fourcc(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) | ((uint32_t)(uint8_t)c << 16) |
           ((uint32_t)(uint8_t)d << 24);
}

#define FOURCC_RIFF make_fourcc('R', 'I', 'F', 'F')
#define FOURCC_AVI make_fourcc('A', 'V', 'I', ' ')
#define FOURCC_LIST make_fourcc('L', 'I', 'S', 'T')
#define FOURCC_hdrl make_fourcc('h', 'd', 'r', 'l')
#define FOURCC_strl make_fourcc('s', 't', 'r', 'l')
#define FOURCC_movi make_fourcc('m', 'o', 'v', 'i')
#define FOURCC_avih make_fourcc('a', 'v', 'i', 'h')
#define FOURCC_strh make_fourcc('s', 't', 'r', 'h')
#define FOURCC_strf make_fourcc('s', 't', 'r', 'f')
#define FOURCC_vids make_fourcc('v', 'i', 'd', 's')
#define FOURCC_auds make_fourcc('a', 'u', 'd', 's')
#define FOURCC_rec make_fourcc('r', 'e', 'c', ' ')

typedef struct {
    int stream_type;  ///< -1=unknown, 0=video, 1=audio.
    int stream_index; ///< Zero-based stream list index from hdrl.
} avi_stream_parse_state_t;

/// @brief Extract the two-digit stream number encoded at the start of an AVI chunk FOURCC.
/// @details AVI media chunks conventionally use tags such as `00dc`,
///          `01wb`, or `02db`. Chunks without two leading ASCII digits
///          return -1 and are ignored by stream-index filtering.
/// @param fourcc Packed little-endian chunk tag.
/// @return Stream index 0-99, or -1 when the tag is not stream-numbered.
static int avi_stream_index_from_fourcc(uint32_t fourcc) {
    uint8_t c0 = (uint8_t)(fourcc & 0xFFu);
    uint8_t c1 = (uint8_t)((fourcc >> 8) & 0xFFu);
    if (c0 < '0' || c0 > '9' || c1 < '0' || c1 > '9')
        return -1;
    return (int)(c0 - '0') * 10 + (int)(c1 - '0');
}

/*==========================================================================
 * Chunk addition
 *=========================================================================*/

/// @brief Append a movi chunk record to the context's chunk list, growing the array as needed.
/// @details Geometric growth: starts at capacity 64, doubles thereafter.
///          Used to record the location of every video and audio chunk in
///          the movi list during parsing — the post-parse pass then walks
///          this array to build the video-frame index.
/// @return 0 on success; -1 on allocation failure (chunk is dropped).
static int add_chunk(avi_context_t *ctx, const uint8_t *data, uint32_t size, int8_t is_video) {
    if (ctx->chunk_count >= ctx->chunk_capacity) {
        if (ctx->chunk_capacity > INT32_MAX / 2)
            return -1;
        int32_t new_cap = ctx->chunk_capacity < 64 ? 64 : ctx->chunk_capacity * 2;
        if ((uint64_t)new_cap > (uint64_t)SIZE_MAX / sizeof(avi_chunk_t))
            return -1;
        avi_chunk_t *new_chunks =
            (avi_chunk_t *)realloc(ctx->chunks, (size_t)new_cap * sizeof(avi_chunk_t));
        if (!new_chunks)
            return -1;
        ctx->chunks = new_chunks;
        ctx->chunk_capacity = new_cap;
    }
    ctx->chunks[ctx->chunk_count].data = data;
    ctx->chunks[ctx->chunk_count].size = size;
    ctx->chunks[ctx->chunk_count].is_video = is_video;
    ctx->chunk_count++;
    return 0;
}

/*==========================================================================
 * Header parsing
 *=========================================================================*/

/// @brief Parse the main AVI header (avih chunk).
static void parse_avih(avi_context_t *ctx, const uint8_t *data, uint32_t size) {
    if (size < 40)
        return;
    uint32_t us_per_frame = read_le32(data + 0);
    /* bytes 4-12: dwMaxBytesPerSec, dwPaddingGranularity, dwFlags */
    uint32_t total_frames = read_le32(data + 16);
    /* bytes 20-28: dwInitialFrames, dwStreams */
    /* bytes 32: dwSuggestedBufferSize */
    uint32_t width = read_le32(data + 32);
    uint32_t height = read_le32(data + 36);

    if (width <= INT32_MAX)
        ctx->video.width = (int32_t)width;
    if (height <= INT32_MAX)
        ctx->video.height = (int32_t)height;
    if (total_frames <= INT32_MAX)
        ctx->video.frame_count = (int32_t)total_frames;
    if (us_per_frame > 0)
        ctx->video.fps = 1000000.0 / (double)us_per_frame;
    else
        ctx->video.fps = 30.0; /* default */
    if (ctx->video.fps > 0.0 && ctx->video.frame_count > 0)
        ctx->video.duration = (double)ctx->video.frame_count / ctx->video.fps;
}

/// @brief Parse a stream header (strh chunk).
static void parse_strh(avi_context_t *ctx,
                       const uint8_t *data,
                       uint32_t size,
                       avi_stream_parse_state_t *stream) {
    if (size < 48)
        return;
    uint32_t fcc_type = read_le32(data + 0);
    uint32_t fcc_handler = read_le32(data + 4);
    uint32_t scale = read_le32(data + 20);
    uint32_t rate = read_le32(data + 24);
    uint32_t length = read_le32(data + 32);

    if (fcc_type == FOURCC_vids) {
        stream->stream_type = 0; /* video */
        if (ctx->video_stream_index < 0) {
            ctx->video_stream_index = stream->stream_index;
            ctx->video.fourcc = fcc_handler;
            if (scale > 0 && rate > 0)
                ctx->video.fps = (double)rate / (double)scale;
            if (length > 0 && length <= (uint32_t)INT32_MAX)
                ctx->video.frame_count = (int32_t)length;
            if (ctx->video.fps > 0.0 && ctx->video.frame_count > 0)
                ctx->video.duration = (double)ctx->video.frame_count / ctx->video.fps;
        }
    } else if (fcc_type == FOURCC_auds) {
        stream->stream_type = 1; /* audio */
        if (ctx->audio_stream_index < 0) {
            ctx->audio_stream_index = stream->stream_index;
            ctx->has_audio = 1;
        }
    } else {
        stream->stream_type = -1;
    }
}

/// @brief Parse a stream format (strf chunk) for video.
static void parse_strf_video(avi_context_t *ctx, const uint8_t *data, uint32_t size) {
    if (size < 40)
        return;
    /* BITMAPINFOHEADER */
    int32_t raw_width = (int32_t)read_le32(data + 4);
    int32_t raw_height = (int32_t)read_le32(data + 8);
    uint32_t bi_compression = read_le32(data + 16);
    if (raw_width > 0)
        ctx->video.width = (int32_t)raw_width;
    if (raw_height == INT32_MIN)
        return;
    if (raw_height < 0)
        raw_height = -raw_height;
    if (raw_height > 0)
        ctx->video.height = raw_height;
    if (bi_compression != 0)
        ctx->video.fourcc = bi_compression;
}

/// @brief Parse a stream format (strf chunk) for audio.
static void parse_strf_audio(avi_context_t *ctx, const uint8_t *data, uint32_t size) {
    if (size < 16)
        return;
    /* WAVEFORMATEX */
    uint16_t format_tag = read_le16(data);
    int32_t channels = (int32_t)read_le16(data + 2);
    uint32_t sample_rate_u = read_le32(data + 4);
    int32_t block_align = (int32_t)read_le16(data + 12);
    int32_t bits_per_sample = (int32_t)read_le16(data + 14);
    if ((format_tag != 1u && format_tag != 3u) || channels <= 0 || channels > 8 ||
        sample_rate_u == 0 || sample_rate_u > 384000u || sample_rate_u > (uint32_t)INT32_MAX ||
        block_align <= 0 ||
        (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 24 &&
         bits_per_sample != 32 && bits_per_sample != 64)) {
        ctx->has_audio = 0;
        ctx->audio_stream_index = -1;
        memset(&ctx->audio, 0, sizeof(ctx->audio));
        return;
    }
    ctx->audio.channels = channels;
    ctx->audio.sample_rate = (int32_t)sample_rate_u;
    ctx->audio.block_align = block_align;
    ctx->audio.bits_per_sample = bits_per_sample;
    ctx->has_audio = 1;
}

/*==========================================================================
 * RIFF chunk walking
 *=========================================================================*/

/// @brief Walk chunks at a given level, calling handler for each.
static int walk_chunks(const uint8_t *data,
                       size_t len,
                       size_t start,
                       void (*handler)(avi_context_t *,
                                       uint32_t fourcc,
                                       const uint8_t *payload,
                                       uint32_t size,
                                       void *extra),
                       avi_context_t *ctx,
                       void *extra) {
    size_t pos = start;
    while (pos <= len && len - pos >= 8) {
        uint32_t fourcc = read_le32(data + pos);
        uint32_t size = read_le32(data + pos + 4);
        if (size > len - pos - 8) {
            if (ctx)
                ctx->parse_error = 1;
            return -1;
        }
        handler(ctx, fourcc, data + pos + 8, size, extra);
        if (ctx && ctx->parse_error)
            break;
        pos += 8 + size;
        if (size & 1) {
            if (pos >= len) {
                if (ctx)
                    ctx->parse_error = 1;
                return -1;
            }
            pos++; /* RIFF 2-byte alignment */
        }
    }
    return 0;
}

/*==========================================================================
 * Parse handlers
 *=========================================================================*/

/// @brief Handle chunks inside a strl LIST.
static void handle_strl(
    avi_context_t *ctx, uint32_t fourcc, const uint8_t *payload, uint32_t size, void *extra) {
    avi_stream_parse_state_t *stream = (avi_stream_parse_state_t *)extra;
    if (fourcc == FOURCC_strh)
        parse_strh(ctx, payload, size, stream);
    else if (fourcc == FOURCC_strf) {
        if (stream->stream_type == 0 && stream->stream_index == ctx->video_stream_index)
            parse_strf_video(ctx, payload, size);
        else if (stream->stream_type == 1 && stream->stream_index == ctx->audio_stream_index)
            parse_strf_audio(ctx, payload, size);
    }
}

/// @brief Handle chunks inside the hdrl LIST.
static void handle_hdrl(
    avi_context_t *ctx, uint32_t fourcc, const uint8_t *payload, uint32_t size, void *extra) {
    (void)extra;
    if (fourcc == FOURCC_avih) {
        parse_avih(ctx, payload, size);
    } else if (fourcc == FOURCC_LIST && size >= 4) {
        uint32_t list_type = read_le32(payload);
        if (list_type == FOURCC_strl) {
            avi_stream_parse_state_t stream = {-1, ctx->stream_count};
            if (ctx->stream_count < INT32_MAX)
                ctx->stream_count++;
            walk_chunks(payload, size, 4, handle_strl, ctx, &stream);
        }
    }
}

/// @brief Handle chunks inside the movi LIST.
static void handle_movi(
    avi_context_t *ctx, uint32_t fourcc, const uint8_t *payload, uint32_t size, void *extra) {
    (void)extra;
    if (fourcc == FOURCC_LIST && size >= 4) {
        uint32_t list_type = read_le32(payload);
        if (list_type == FOURCC_rec)
            walk_chunks(payload, size, 4, handle_movi, ctx, NULL);
        return;
    }
    /* Check chunk type by last 2 chars of FOURCC:
     * 'dc' or 'db' = compressed/DIB video, 'wb' = wave bytes (audio) */
    uint8_t c2 = (uint8_t)((fourcc >> 16) & 0xFF);
    uint8_t c3 = (uint8_t)((fourcc >> 24) & 0xFF);
    int stream_index = avi_stream_index_from_fourcc(fourcc);
    if ((c2 == 'd' && (c3 == 'c' || c3 == 'b'))) {
        if (stream_index == ctx->video_stream_index && add_chunk(ctx, payload, size, 1) != 0)
            ctx->parse_error = 1; /* video */
    } else if (c2 == 'w' && c3 == 'b') {
        if (stream_index == ctx->audio_stream_index && add_chunk(ctx, payload, size, 0) != 0)
            ctx->parse_error = 1; /* audio */
    }
    /* Skip 'ix##', 'JUNK' and other chunks */
}

/// @brief Handle top-level RIFF chunks.
static void handle_top(
    avi_context_t *ctx, uint32_t fourcc, const uint8_t *payload, uint32_t size, void *extra) {
    (void)extra;
    if (fourcc == FOURCC_LIST && size >= 4) {
        uint32_t list_type = read_le32(payload);
        if (list_type == FOURCC_hdrl)
            walk_chunks(payload, size, 4, handle_hdrl, ctx, NULL);
        else if (list_type == FOURCC_movi)
            walk_chunks(payload, size, 4, handle_movi, ctx, NULL);
    }
}

/*==========================================================================
 * Public API
 *=========================================================================*/

/// @brief Parse an AVI file's RIFF chunk tree and build a frame index.
/// @details Validates the `RIFF....AVI ` header, walks the top-level chunks
///          (driving the parse via `handle_top` → `handle_hdrl` /
///          `handle_movi`), then post-processes the recorded chunks into a
///          dense index of video-frame chunks for `avi_get_video_frame`
///          random access. The header-reported frame count is overridden
///          by the actual movi-chunk count when the header lies (common in
///          files produced by sloppy encoders).
///          Note: `data` is not copied — the caller owns it and must keep
///          the buffer alive for the lifetime of `ctx`. Only the chunks
///          and indices arrays are heap-allocated; freed by `avi_free`.
/// @return 0 on success (at least one video frame found); -1 on header
///         validation failure or zero video frames.
int avi_parse(avi_context_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data || len < 12)
        return -1;

    {
        avi_context_t zero = {0};
        *ctx = zero;
    }
    ctx->file_data = data;
    ctx->file_len = len;
    ctx->video_stream_index = -1;
    ctx->audio_stream_index = -1;

    /* Validate RIFF header */
    if (read_le32(data) != FOURCC_RIFF)
        return -1;
    uint32_t riff_size = read_le32(data + 4);
    if (riff_size < 4u || (size_t)riff_size > SIZE_MAX - 8u)
        return -1;
    size_t riff_end = (size_t)riff_size + 8u;
    if (riff_end > len)
        return -1;
    if (read_le32(data + 8) != FOURCC_AVI)
        return -1;

    /* Walk top-level chunks (skip RIFF header: 12 bytes) */
    walk_chunks(data, riff_end, 12, handle_top, ctx, NULL);
    if (ctx->parse_error) {
        avi_free(ctx);
        return -1;
    }

    /* Build video frame index */
    if (ctx->chunk_count > 0) {
        ctx->video_indices = (int32_t *)malloc((size_t)ctx->chunk_count * sizeof(int32_t));
        if (ctx->video_indices) {
            ctx->video_frame_count = 0;
            for (int32_t i = 0; i < ctx->chunk_count; i++) {
                if (ctx->chunks[i].is_video) {
                    ctx->video_indices[ctx->video_frame_count] = i;
                    ctx->video_frame_count++;
                }
            }
        } else {
            avi_free(ctx);
            return -1;
        }
        /* Update frame count from actual movi data if header was wrong */
        if (ctx->video_frame_count > 0 && ctx->video.frame_count == 0)
            ctx->video.frame_count = ctx->video_frame_count;
        if (ctx->video.fps > 0.0 && ctx->video.frame_count > 0)
            ctx->video.duration = (double)ctx->video.frame_count / ctx->video.fps;
    }

    if (ctx->video_frame_count <= 0) {
        avi_free(ctx);
        return -1;
    }
    return 0;
}

/// @brief Free the chunk list and frame index owned by `ctx`.
/// @details Does not free the file-data buffer — that's owned by the caller.
///          Safe to call multiple times; the second call sees the already-
///          NULL pointers and is a no-op.
void avi_free(avi_context_t *ctx) {
    if (!ctx)
        return;
    free(ctx->chunks);
    free(ctx->video_indices);
    ctx->chunks = NULL;
    ctx->video_indices = NULL;
    ctx->chunk_count = 0;
    ctx->video_frame_count = 0;
}

/// @brief Random-access fetch of one video frame's compressed payload.
/// @details Uses the frame index built during parse, so this is O(1).
///          The returned pointer points into the original file-data buffer
///          (no copy); caller must not free or modify it. `out_size` may
///          be NULL if the caller doesn't need the chunk size.
/// @return Frame data pointer + size, or NULL on out-of-range index.
const uint8_t *avi_get_video_frame(const avi_context_t *ctx,
                                   int32_t frame_index,
                                   uint32_t *out_size) {
    if (!ctx || frame_index < 0 || frame_index >= ctx->video_frame_count)
        return NULL;
    int32_t chunk_idx = ctx->video_indices[frame_index];
    if (out_size)
        *out_size = ctx->chunks[chunk_idx].size;
    return ctx->chunks[chunk_idx].data;
}
