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

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
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

    const uint8_t *y_plane = y + dec->pic_y * dec->y_stride + dec->pic_x;
    const uint8_t *cb_plane = cb + (dec->pic_y / 2) * dec->c_stride + (dec->pic_x / 2);
    const uint8_t *cr_plane = cr + (dec->pic_y / 2) * dec->c_stride + (dec->pic_x / 2);
    ycbcr420_to_rgba(
        y_plane, cb_plane, cr_plane, pic_w, pic_h, dec->y_stride, dec->c_stride, dst->data);
    return 1;
}

#ifdef VIPER_ENABLE_AUDIO
static void videoplayer_set_audio_volume(rt_videoplayer *vp) {
    if (!vp || !vp->audio_track)
        return;
    int64_t vol = (int64_t)(vp->volume * 100.0 + 0.5);
    rt_music_set_volume(vp->audio_track, vol);
}

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

static void videoplayer_pause_audio(rt_videoplayer *vp) {
    if (!vp || !vp->audio_track || !vp->audio_started || vp->audio_paused)
        return;
    rt_music_pause(vp->audio_track);
    vp->audio_paused = 1;
}

static void videoplayer_stop_audio(rt_videoplayer *vp) {
    if (!vp || !vp->audio_track)
        return;
    rt_music_stop(vp->audio_track);
    vp->audio_started = 0;
    vp->audio_paused = 0;
}

static void videoplayer_seek_audio(rt_videoplayer *vp) {
    if (!vp || !vp->audio_track || !vp->audio_started)
        return;
    rt_music_seek(vp->audio_track, (int64_t)(vp->position * 1000.0 + 0.5));
}
#endif

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

static void videoplayer_finalizer(void *obj) {
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    if (vp->container_type == 0)
        avi_free(&vp->avi);
#ifdef VIPER_ENABLE_AUDIO
    if (vp->audio_track)
        rt_music_destroy(vp->audio_track);
#endif
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
    rt_videoplayer *vp = (rt_videoplayer *)rt_obj_new_i64(0, (int64_t)sizeof(rt_videoplayer));
    if (!vp) {
        free(data);
        return NULL;
    }
    memset(vp, 0, sizeof(*vp));
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

    /* Stable frame buffer — pre-allocated, content copied in each frame.
     * This prevents GC from collecting the display Pixels mid-frame. */
    vp->frame_display = rt_pixels_new(vp->width, vp->height);
    vp->frame_decode = NULL;

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
                    memcpy(dst->data,
                           src->data,
                           (size_t)(dst->width * dst->height) * sizeof(uint32_t));
            }
        }
        vp->current_frame = 0;
    }

    return vp;
}

void rt_videoplayer_play(void *obj) {
    if (!obj)
        return;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    vp->playing = 1;
#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1)
        videoplayer_start_audio(vp);
#endif
}

void rt_videoplayer_pause(void *obj) {
    if (!obj)
        return;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    vp->playing = 0;
#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1)
        videoplayer_pause_audio(vp);
#endif
}

void rt_videoplayer_stop(void *obj) {
    if (!obj)
        return;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
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

void rt_videoplayer_seek(void *obj, double seconds) {
    if (!obj)
        return;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    if (seconds < 0.0)
        seconds = 0.0;
    if (seconds > vp->duration)
        seconds = vp->duration;
    vp->position = seconds;
    if (vp->container_type == 1) {
        int32_t target = (int32_t)(vp->position * vp->fps);
        if (target >= vp->total_frames)
            target = vp->total_frames > 0 ? vp->total_frames - 1 : -1;
        vp->current_frame = -1;
        if (ogv_prepare_playback(vp) && target >= 0)
            ogv_decode_until_frame(vp, target);
#ifdef VIPER_ENABLE_AUDIO
        videoplayer_seek_audio(vp);
#endif
    } else {
        vp->current_frame = -1; /* force re-decode on next Update */
    }
}

void rt_videoplayer_update(void *obj, double dt) {
    if (!obj || dt <= 0.0)
        return;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    if (!vp->playing)
        return;

    if (vp->container_type == 1) {
#ifdef VIPER_ENABLE_AUDIO
        if (vp->audio_track && vp->audio_started && rt_music_is_playing(vp->audio_track)) {
            vp->position = (double)rt_music_get_position(vp->audio_track) / 1000.0;
        } else
#endif
        {
            vp->position += dt;
        }
    } else {
        vp->position += dt;
    }

    /* Determine target frame */
    int32_t target = (int32_t)(vp->position * vp->fps);
    if (target >= vp->total_frames) {
        vp->playing = 0;
        vp->position = vp->duration;
#ifdef VIPER_ENABLE_AUDIO
        if (vp->container_type == 1)
            videoplayer_stop_audio(vp);
#endif
        return;
    }

    if (vp->container_type == 1) {
        if (target != vp->current_frame && target >= 0) {
            if (!ogv_decode_until_frame(vp, target)) {
                vp->playing = 0;
                if (vp->duration > 0.0)
                    vp->position = vp->duration;
            }
        }
        return;
    }

    /* Only decode if frame changed */
    if (target != vp->current_frame && target >= 0) {
        uint32_t frame_size = 0;
        const uint8_t *frame_data = avi_get_video_frame(&vp->avi, target, &frame_size);
        if (frame_data && vp->frame_display) {
            void *decoded = decode_mjpeg_frame(frame_data, frame_size);
            if (decoded) {
                /* Copy decoded pixels into stable frame buffer */
                px_view *dst = (px_view *)vp->frame_display;
                px_view *src = (px_view *)decoded;
                if (dst->data && src->data && dst->width == src->width &&
                    dst->height == src->height)
                    memcpy(dst->data,
                           src->data,
                           (size_t)(dst->width * dst->height) * sizeof(uint32_t));
            }
        }
        vp->current_frame = target;
    }
}

void rt_videoplayer_set_volume(void *obj, double vol) {
    if (!obj)
        return;
    if (vol < 0.0)
        vol = 0.0;
    if (vol > 1.0)
        vol = 1.0;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    vp->volume = vol;
#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1)
        videoplayer_set_audio_volume(vp);
#endif
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
    if (!obj)
        return 0.0;
    rt_videoplayer *vp = (rt_videoplayer *)obj;
#ifdef VIPER_ENABLE_AUDIO
    if (vp->container_type == 1 && vp->audio_track && vp->audio_started &&
        rt_music_is_playing(vp->audio_track)) {
        return (double)rt_music_get_position(vp->audio_track) / 1000.0;
    }
#endif
    return vp->position;
}

int64_t rt_videoplayer_get_is_playing(void *obj) {
    return obj ? ((rt_videoplayer *)obj)->playing : 0;
}

void *rt_videoplayer_get_frame(void *obj) {
    return obj ? ((rt_videoplayer *)obj)->frame_display : NULL;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
