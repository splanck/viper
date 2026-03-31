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

#include <stdlib.h>
#include <string.h>

/*==========================================================================
 * Little-endian helpers
 *=========================================================================*/

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t make_fourcc(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) |
           ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}

#define FOURCC_RIFF make_fourcc('R', 'I', 'F', 'F')
#define FOURCC_AVI  make_fourcc('A', 'V', 'I', ' ')
#define FOURCC_LIST make_fourcc('L', 'I', 'S', 'T')
#define FOURCC_hdrl make_fourcc('h', 'd', 'r', 'l')
#define FOURCC_strl make_fourcc('s', 't', 'r', 'l')
#define FOURCC_movi make_fourcc('m', 'o', 'v', 'i')
#define FOURCC_avih make_fourcc('a', 'v', 'i', 'h')
#define FOURCC_strh make_fourcc('s', 't', 'r', 'h')
#define FOURCC_strf make_fourcc('s', 't', 'r', 'f')
#define FOURCC_vids make_fourcc('v', 'i', 'd', 's')
#define FOURCC_auds make_fourcc('a', 'u', 'd', 's')

/*==========================================================================
 * Chunk addition
 *=========================================================================*/

static int add_chunk(avi_context_t *ctx, const uint8_t *data, uint32_t size,
                      int8_t is_video) {
    if (ctx->chunk_count >= ctx->chunk_capacity) {
        int32_t new_cap = ctx->chunk_capacity < 64 ? 64 : ctx->chunk_capacity * 2;
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

    ctx->video.width = (int32_t)width;
    ctx->video.height = (int32_t)height;
    ctx->video.frame_count = (int32_t)total_frames;
    if (us_per_frame > 0)
        ctx->video.fps = 1000000.0 / (double)us_per_frame;
    else
        ctx->video.fps = 30.0; /* default */
    if (ctx->video.fps > 0.0 && ctx->video.frame_count > 0)
        ctx->video.duration =
            (double)ctx->video.frame_count / ctx->video.fps;
}

/// @brief Parse a stream header (strh chunk).
static void parse_strh(avi_context_t *ctx, const uint8_t *data, uint32_t size,
                        int *stream_type) {
    if (size < 48)
        return;
    uint32_t fcc_type = read_le32(data + 0);
    uint32_t fcc_handler = read_le32(data + 4);

    if (fcc_type == FOURCC_vids) {
        *stream_type = 0; /* video */
        ctx->video.fourcc = fcc_handler;
    } else if (fcc_type == FOURCC_auds) {
        *stream_type = 1; /* audio */
        ctx->has_audio = 1;
    } else {
        *stream_type = -1;
    }
}

/// @brief Parse a stream format (strf chunk) for video.
static void parse_strf_video(avi_context_t *ctx, const uint8_t *data,
                              uint32_t size) {
    if (size < 40)
        return;
    /* BITMAPINFOHEADER */
    int32_t bi_width = (int32_t)read_le32(data + 4);
    int32_t bi_height = (int32_t)read_le32(data + 8);
    uint32_t bi_compression = read_le32(data + 16);
    if (bi_width > 0)
        ctx->video.width = bi_width;
    if (bi_height > 0)
        ctx->video.height = bi_height;
    if (bi_compression != 0)
        ctx->video.fourcc = bi_compression;
}

/// @brief Parse a stream format (strf chunk) for audio.
static void parse_strf_audio(avi_context_t *ctx, const uint8_t *data,
                              uint32_t size) {
    if (size < 16)
        return;
    /* WAVEFORMATEX */
    ctx->audio.channels = (int32_t)read_le16(data + 2);
    ctx->audio.sample_rate = (int32_t)read_le32(data + 4);
    ctx->audio.block_align = (int32_t)read_le16(data + 12);
    ctx->audio.bits_per_sample = (int32_t)read_le16(data + 14);
}

/*==========================================================================
 * RIFF chunk walking
 *=========================================================================*/

/// @brief Walk chunks at a given level, calling handler for each.
static int walk_chunks(const uint8_t *data, size_t len, size_t start,
                        void (*handler)(avi_context_t *, uint32_t fourcc,
                                        const uint8_t *payload, uint32_t size,
                                        void *extra),
                        avi_context_t *ctx, void *extra) {
    size_t pos = start;
    while (pos + 8 <= len) {
        uint32_t fourcc = read_le32(data + pos);
        uint32_t size = read_le32(data + pos + 4);
        if (pos + 8 + size > len)
            break;
        handler(ctx, fourcc, data + pos + 8, size, extra);
        pos += 8 + size;
        if (size & 1)
            pos++; /* RIFF 2-byte alignment */
    }
    return 0;
}

/*==========================================================================
 * Parse handlers
 *=========================================================================*/

/// @brief Handle chunks inside a strl LIST.
static void handle_strl(avi_context_t *ctx, uint32_t fourcc,
                         const uint8_t *payload, uint32_t size, void *extra) {
    int *stream_type = (int *)extra;
    if (fourcc == FOURCC_strh)
        parse_strh(ctx, payload, size, stream_type);
    else if (fourcc == FOURCC_strf) {
        if (*stream_type == 0)
            parse_strf_video(ctx, payload, size);
        else if (*stream_type == 1)
            parse_strf_audio(ctx, payload, size);
    }
}

/// @brief Handle chunks inside the hdrl LIST.
static void handle_hdrl(avi_context_t *ctx, uint32_t fourcc,
                         const uint8_t *payload, uint32_t size, void *extra) {
    (void)extra;
    if (fourcc == FOURCC_avih) {
        parse_avih(ctx, payload, size);
    } else if (fourcc == FOURCC_LIST && size >= 4) {
        uint32_t list_type = read_le32(payload);
        if (list_type == FOURCC_strl) {
            int stream_type = -1;
            walk_chunks(payload, size, 4, handle_strl, ctx, &stream_type);
        }
    }
}

/// @brief Handle chunks inside the movi LIST.
static void handle_movi(avi_context_t *ctx, uint32_t fourcc,
                         const uint8_t *payload, uint32_t size, void *extra) {
    (void)extra;
    /* Check chunk type by last 2 chars of FOURCC:
     * 'dc' or 'db' = compressed/DIB video, 'wb' = wave bytes (audio) */
    uint8_t c2 = (uint8_t)((fourcc >> 16) & 0xFF);
    uint8_t c3 = (uint8_t)((fourcc >> 24) & 0xFF);
    if ((c2 == 'd' && (c3 == 'c' || c3 == 'b'))) {
        add_chunk(ctx, payload, size, 1); /* video */
    } else if (c2 == 'w' && c3 == 'b') {
        add_chunk(ctx, payload, size, 0); /* audio */
    }
    /* Skip 'rec ', 'ix##', 'JUNK' and other chunks */
}

/// @brief Handle top-level RIFF chunks.
static void handle_top(avi_context_t *ctx, uint32_t fourcc,
                        const uint8_t *payload, uint32_t size, void *extra) {
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

int avi_parse(avi_context_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data || len < 12)
        return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->file_data = data;
    ctx->file_len = len;

    /* Validate RIFF header */
    if (read_le32(data) != FOURCC_RIFF)
        return -1;
    /* uint32_t riff_size = read_le32(data + 4); */
    if (read_le32(data + 8) != FOURCC_AVI)
        return -1;

    /* Walk top-level chunks (skip RIFF header: 12 bytes) */
    walk_chunks(data, len, 12, handle_top, ctx, NULL);

    /* Build video frame index */
    if (ctx->chunk_count > 0) {
        ctx->video_indices =
            (int32_t *)malloc((size_t)ctx->chunk_count * sizeof(int32_t));
        if (ctx->video_indices) {
            ctx->video_frame_count = 0;
            for (int32_t i = 0; i < ctx->chunk_count; i++) {
                if (ctx->chunks[i].is_video) {
                    ctx->video_indices[ctx->video_frame_count] = i;
                    ctx->video_frame_count++;
                }
            }
        }
        /* Update frame count from actual movi data if header was wrong */
        if (ctx->video_frame_count > 0 && ctx->video.frame_count == 0)
            ctx->video.frame_count = ctx->video_frame_count;
        if (ctx->video.fps > 0.0 && ctx->video.frame_count > 0)
            ctx->video.duration =
                (double)ctx->video.frame_count / ctx->video.fps;
    }

    return (ctx->video_frame_count > 0) ? 0 : -1;
}

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
