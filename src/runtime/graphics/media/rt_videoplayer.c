//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_videoplayer.c
// Purpose: Video playback engine — loads AVI/MJPEG and OGG/Theora containers,
//   decodes video frames to Pixels, and manages playback state and sync.
//
// Key invariants:
//   - Frame decode uses rt_jpeg_decode_buffer for MJPEG and rt_theora for OGG.
//   - Double-buffered frames: display + decode, swapped on advance.
//   - AVI sync: frame index = position * fps (no timestamps).
//   - OGG/Theora playback advances by logical packet order and granule-derived
//     frame indices; Vorbis tracks in the same OGG container are handed off to
//     the audio runtime when available.
//   - Caller must call Update(dt) each frame to advance playback.
//
// Links: rt_videoplayer.h, rt_avi.h, rt_pixels.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_videoplayer.h"
#include "../audio/rt_ogg.h"
#include "rt_avi.h"
#include "rt_canvas3d_internal.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_theora.h"
#include "rt_ycbcr.h"
#ifdef VIPER_ENABLE_AUDIO
#include "../audio/rt_audio.h"
#endif

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/types.h>
#endif

#include "rt_trap.h"
extern const char *rt_string_cstr(rt_string str);
extern void *rt_pixels_new(int64_t width, int64_t height);
extern int64_t rt_pixels_width(void *pixels);
extern int64_t rt_pixels_height(void *pixels);
extern void *rt_jpeg_decode_buffer(const uint8_t *data, size_t len);

/* Internal pixel struct for direct buffer copy */
typedef struct {
    int64_t width, height;
    uint32_t *data;
} px_view;

/// @brief Compute the byte count for a whole-frame RGBA pixel copy.
/// @details Verifies that both pixel views are non-null, have positive matching dimensions,
///          and that the width * height * sizeof(uint32_t) calculation fits in size_t before
///          callers pass the count to memcpy.
/// @param dst Destination pixel view.
/// @param src Source pixel view.
/// @param bytes_out Receives the validated byte count on success.
/// @return 1 when the views are compatible and the byte count is safe; otherwise 0.
static int video_pixel_copy_bytes(const px_view *dst, const px_view *src, size_t *bytes_out) {
    uint64_t pixels;
    if (!dst || !src || !bytes_out || !dst->data || !src->data)
        return 0;
    if (dst->width <= 0 || dst->height <= 0 || dst->width != src->width ||
        dst->height != src->height)
        return 0;
    if ((uint64_t)dst->width > UINT64_MAX / (uint64_t)dst->height)
        return 0;
    pixels = (uint64_t)dst->width * (uint64_t)dst->height;
    if (pixels > (uint64_t)SIZE_MAX / sizeof(uint32_t))
        return 0;
    *bytes_out = (size_t)pixels * sizeof(uint32_t);
    return 1;
}

/// @brief Build a packed little-endian FOURCC tag from four ASCII bytes.
/// @param a First FOURCC byte.
/// @param b Second FOURCC byte.
/// @param c Third FOURCC byte.
/// @param d Fourth FOURCC byte.
/// @return Packed 32-bit FOURCC value.
static uint32_t video_fourcc(char a, char b, char c, char d) {
    return (uint32_t)(uint8_t)a | ((uint32_t)(uint8_t)b << 8) |
           ((uint32_t)(uint8_t)c << 16) | ((uint32_t)(uint8_t)d << 24);
}

/// @brief Whether an AVI video stream codec can be decoded by this player.
/// @details The AVI path currently decodes MJPEG frames through the runtime
///          JPEG decoder. Unsupported codecs are rejected at open time so
///          playback does not silently produce stale or blank frames.
/// @param fourcc AVI stream codec FOURCC.
/// @return 1 for supported MJPEG aliases, otherwise 0.
static int videoplayer_avi_codec_supported(uint32_t fourcc) {
    return fourcc == video_fourcc('M', 'J', 'P', 'G') ||
           fourcc == video_fourcc('m', 'j', 'p', 'g') ||
           fourcc == video_fourcc('J', 'P', 'E', 'G') ||
           fourcc == video_fourcc('j', 'p', 'e', 'g');
}

/// @brief Copy a decoded Pixels object into the player's stable display buffer.
/// @details Verifies dimensions and byte-count overflow before copying. The
///          decoded object is not retained or released by this helper.
/// @param frame_display Destination Pixels object owned by the video player.
/// @param decoded Source Pixels object returned by the frame decoder.
/// @return 1 when the copy succeeded, 0 for NULL/mismatched/overflowing views.
static int videoplayer_copy_decoded_frame(void *frame_display, void *decoded) {
    px_view *dst = (px_view *)frame_display;
    px_view *src = (px_view *)decoded;
    size_t copy_bytes = 0;
    if (!video_pixel_copy_bytes(dst, src, &copy_bytes))
        return 0;
    memcpy(dst->data, src->data, copy_bytes);
    return 1;
}

// Keep video file loading large-file safe on platforms where long is 32-bit.
#if defined(_WIN32)
#define video_fseek(fp, off, whence) _fseeki64((fp), (off), (whence))
#define video_ftell(fp) _ftelli64((fp))
#else
#define video_fseek(fp, off, whence) fseeko((fp), (off_t)(off), (whence))
#define video_ftell(fp) ftello((fp))
#endif

/*==========================================================================
 * Standard JPEG DHT tables (Annex K of ITU-T T.81)
 * MJPEG frames in AVI often omit these; we inject them before SOS.
 *=========================================================================*/

