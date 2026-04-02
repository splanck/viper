//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_videoplayer.c
// Purpose: Video playback engine — loads AVI containers with MJPEG codec,
//   decodes video frames to Pixels, manages playback state and A/V sync.
//
// Key invariants:
//   - Frame decode uses rt_jpeg_decode_buffer (MJPEG = sequence of JPEGs).
//   - Double-buffered frames: display + decode, swapped on advance.
//   - AVI sync: frame index = position * fps (no timestamps).
//   - Caller must call Update(dt) each frame to advance playback.
//
// Links: rt_videoplayer.h, rt_avi.h, rt_pixels.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_videoplayer.h"
#include "rt_avi.h"
#include "rt_canvas3d_internal.h"
#include "rt_theora.h"
#include "rt_ycbcr.h"

/* OGG reader — declared in audio module, we use extern declarations
 * to avoid cross-module include path dependency. */
typedef struct ogg_reader_t ogg_reader_t;
extern ogg_reader_t *ogg_reader_open_mem(const uint8_t *data, size_t len);
extern void ogg_reader_free(ogg_reader_t *r);
extern int ogg_reader_next_packet(ogg_reader_t *r, const uint8_t **out_data,
                                   size_t *out_len);
extern void ogg_reader_rewind(ogg_reader_t *r);

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void rt_trap(const char *msg);
extern const char *rt_string_cstr(void *str);
extern void *rt_pixels_new(int64_t width, int64_t height);
extern int64_t rt_pixels_width(void *pixels);
extern int64_t rt_pixels_height(void *pixels);
extern void *rt_jpeg_decode_buffer(const uint8_t *data, size_t len);

/* Internal pixel struct for direct buffer copy */
typedef struct {
    int64_t width, height;
    uint32_t *data;
} px_view;

/*==========================================================================
 * Standard JPEG DHT tables (Annex K of ITU-T T.81)
 * MJPEG frames in AVI often omit these; we inject them before SOS.
 *=========================================================================*/

static const uint8_t std_dht[] = {
    0xFF, 0xC4, /* DHT marker */
    0x01, 0xA2, /* length = 418 bytes (2 DC + 2 AC tables) */
    /* DC luminance (table 0, class 0) */
    0x00,
    0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    /* DC chrominance (table 1, class 0) */
    0x01,
    0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    /* AC luminance (table 0, class 1) */
    0x10,
    0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
    0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D,
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
    0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
    0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
    /* AC chrominance (table 1, class 1) */
    0x11,
    0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
    0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77,
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
    0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
    0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
    0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA,
};