static const uint8_t std_dht[] = {
    0xFF,
    0xC4, /* DHT marker */
    0x01,
    0xA2, /* length = 418 bytes (2 DC + 2 AC tables) */
    /* DC luminance (table 0, class 0) */
    0x00,
    0x00,
    0x01,
    0x05,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0A,
    0x0B,
    /* DC chrominance (table 1, class 0) */
    0x01,
    0x00,
    0x03,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0A,
    0x0B,
    /* AC luminance (table 0, class 1) */
    0x10,
    0x00,
    0x02,
    0x01,
    0x03,
    0x03,
    0x02,
    0x04,
    0x03,
    0x05,
    0x05,
    0x04,
    0x04,
    0x00,
    0x00,
    0x01,
    0x7D,
    0x01,
    0x02,
    0x03,
    0x00,
    0x04,
    0x11,
    0x05,
    0x12,
    0x21,
    0x31,
    0x41,
    0x06,
    0x13,
    0x51,
    0x61,
    0x07,
    0x22,
    0x71,
    0x14,
    0x32,
    0x81,
    0x91,
    0xA1,
    0x08,
    0x23,
    0x42,
    0xB1,
    0xC1,
    0x15,
    0x52,
    0xD1,
    0xF0,
    0x24,
    0x33,
    0x62,
    0x72,
    0x82,
    0x09,
    0x0A,
    0x16,
    0x17,
    0x18,
    0x19,
    0x1A,
    0x25,
    0x26,
    0x27,
    0x28,
    0x29,
    0x2A,
    0x34,
    0x35,
    0x36,
    0x37,
    0x38,
    0x39,
    0x3A,
    0x43,
    0x44,
    0x45,
    0x46,
    0x47,
    0x48,
    0x49,
    0x4A,
    0x53,
    0x54,
    0x55,
    0x56,
    0x57,
    0x58,
    0x59,
    0x5A,
    0x63,
    0x64,
    0x65,
    0x66,
    0x67,
    0x68,
    0x69,
    0x6A,
    0x73,
    0x74,
    0x75,
    0x76,
    0x77,
    0x78,
    0x79,
    0x7A,
    0x83,
    0x84,
    0x85,
    0x86,
    0x87,
    0x88,
    0x89,
    0x8A,
    0x92,
    0x93,
    0x94,
    0x95,
    0x96,
    0x97,
    0x98,
    0x99,
    0x9A,
    0xA2,
    0xA3,
    0xA4,
    0xA5,
    0xA6,
    0xA7,
    0xA8,
    0xA9,
    0xAA,
    0xB2,
    0xB3,
    0xB4,
    0xB5,
    0xB6,
    0xB7,
    0xB8,
    0xB9,
    0xBA,
    0xC2,
    0xC3,
    0xC4,
    0xC5,
    0xC6,
    0xC7,
    0xC8,
    0xC9,
    0xCA,
    0xD2,
    0xD3,
    0xD4,
    0xD5,
    0xD6,
    0xD7,
    0xD8,
    0xD9,
    0xDA,
    0xE1,
    0xE2,
    0xE3,
    0xE4,
    0xE5,
    0xE6,
    0xE7,
    0xE8,
    0xE9,
    0xEA,
    0xF1,
    0xF2,
    0xF3,
    0xF4,
    0xF5,
    0xF6,
    0xF7,
    0xF8,
    0xF9,
    0xFA,
    /* AC chrominance (table 1, class 1) */
    0x11,
    0x00,
    0x02,
    0x01,
    0x02,
    0x04,
    0x04,
    0x03,
    0x04,
    0x07,
    0x05,
    0x04,
    0x04,
    0x00,
    0x01,
    0x02,
    0x77,
    0x00,
    0x01,
    0x02,
    0x03,
    0x11,
    0x04,
    0x05,
    0x21,
    0x31,
    0x06,
    0x12,
    0x41,
    0x51,
    0x07,
    0x61,
    0x71,
    0x13,
    0x22,
    0x32,
    0x81,
    0x08,
    0x14,
    0x42,
    0x91,
    0xA1,
    0xB1,
    0xC1,
    0x09,
    0x23,
    0x33,
    0x52,
    0xF0,
    0x15,
    0x62,
    0x72,
    0xD1,
    0x0A,
    0x16,
    0x24,
    0x34,
    0xE1,
    0x25,
    0xF1,
    0x17,
    0x18,
    0x19,
    0x1A,
    0x26,
    0x27,
    0x28,
    0x29,
    0x2A,
    0x35,
    0x36,
    0x37,
    0x38,
    0x39,
    0x3A,
    0x43,
    0x44,
    0x45,
    0x46,
    0x47,
    0x48,
    0x49,
    0x4A,
    0x53,
    0x54,
    0x55,
    0x56,
    0x57,
    0x58,
    0x59,
    0x5A,
    0x63,
    0x64,
    0x65,
    0x66,
    0x67,
    0x68,
    0x69,
    0x6A,
    0x73,
    0x74,
    0x75,
    0x76,
    0x77,
    0x78,
    0x79,
    0x7A,
    0x82,
    0x83,
    0x84,
    0x85,
    0x86,
    0x87,
    0x88,
    0x89,
    0x8A,
    0x92,
    0x93,
    0x94,
    0x95,
    0x96,
    0x97,
    0x98,
    0x99,
    0x9A,
    0xA2,
    0xA3,
    0xA4,
    0xA5,
    0xA6,
    0xA7,
    0xA8,
    0xA9,
    0xAA,
    0xB2,
    0xB3,
    0xB4,
    0xB5,
    0xB6,
    0xB7,
    0xB8,
    0xB9,
    0xBA,
    0xC2,
    0xC3,
    0xC4,
    0xC5,
    0xC6,
    0xC7,
    0xC8,
    0xC9,
    0xCA,
    0xD2,
    0xD3,
    0xD4,
    0xD5,
    0xD6,
    0xD7,
    0xD8,
    0xD9,
    0xDA,
    0xE2,
    0xE3,
    0xE4,
    0xE5,
    0xE6,
    0xE7,
    0xE8,
    0xE9,
    0xEA,
    0xF2,
    0xF3,
    0xF4,
    0xF5,
    0xF6,
    0xF7,
    0xF8,
    0xF9,
    0xFA,
};

/// @brief Whether a JPEG marker has no segment-length payload.
/// @param marker JPEG marker byte after the 0xFF prefix.
/// @return 1 for stand-alone markers, 0 for length-prefixed segment markers.
static int jpeg_marker_has_no_payload(uint8_t marker) {
    return marker == 0x01 || marker == 0xD8 || marker == 0xD9 ||
           (marker >= 0xD0 && marker <= 0xD7);
}

/// @brief Scan a JPEG/MJPEG frame for DHT and SOS markers by walking marker segments.
/// @details Stops at SOS because entropy-coded data after that point can contain
///          byte-stuffed marker-like values. This avoids the old fixed-window
///          scan while still allowing DHT injection immediately before SOS.
/// @param data JPEG frame bytes.
/// @param size Byte length of @p data.
/// @param out_has_dht Receives whether a DHT marker was present before SOS.
/// @param out_sos_pos Receives the byte offset of SOS, or 0 when not found.
/// @return 1 when the marker structure scanned cleanly, 0 on malformed segment lengths.
static int mjpeg_scan_markers(const uint8_t *data,
                              uint32_t size,
                              int *out_has_dht,
                              uint32_t *out_sos_pos) {
    size_t pos = 0;
    if (!data || !out_has_dht || !out_sos_pos)
        return 0;
    *out_has_dht = 0;
    *out_sos_pos = 0;
    while (pos + 1u < (size_t)size) {
        while (pos < (size_t)size && data[pos] != 0xFFu)
            pos++;
        if (pos + 1u >= (size_t)size)
            return 1;
        size_t marker_pos = pos;
        pos++;
        while (pos < (size_t)size && data[pos] == 0xFFu)
            pos++;
        if (pos >= (size_t)size)
            return 0;
        uint8_t marker = data[pos++];
        if (marker == 0x00u)
            continue;
        if (marker == 0xC4u)
            *out_has_dht = 1;
        if (marker == 0xDAu) {
            *out_sos_pos = (uint32_t)marker_pos;
            return 1;
        }
        if (jpeg_marker_has_no_payload(marker))
            continue;
        if (pos + 2u > (size_t)size)
            return 0;
        uint16_t seg_len = (uint16_t)(((uint16_t)data[pos] << 8) | (uint16_t)data[pos + 1u]);
        if (seg_len < 2u || (size_t)(seg_len - 2u) > (size_t)size - pos - 2u)
            return 0;
        pos += (size_t)seg_len;
    }
    return 1;
}

/// @brief Decode an MJPEG frame, injecting standard DHT if missing.
static void *decode_mjpeg_frame(const uint8_t *data, uint32_t size) {
    if (!data || size < 4)
        return NULL;

    int has_dht = 0;
    uint32_t sos_pos = 0;
    if (!mjpeg_scan_markers(data, size, &has_dht, &sos_pos))
        return NULL;

    if (has_dht)
        return rt_jpeg_decode_buffer(data, size);

    /* Find SOS marker (0xFFDA) — insert DHT just before it */
    if (sos_pos == 0)
        return rt_jpeg_decode_buffer(data, size); /* no SOS found, try anyway */

    /* Build new buffer: [header...SOS) + DHT + [SOS...end] */
    size_t dht_size = sizeof(std_dht);
    if ((size_t)size > SIZE_MAX - dht_size)
        return NULL;
    size_t new_size = size + dht_size;
    uint8_t *buf = (uint8_t *)malloc(new_size);
    if (!buf)
        return NULL;

    memcpy(buf, data, sos_pos);
    memcpy(buf + sos_pos, std_dht, dht_size);
    memcpy(buf + sos_pos + dht_size, data + sos_pos, size - sos_pos);

    void *result = rt_jpeg_decode_buffer(buf, new_size);
    free(buf);
    return result;
}

typedef struct {
    void *vptr;
    /* File data */
    uint8_t *file_data;
    size_t file_len;
    /* Container */
    int32_t container_type; /* 0=AVI, 1=OGG */
    avi_context_t avi;
    /* OGG/Theora state */
    ogg_reader_t *ogg_reader;
    theora_decoder_t theora;
    uint32_t theora_serial; /* OGG stream serial for Theora */
#ifdef VIPER_ENABLE_AUDIO
    void *audio_track; /* rt_music */
    int8_t audio_started;
    int8_t audio_paused;
#endif
    /* Video info */
    int32_t width, height;
    double fps;
    double duration;
    int32_t total_frames;
    /* Playback state */
    int8_t playing;
    double position;
    int32_t current_frame;
    double volume;
    /* Frame buffers */
    void *frame_display; /* Pixels — currently displayed */
    void *frame_decode;  /* Pixels — scratch for decode (may be reallocated) */
} rt_videoplayer;

static rt_videoplayer *videoplayer_checked(void *obj) {
    if (!obj || !rt_obj_is_instance(obj, RT_VIDEOPLAYER_CLASS_ID, sizeof(rt_videoplayer)))
        return NULL;
    return (rt_videoplayer *)obj;
}