/// @brief Decode an MJPEG frame, injecting standard DHT if missing.
static void *decode_mjpeg_frame(const uint8_t *data, uint32_t size) {
    if (!data || size < 4)
        return NULL;

    /* Check if DHT marker (0xFFC4) is already present */
    int has_dht = 0;
    for (uint32_t i = 0; i + 1 < size && i < 1000; i++) {
        if (data[i] == 0xFF && data[i + 1] == 0xC4) {
            has_dht = 1;
            break;
        }
    }

    if (has_dht)
        return rt_jpeg_decode_buffer(data, size);

    /* Find SOS marker (0xFFDA) — insert DHT just before it */
    uint32_t sos_pos = 0;
    for (uint32_t i = 0; i + 1 < size; i++) {
        if (data[i] == 0xFF && data[i + 1] == 0xDA) {
            sos_pos = i;
            break;
        }
    }
    if (sos_pos == 0)
        return rt_jpeg_decode_buffer(data, size); /* no SOS found, try anyway */

    /* Build new buffer: [header...SOS) + DHT + [SOS...end] */
    size_t dht_size = sizeof(std_dht);
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
    uint32_t theora_serial;    /* OGG stream serial for Theora */
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

static void videoplayer_finalizer(void *obj) {
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    if (vp->container_type == 0)
        avi_free(&vp->avi);
    if (vp->ogg_reader)
        ogg_reader_free(vp->ogg_reader);
    theora_decoder_free(&vp->theora);
    free(vp->file_data);
    vp->file_data = NULL;
}


void *rt_videoplayer_open(void *path) {
    if (!path)
        return NULL;

    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return NULL;

    /* Read entire file into memory */
    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_len <= 12 || file_len > 512L * 1024 * 1024) { /* 512 MB limit */
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
    int is_ogg = (file_len >= 4 && data[0] == 'O' && data[1] == 'g' &&
                  data[2] == 'g' && data[3] == 'S');
    int is_avi = (file_len >= 12 && data[0] == 'R' && data[1] == 'I' &&
                  data[2] == 'F' && data[3] == 'F' &&
                  data[8] == 'A' && data[9] == 'V' &&
                  data[10] == 'I' && data[11] == ' ');

    if (!is_avi && !is_ogg) {
        free(data);
        return NULL;
    }

    /* OGG/Theora decode is not complete enough to expose as a working runtime
     * surface yet. Reject it here instead of returning a player that only
     * produces placeholder frames. */
    if (is_ogg) {
        free(data);
        return NULL;
    }

    /* Create VideoPlayer object */
    rt_videoplayer *vp =
        (rt_videoplayer *)rt_obj_new_i64(0, (int64_t)sizeof(rt_videoplayer));
    if (!vp) {
        free(data);
        return NULL;
    }
    memset(vp, 0, sizeof(*vp));
    vp->file_data = data;
    vp->file_len = (size_t)file_len;

    vp->container_type = 0;
    if (avi_parse(&vp->avi, data, (size_t)file_len) != 0) {
        free(data);
        if (rt_obj_release_check0(vp))
            rt_obj_free(vp);
        return NULL;
    }
    vp->width = vp->avi.video.width;
    vp->height = vp->avi.video.height;
    vp->fps = vp->avi.video.fps;
    vp->duration = vp->avi.video.duration;
    vp->total_frames = vp->avi.video_frame_count;
    vp->playing = 0;
    vp->position = 0.0;
    vp->current_frame = -1;
    vp->volume = 1.0;

    /* Stable frame buffer — pre-allocated, content copied in each frame.
     * This prevents GC from collecting the display Pixels mid-frame. */
    vp->frame_display = rt_pixels_new(vp->width, vp->height);
    vp->frame_decode = NULL;

    rt_obj_set_finalizer(vp, videoplayer_finalizer);

    /* Decode first frame immediately so get_Frame works before Play */
    if (vp->total_frames > 0) {
        uint32_t frame_size = 0;
        const uint8_t *frame_data = avi_get_video_frame(&vp->avi, 0, &frame_size);
        if (frame_data) {
            void *decoded = decode_mjpeg_frame(frame_data, frame_size);
            if (decoded && vp->frame_display) {
                px_view *dst = (px_view *)vp->frame_display;
                px_view *src = (px_view *)decoded;
                if (dst->data && src->data && dst->width == src->width &&
                    dst->height == src->height)
                    memcpy(dst->data, src->data,
                           (size_t)(dst->width * dst->height) * sizeof(uint32_t));
            }
        }
        vp->current_frame = 0;
    }

    return vp;
}

void rt_videoplayer_play(void *obj) {
    if (!obj) return;
    ((rt_videoplayer *)obj)->playing = 1;
}

void rt_videoplayer_pause(void *obj) {
    if (!obj) return;
    ((rt_videoplayer *)obj)->playing = 0;
}

void rt_videoplayer_stop(void *obj) {
    if (!obj) return;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    vp->playing = 0;
    vp->position = 0.0;
    vp->current_frame = -1;
}

void rt_videoplayer_seek(void *obj, double seconds) {
    if (!obj) return;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    if (seconds < 0.0) seconds = 0.0;
    if (seconds > vp->duration) seconds = vp->duration;
    vp->position = seconds;
    vp->current_frame = -1; /* force re-decode on next Update */
}

void rt_videoplayer_update(void *obj, double dt) {
    if (!obj || dt <= 0.0) return;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    if (!vp->playing) return;

    vp->position += dt;

    /* Determine target frame */
    int32_t target = (int32_t)(vp->position * vp->fps);
    if (target >= vp->total_frames) {
        vp->playing = 0;
        vp->position = vp->duration;
        return;
    }

    /* Only decode if frame changed */
    if (target != vp->current_frame && target >= 0) {
        uint32_t frame_size = 0;
        const uint8_t *frame_data =
            avi_get_video_frame(&vp->avi, target, &frame_size);
        if (frame_data && vp->frame_display) {
            void *decoded = decode_mjpeg_frame(frame_data, frame_size);
            if (decoded) {
                /* Copy decoded pixels into stable frame buffer */
                px_view *dst = (px_view *)vp->frame_display;
                px_view *src = (px_view *)decoded;
                if (dst->data && src->data && dst->width == src->width &&
                    dst->height == src->height)
                    memcpy(dst->data, src->data,
                           (size_t)(dst->width * dst->height) * sizeof(uint32_t));
            }
        }
        vp->current_frame = target;
    }
}

void rt_videoplayer_set_volume(void *obj, double vol) {
    if (!obj) return;
    if (vol < 0.0) vol = 0.0;
    if (vol > 1.0) vol = 1.0;
    ((rt_videoplayer *)obj)->volume = vol;
}

int64_t rt_videoplayer_get_width(void *obj) {
    return obj ? ((rt_videoplayer *)obj)->width : 0;
}

int64_t rt_videoplayer_get_height(void *obj) {
    return obj ? ((rt_videoplayer *)obj)->height : 0;
}

double rt_videoplayer_get_duration(void *obj) {
    return obj ? ((rt_videoplayer *)obj)->duration : 0.0;
}

double rt_videoplayer_get_position(void *obj) {
    return obj ? ((rt_videoplayer *)obj)->position : 0.0;
}

int64_t rt_videoplayer_get_is_playing(void *obj) {
    return obj ? ((rt_videoplayer *)obj)->playing : 0;
}

void *rt_videoplayer_get_frame(void *obj) {
    return obj ? ((rt_videoplayer *)obj)->frame_display : NULL;
}

#endif /* VIPER_ENABLE_GRAPHICS */