static void release_owned_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Copy the current display pixels into a malloc-owned rollback buffer.
///
/// @details Seeking through a forward-only stream can decode intermediate
///   frames before discovering that the requested target frame is unavailable.
///   This helper snapshots the visible `frame_display` buffer before seeking so
///   failure recovery can restore the exact pixels the caller was seeing. The
///   returned buffer owns @p out_count uint32_t pixels and must be freed with
///   @c free by the caller.
///
/// @param vp        Video player whose display buffer should be copied.
/// @param out_count Receives the number of uint32_t pixels in the snapshot.
/// @return A malloc-owned pixel copy, or NULL if no safe snapshot is possible.
static uint32_t *videoplayer_snapshot_display(const rt_videoplayer *vp, size_t *out_count) {
    if (out_count)
        *out_count = 0;
    if (!vp || !vp->frame_display || !out_count)
        return NULL;
    const px_view *view = (const px_view *)vp->frame_display;
    if (!view->data || view->width <= 0 || view->height <= 0)
        return NULL;
    if ((size_t)view->width > SIZE_MAX / (size_t)view->height)
        return NULL;
    size_t count = (size_t)view->width * (size_t)view->height;
    if (count > SIZE_MAX / sizeof(uint32_t))
        return NULL;
    uint32_t *copy = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!copy)
        return NULL;
    memcpy(copy, view->data, count * sizeof(uint32_t));
    *out_count = count;
    return copy;
}

/// @brief Restore a display-buffer snapshot produced by @c videoplayer_snapshot_display.
///
/// @details The restore is intentionally conservative: dimensions are rechecked
///   against the current display buffer and no write occurs if the target buffer
///   has been reallocated to a different size. The snapshot remains caller-owned
///   and is not freed by this helper.
///
/// @param vp    Video player whose display buffer should be restored.
/// @param data  Snapshot pixel buffer, or NULL for a no-op.
/// @param count Number of uint32_t pixels in @p data.
static void videoplayer_restore_display(rt_videoplayer *vp, const uint32_t *data, size_t count) {
    if (!vp || !vp->frame_display || !data || count == 0)
        return;
    px_view *view = (px_view *)vp->frame_display;
    if (!view->data || view->width <= 0 || view->height <= 0)
        return;
    if ((size_t)view->width > SIZE_MAX / (size_t)view->height)
        return;
    size_t expected = (size_t)view->width * (size_t)view->height;
    if (expected != count)
        return;
    memcpy(view->data, data, count * sizeof(uint32_t));
}

/// @brief Decode one AVI/MJPEG frame into the stable display buffer.
/// @details Fetches the requested frame from the AVI index, decodes it through the MJPEG/JPEG path,
///          copies it into @c frame_display after dimension validation, and releases the temporary
///          decoded Pixels handle before returning.
/// @param vp Video player owning the parsed AVI context and display buffer.
/// @param frame_index Zero-based target frame index.
/// @return 1 when the frame was decoded and copied, 0 on missing data or decode failure.
static int videoplayer_decode_avi_frame(rt_videoplayer *vp, int32_t frame_index) {
    if (!vp || vp->container_type != 0 || frame_index < 0 || frame_index >= vp->total_frames)
        return 0;
    uint32_t frame_size = 0;
    const uint8_t *frame_data = avi_get_video_frame(&vp->avi, frame_index, &frame_size);
    void *decoded = frame_data ? decode_mjpeg_frame(frame_data, frame_size) : NULL;
    int copied = videoplayer_copy_decoded_frame(vp->frame_display, decoded);
    release_owned_ref(&decoded);
    if (!copied)
        return 0;
    vp->current_frame = frame_index;
    return 1;
}

static double videoplayer_clamp_seconds(const rt_videoplayer *vp, double seconds) {
    if (!isfinite(seconds)) {
        if (seconds > 0.0 && vp && isfinite(vp->duration) && vp->duration >= 0.0)
            return vp->duration;
        return 0.0;
    }
    if (seconds < 0.0)
        return 0.0;
    if (vp && isfinite(vp->duration) && vp->duration >= 0.0 && seconds > vp->duration)
        return vp->duration;
    return seconds;
}

static int32_t videoplayer_frame_index_at(const rt_videoplayer *vp, double seconds) {
    if (!vp || vp->total_frames <= 0 || !isfinite(seconds) || !isfinite(vp->fps) || vp->fps <= 0.0)
        return -1;
    long double frame = (long double)seconds * (long double)vp->fps;
    if (frame < 0.0L)
        return -1;
    if (frame >= (long double)INT32_MAX)
        return INT32_MAX;
    return (int32_t)frame;
}

/// @brief Decode a Theora granulepos into a sequential frame index.
///
/// Theora packs `(intra_frame_count, interframe_count)` into the
/// granule using `keyframe_granule_shift` bits. The total frame
/// number is the sum: keyframe count plus inter-frame distance.
/// Returns -1 on overflow / invalid shift.
static int32_t theora_granule_to_frame_index(const theora_decoder_t *dec, int64_t granule_pos) {
    if (!dec || granule_pos < 0)
        return -1;
    uint32_t shift = dec->keyframe_granule_shift;
    if (shift >= 63)
        return -1;
    int64_t iframe = (shift == 0) ? granule_pos : (granule_pos >> shift);
    int64_t pframe = (shift == 0) ? 0 : (granule_pos - (iframe << shift));
    int64_t frame = iframe + pframe;
    if (frame < 0 || frame > INT32_MAX)
        return -1;
    return (int32_t)frame;
}

/// @brief Convert a Theora YCbCr frame to the player's RGBA display buffer.
///
/// Crops the decoded picture using `pic_x/pic_y/pic_width/pic_height`
/// (Theora encodes a slightly larger frame and stores a crop rect),
/// then runs the appropriate YCbCr-to-RGBA color conversion for the stream's
/// 4:2:0, 4:2:2, or 4:4:4 pixel format into `frame_display`.
/// @return 1 on success, 0 if the destination buffer is missing or sizes mismatch.
static int copy_theora_frame_to_display(rt_videoplayer *vp,
                                        const uint8_t *y,
                                        const uint8_t *cb,
                                        const uint8_t *cr) {
    if (!vp || !vp->frame_display || !y || !cb || !cr)
        return 0;

    px_view *dst = (px_view *)vp->frame_display;
    if (!dst->data)
        return 0;

    const theora_decoder_t *dec = &vp->theora;
    int32_t pic_w = (int32_t)dec->pic_width;
    int32_t pic_h = (int32_t)dec->pic_height;
    if (pic_w <= 0 || pic_h <= 0)
        return 0;
    if (dst->width != pic_w || dst->height != pic_h)
        return 0;

    int32_t chroma_x_shift = dec->pixel_format == 3 ? 0 : 1;
    int32_t chroma_y_shift = dec->pixel_format == 0 ? 1 : 0;
    const uint8_t *y_plane = y + dec->pic_y * dec->y_stride + dec->pic_x;
    const uint8_t *cb_plane =
        cb + (dec->pic_y >> chroma_y_shift) * dec->c_stride + (dec->pic_x >> chroma_x_shift);
    const uint8_t *cr_plane =
        cr + (dec->pic_y >> chroma_y_shift) * dec->c_stride + (dec->pic_x >> chroma_x_shift);
    switch (dec->pixel_format) {
        case 0:
            ycbcr420_to_rgba(
                y_plane, cb_plane, cr_plane, pic_w, pic_h, dec->y_stride, dec->c_stride, dst->data);
            break;
        case 2:
            ycbcr422_to_rgba(
                y_plane, cb_plane, cr_plane, pic_w, pic_h, dec->y_stride, dec->c_stride, dst->data);
            break;
        case 3:
            ycbcr444_to_rgba(
                y_plane, cb_plane, cr_plane, pic_w, pic_h, dec->y_stride, dec->c_stride, dst->data);
            break;
        default:
            return 0;
    }
    return 1;
}

#ifdef VIPER_ENABLE_AUDIO
// ---------------------------------------------------------------------------
// Audio-track helpers — the videoplayer mixes the optional
// embedded audio stream (Vorbis if present in the Ogg container)
// through the regular `rt_music_*` audio API. These wrappers
// guard against missing audio tracks (silent video files).
// ---------------------------------------------------------------------------

/// @brief Push the player's volume (0.0-1.0) into the underlying audio track (0-100 scale).
static void videoplayer_set_audio_volume(rt_videoplayer *vp) {
    if (!vp || !vp->audio_track)
        return;
    int64_t vol = (int64_t)(vp->volume * 100.0 + 0.5);
    rt_music_set_volume(vp->audio_track, vol);
}

/// @brief Begin or resume audio playback in sync with the current video position.
static void videoplayer_start_audio(rt_videoplayer *vp) {
    if (!vp || !vp->audio_track)
        return;
    videoplayer_set_audio_volume(vp);
    if (vp->audio_paused && vp->audio_started) {
        rt_music_resume(vp->audio_track);
    } else {
        rt_music_seek(vp->audio_track, (int64_t)(vp->position * 1000.0 + 0.5));
        rt_music_play(vp->audio_track, 0);
        vp->audio_started = 1;
    }
    vp->audio_paused = 0;
}

/// @brief Pause the audio track if it's currently playing.
static void videoplayer_pause_audio(rt_videoplayer *vp) {
    if (!vp || !vp->audio_track || !vp->audio_started || vp->audio_paused)
        return;
    rt_music_pause(vp->audio_track);
    vp->audio_paused = 1;
}

/// @brief Stop the audio track and reset the started/paused flags.
static void videoplayer_stop_audio(rt_videoplayer *vp) {
    if (!vp || !vp->audio_track)
        return;
    rt_music_stop(vp->audio_track);
    vp->audio_started = 0;
    vp->audio_paused = 0;
}

/// @brief Seek the audio track to match the player's current video position.
static void videoplayer_seek_audio(rt_videoplayer *vp) {
    if (!vp || !vp->audio_track || !vp->audio_started)
        return;
    rt_music_seek(vp->audio_track, (int64_t)(vp->position * 1000.0 + 0.5));
}
#endif

/// @brief Scan the entire Ogg container to find Theora headers and total frame count.
///
/// Pre-pass that runs on file open: walks every page, identifies
/// the Theora stream's serial number, decodes its three header
/// packets, and records the granule of the last data packet
/// (which gives us the total frame count + duration).
/// @return 1 on success, 0 if no Theora stream is found.
static int ogv_scan_stream(rt_videoplayer *vp) {
    ogg_reader_t *reader = ogg_reader_open_mem(vp->file_data, vp->file_len);
    if (!reader)
        return 0;

    theora_decoder_t dec;
    theora_decoder_init(&dec);

    int found_theora = 0;
    int64_t last_granule = -1;
    const uint8_t *packet = NULL;
    size_t packet_len = 0;
    ogg_packet_info_t info;

    while (ogg_reader_next_packet_ex(reader, &packet, &packet_len, &info)) {
        if (!found_theora) {
            if (!theora_is_header_packet(packet, packet_len))
                continue;
            vp->theora_serial = info.serial_number;
            found_theora = 1;
        }
        if (info.serial_number != vp->theora_serial)
            continue;

        int rc = theora_decode_header(&dec, packet, packet_len);
        if (rc < 0) {
            ogg_reader_free(reader);
            theora_decoder_free(&dec);
            return 0;
        }
        if (rc == 1 && info.granule_position >= 0)
            last_granule = info.granule_position;
    }

    ogg_reader_free(reader);
    if (!found_theora || !dec.headers_complete || dec.pic_width == 0 || dec.pic_height == 0) {
        theora_decoder_free(&dec);
        return 0;
    }

    vp->width = (int32_t)dec.pic_width;
    vp->height = (int32_t)dec.pic_height;
    vp->fps =
        (dec.fps_num > 0 && dec.fps_den > 0) ? ((double)dec.fps_num / (double)dec.fps_den) : 0.0;
    if (vp->fps <= 0.0)
        vp->fps = 1.0;
    vp->total_frames =
        (last_granule >= 0) ? (theora_granule_to_frame_index(&dec, last_granule) + 1) : 0;
    if (vp->total_frames < 0)
        vp->total_frames = 0;
    vp->duration = (vp->total_frames > 0) ? ((double)vp->total_frames / vp->fps) : 0.0;

    theora_decoder_free(&dec);
    return 1;
}

/// @brief Reopen the Ogg reader at the beginning of the file in preparation for playback.
///
/// Run after `ogv_scan_stream` so the reader is ready to deliver
/// data packets in order. Required because we don't keep the
/// scan-pass reader alive (it would just be a wasteful memory hold).
static int ogv_prepare_playback(rt_videoplayer *vp) {
    if (!vp)
        return 0;

    if (!vp->ogg_reader) {
        vp->ogg_reader = ogg_reader_open_mem(vp->file_data, vp->file_len);
        if (!vp->ogg_reader)
            return 0;
    } else {
        ogg_reader_rewind(vp->ogg_reader);
    }

    theora_decoder_free(&vp->theora);
    theora_decoder_init(&vp->theora);

    const uint8_t *packet = NULL;
    size_t packet_len = 0;
    ogg_packet_info_t info;
    while (ogg_reader_next_packet_ex(vp->ogg_reader, &packet, &packet_len, &info)) {
        if (info.serial_number != vp->theora_serial)
            continue;
        int rc = theora_decode_header(&vp->theora, packet, packet_len);
        if (rc < 0)
            return 0;
        if (vp->theora.headers_complete)
            return 1;
    }

    return 0;
}

/// @brief Pull the next Theora data packet, decode it, and copy the result to `frame_display`.
/// @return 1 on a fresh frame, 0 at EOF or decode failure.
static int ogv_decode_next_frame(rt_videoplayer *vp) {
    if (!vp || !vp->ogg_reader)
        return 0;

    const uint8_t *packet = NULL;
    size_t packet_len = 0;
    ogg_packet_info_t info;
    while (ogg_reader_next_packet_ex(vp->ogg_reader, &packet, &packet_len, &info)) {
        if (info.serial_number != vp->theora_serial)
            continue;

        const uint8_t *y = NULL;
        const uint8_t *cb = NULL;
        const uint8_t *cr = NULL;
        int rc = theora_decode_frame(&vp->theora, packet, packet_len, &y, &cb, &cr);
        if (rc != 0)
            continue;
        if (!copy_theora_frame_to_display(vp, y, cb, cr))
            return 0;

        int32_t frame_index = theora_granule_to_frame_index(&vp->theora, info.granule_position);
        if (frame_index < 0)
            frame_index = vp->current_frame + 1;
        vp->current_frame = frame_index;
        return 1;
    }

    return 0;
}

/// @brief Fast-decode forward until reaching `target_frame` (used for seeks).
///
/// Theora is forward-only — to seek to frame N you must decode
/// every frame from the previous keyframe. This loop drives that
/// catch-up decode without rendering intermediate frames.
static int ogv_decode_until_frame(rt_videoplayer *vp, int32_t target_frame) {
    if (!vp)
        return 0;
    if (target_frame < 0)
        return 1;
    while (vp->current_frame < target_frame) {
        if (!ogv_decode_next_frame(vp))
            return 0;
    }
    return 1;
}

/// @brief GC finalizer for the videoplayer — releases the ogg reader, decoder, frame buffers, audio
/// track.
static void videoplayer_finalizer(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    if (!vp)
        return;
    if (vp->container_type == 0)
        avi_free(&vp->avi);
#ifdef VIPER_ENABLE_AUDIO
    if (vp->audio_track)
        rt_music_destroy(vp->audio_track);
#endif
    if (vp->ogg_reader)
        ogg_reader_free(vp->ogg_reader);
    theora_decoder_free(&vp->theora);
    release_owned_ref(&vp->frame_display);
    release_owned_ref(&vp->frame_decode);
    free(vp->file_data);
    vp->file_data = NULL;
}

// ===========================================================================
// VideoPlayer public API
//
// Each player wraps an in-memory copy of the source file (so seeking is
// cheap), an ogg+theora decoder pair, optional audio track, and a pair
// of pixel buffers (one displayed, one decode-scratch). The runtime
// drives playback by calling `rt_videoplayer_update(dt)` once per frame
// — the player advances time, decodes new frames as needed, and exposes
// the currently visible frame via `rt_videoplayer_get_frame`.
// ===========================================================================

/// @brief Open a video file from disk and prepare it for playback.
///
/// Reads the whole file into memory (so seek is cheap), runs
/// `ogv_scan_stream` to find the Theora track + duration, then
/// initialises the decoder. Returns NULL on any failure (file
/// not found, not an Ogg/Theora file, OOM).
void *rt_videoplayer_open(rt_string path) {
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return NULL;

    /* Read entire file into memory */
    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;

    if (video_fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    int64_t file_len = video_ftell(f);
    if (video_fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    if (file_len <= 12 || file_len > INT64_C(512) * 1024 * 1024) { /* 512 MB limit */
        fclose(f);
        return NULL;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)file_len);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, (size_t)file_len, f) != (size_t)file_len) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    /* Detect container format by magic bytes */
    int is_ogg =
        (file_len >= 4 && data[0] == 'O' && data[1] == 'g' && data[2] == 'g' && data[3] == 'S');
    int is_avi =
        (file_len >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
         data[8] == 'A' && data[9] == 'V' && data[10] == 'I' && data[11] == ' ');

    if (!is_avi && !is_ogg) {
        free(data);
        return NULL;
    }

    /* Create VideoPlayer object */
    rt_videoplayer *vp =
        (rt_videoplayer *)rt_obj_new_i64(RT_VIDEOPLAYER_CLASS_ID, (int64_t)sizeof(rt_videoplayer));
    if (!vp) {
        free(data);
        return NULL;
    }
    {
        rt_videoplayer zero = {0};
        *vp = zero;
    }
    vp->file_data = data;
    vp->file_len = (size_t)file_len;
    vp->playing = 0;
    vp->position = 0.0;
    vp->current_frame = -1;
    vp->volume = 1.0;
    rt_obj_set_finalizer(vp, videoplayer_finalizer);

    if (is_ogg) {
        vp->container_type = 1;
        if (!ogv_scan_stream(vp)) {
            if (rt_obj_release_check0(vp))
                rt_obj_free(vp);
            return NULL;
        }
        vp->frame_display = rt_pixels_new(vp->width, vp->height);
        vp->frame_decode = NULL;
        if (!vp->frame_display || !ogv_prepare_playback(vp)) {
            if (rt_obj_release_check0(vp))
                rt_obj_free(vp);
            return NULL;
        }
        if (vp->total_frames > 0 && !ogv_decode_until_frame(vp, 0)) {
            if (rt_obj_release_check0(vp))
                rt_obj_free(vp);
            return NULL;
        }
#ifdef VIPER_ENABLE_AUDIO
        vp->audio_track = rt_music_load(path);
        videoplayer_set_audio_volume(vp);
#endif
        return vp;
    }

    vp->container_type = 0;
    if (avi_parse(&vp->avi, data, (size_t)file_len) != 0) {
        if (rt_obj_release_check0(vp))
            rt_obj_free(vp);
        return NULL;
    }
    vp->width = vp->avi.video.width;
    vp->height = vp->avi.video.height;
    vp->fps = vp->avi.video.fps;
    vp->duration = vp->avi.video.duration;
    vp->total_frames = vp->avi.video_frame_count;
    if (!videoplayer_avi_codec_supported(vp->avi.video.fourcc) || vp->width <= 0 ||
        vp->height <= 0 || vp->total_frames <= 0 || !isfinite(vp->fps) || vp->fps <= 0.0 ||
        !isfinite(vp->duration) || vp->duration < 0.0) {
        if (rt_obj_release_check0(vp))
            rt_obj_free(vp);
        return NULL;
    }

    /* Stable frame buffer — pre-allocated, content copied in each frame.
     * This prevents GC from collecting the display Pixels mid-frame. */
    vp->frame_display = rt_pixels_new(vp->width, vp->height);
    vp->frame_decode = NULL;
    if (!vp->frame_display) {
        if (rt_obj_release_check0(vp))
            rt_obj_free(vp);
        return NULL;
    }

    /* Decode first frame immediately so get_Frame works before Play */
    if (!videoplayer_decode_avi_frame(vp, 0)) {
        if (rt_obj_release_check0(vp))
            rt_obj_free(vp);
        return NULL;
    }

    return vp;
}

/// @brief Begin (or resume) playback. Subsequent `update` calls advance time.
void rt_videoplayer_play(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    if (!vp)
        return;
    vp->playing = 1;
#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1)
        videoplayer_start_audio(vp);
#endif
}

/// @brief Pause playback at the current frame. `update` becomes a no-op until `play`.
void rt_videoplayer_pause(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    if (!vp)
        return;
    vp->playing = 0;
#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1)
        videoplayer_pause_audio(vp);
#endif
}

/// @brief Stop playback and rewind to frame 0. The currently displayed frame remains visible.
void rt_videoplayer_stop(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    if (!vp)
        return;
    vp->playing = 0;
    vp->position = 0.0;
    vp->current_frame = -1;
    if (vp->container_type == 1)
        ogv_prepare_playback(vp);
#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1)
        videoplayer_stop_audio(vp);
#endif
}

/// @brief Jump to the frame containing time `seconds`.
///
/// Per Theora's forward-only constraint, seeking backward decodes
/// from the previous keyframe; seeking forward into a different
/// keyframe also decodes from that keyframe forward. The audio
/// track is reseeked to match. If the target frame cannot be decoded,
/// the previously displayed frame and playback position are preserved.
void rt_videoplayer_seek(void *obj, double seconds) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    if (!vp)
        return;
    seconds = videoplayer_clamp_seconds(vp, seconds);
    size_t snapshot_count = 0;
    uint32_t *snapshot = videoplayer_snapshot_display(vp, &snapshot_count);
    if (vp->container_type == 1) {
        double old_position = vp->position;
        int32_t old_frame = vp->current_frame;
        int32_t target = videoplayer_frame_index_at(vp, seconds);
        if (target >= vp->total_frames)
            target = vp->total_frames > 0 ? vp->total_frames - 1 : -1;
        if (target < 0) {
            free(snapshot);
            return;
        }
        vp->current_frame = -1;
        if (!(ogv_prepare_playback(vp) && ogv_decode_until_frame(vp, target))) {
            videoplayer_restore_display(vp, snapshot, snapshot_count);
            vp->position = old_position;
            vp->current_frame = old_frame;
            if (old_frame >= 0) {
                vp->current_frame = -1;
                if (!ogv_prepare_playback(vp) || !ogv_decode_until_frame(vp, old_frame))
                    vp->current_frame = old_frame;
            }
            free(snapshot);
            return;
        }
        vp->position = seconds;
#ifdef VIPER_ENABLE_AUDIO
        videoplayer_seek_audio(vp);
#endif
    } else {
        double old_position = vp->position;
        int32_t old_frame = vp->current_frame;
        int32_t target = videoplayer_frame_index_at(vp, seconds);
        if (target >= vp->total_frames)
            target = vp->total_frames > 0 ? vp->total_frames - 1 : -1;
        if (target >= 0 && videoplayer_decode_avi_frame(vp, target)) {
            vp->position = seconds;
        } else {
            videoplayer_restore_display(vp, snapshot, snapshot_count);
            vp->position = old_position;
            vp->current_frame = old_frame;
        }
    }
    free(snapshot);
}

/// @brief Advance playback by `dt` seconds, decoding new frames as their timestamps elapse.
///
/// Called once per game / UI frame. Skips frames if the host is
/// running below the video's framerate (drops decode time but
/// keeps audio in sync). At end-of-stream, sets `playing = 0`.
void rt_videoplayer_update(void *obj, double dt) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    if (!vp || !isfinite(dt) || dt <= 0.0)
        return;
    if (!vp->playing)
        return;

#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1 && vp->audio_track && vp->audio_started &&
        rt_music_is_playing(vp->audio_track)) {
        vp->position = (double)rt_music_get_position(vp->audio_track) / 1000.0;
    } else
#endif
    {
        vp->position = videoplayer_clamp_seconds(vp, vp->position + dt);
    }

    if (isfinite(vp->duration) && vp->duration > 0.0 && vp->position >= vp->duration) {
        int32_t final_frame = vp->total_frames > 0 ? vp->total_frames - 1 : -1;
        if (final_frame >= 0 && final_frame != vp->current_frame) {
            if (vp->container_type == 1)
                (void)ogv_decode_until_frame(vp, final_frame);
            else
                (void)videoplayer_decode_avi_frame(vp, final_frame);
        }
        vp->position = vp->duration;
        vp->playing = 0;
#ifdef VIPER_ENABLE_AUDIO
        if (vp->container_type == 1)
            videoplayer_stop_audio(vp);
#endif
        return;
    }

    /* Determine target frame */
    int32_t target = videoplayer_frame_index_at(vp, vp->position);
    if (target < 0 || target >= vp->total_frames) {
        vp->playing = 0;
        if (isfinite(vp->duration) && vp->duration >= 0.0)
            vp->position = vp->duration;
#ifdef VIPER_ENABLE_AUDIO
        if (vp->container_type == 1)
            videoplayer_stop_audio(vp);
#endif
        return;
    }

    if (vp->container_type == 1) {
        if (target != vp->current_frame) {
            if (!ogv_decode_until_frame(vp, target)) {
                vp->playing = 0;
                if (vp->duration > 0.0)
                    vp->position = vp->duration;
            }
        }
        return;
    }

    /* Only decode if frame changed */
    if (target != vp->current_frame) {
        if (!videoplayer_decode_avi_frame(vp, target)) {
            vp->playing = 0;
            return;
        }
    }
}

/// @brief Set the audio mix volume in [0.0, 1.0]. Clamped at the bounds.
void rt_videoplayer_set_volume(void *obj, double vol) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    if (!vp)
        return;
    if (!isfinite(vol))
        vol = 0.0;
    if (vol < 0.0)
        vol = 0.0;
    if (vol > 1.0)
        vol = 1.0;
    vp->volume = vol;
#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1)
        videoplayer_set_audio_volume(vp);
#endif
}

/// @brief Pixel width of the video frame (post-crop).
int64_t rt_videoplayer_get_width(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    return vp ? vp->width : 0;
}

/// @brief Pixel height of the video frame (post-crop).
int64_t rt_videoplayer_get_height(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    return vp ? vp->height : 0;
}

/// @brief Total duration of the video in seconds (computed at open time from `last_granule`).
double rt_videoplayer_get_duration(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    return vp ? vp->duration : 0.0;
}

/// @brief Current playback position in seconds.
double rt_videoplayer_get_position(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    if (!vp)
        return 0.0;
#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1 && vp->audio_track && vp->audio_started &&
        rt_music_is_playing(vp->audio_track)) {
        return (double)rt_music_get_position(vp->audio_track) / 1000.0;
    }
#endif
    return vp->position;
}

/// @brief 1 if currently playing, 0 if paused / stopped / at EOF.
int64_t rt_videoplayer_get_is_playing(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    return vp ? vp->playing : 0;
}

/// @brief Return the currently-displayed Pixels frame (NULL until first frame decodes).
/// The returned pointer is owned by the player — borrow only, don't free.
void *rt_videoplayer_get_frame(void *obj) {
    rt_videoplayer *vp = videoplayer_checked(obj);
    return vp ? vp->frame_display : NULL;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
