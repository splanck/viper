//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_videowidget.c
// Purpose: GUI VideoWidget — video playback widget for Viper.GUI apps.
//   Creates an image widget for video display + optional transport controls.
//
// Key invariants:
//   - Video frames decoded by VideoPlayer, blitted to vg_image_t.
//   - Slider tracks playback position (0.0-1.0 normalized).
//   - Update(dt) must be called each GUI frame to advance playback.
//   - Pixel format conversion: Viper Pixels (0xRRGGBBAA) → vg_image RGBA bytes.
//
// Links: rt_videowidget.h, rt_videoplayer.h, rt_gui.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_videowidget.h"
#include "rt_videoplayer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern const char *rt_string_cstr(void *str);
extern int64_t rt_pixels_width(void *pixels);
extern int64_t rt_pixels_height(void *pixels);

/* GUI widget creation functions */
extern void *rt_image_new(void *parent);
extern void rt_image_set_pixels(void *image, void *pixels, int64_t w, int64_t h);
extern void rt_image_set_scale_mode(void *image, int64_t mode);
extern void rt_widget_set_size(void *widget, int64_t w, int64_t h);

/* VideoPlayer functions */
extern void *rt_videoplayer_open(void *path);
extern void rt_videoplayer_play(void *vp);
extern void rt_videoplayer_pause(void *vp);
extern void rt_videoplayer_stop(void *vp);
extern void rt_videoplayer_update(void *vp, double dt);
extern void rt_videoplayer_set_volume(void *vp, double vol);
extern int64_t rt_videoplayer_get_width(void *vp);
extern int64_t rt_videoplayer_get_height(void *vp);
extern double rt_videoplayer_get_duration(void *vp);
extern double rt_videoplayer_get_position(void *vp);
extern int64_t rt_videoplayer_get_is_playing(void *vp);
extern void *rt_videoplayer_get_frame(void *vp);

/* Internal Pixels layout for raw buffer access */
typedef struct {
    int64_t width, height;
    uint32_t *data;
} px_view;

typedef struct {
    void *vptr;
    /* Owned components */
    void *player;           /* rt_videoplayer */
    void *image_widget;     /* vg_image_t for video display */
    /* Config */
    int8_t show_controls;
    int8_t looping;
    double volume;
    /* Cached dimensions */
    int32_t video_width;
    int32_t video_height;
    /* RGBA buffer for vg_image_set_pixels (Viper Pixels uses 0xRRGGBBAA uint32,
     * but vg_image expects byte-order RGBA) */
    uint8_t *rgba_buf;
    int32_t rgba_buf_size;
} rt_videowidget;

static void videowidget_finalizer(void *obj) {
    rt_videowidget *vw = (rt_videowidget *)obj;
    free(vw->rgba_buf);
    vw->rgba_buf = NULL;
    /* player, panel, image_widget, slider are GC-managed */
}

/// @brief Convert Viper Pixels (uint32 0xRRGGBBAA) to byte-order RGBA for vg_image.
static void pixels_to_rgba_bytes(const uint32_t *src, uint8_t *dst,
                                  int32_t width, int32_t height) {
    int32_t count = width * height;
    for (int32_t i = 0; i < count; i++) {
        uint32_t px = src[i];
        dst[i * 4 + 0] = (uint8_t)((px >> 24) & 0xFF); /* R */
        dst[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF); /* G */
        dst[i * 4 + 2] = (uint8_t)((px >> 8) & 0xFF);  /* B */
        dst[i * 4 + 3] = (uint8_t)(px & 0xFF);          /* A */
    }
}

void *rt_videowidget_new(void *parent, void *path) {
    if (!parent || !path)
        return NULL;

    /* Open video file */
    void *player = rt_videoplayer_open(path);
    if (!player)
        return NULL;

    int32_t vw = (int32_t)rt_videoplayer_get_width(player);
    int32_t vh = (int32_t)rt_videoplayer_get_height(player);
    if (vw <= 0 || vh <= 0)
        return NULL;

    /* Create widget */
    rt_videowidget *w =
        (rt_videowidget *)rt_obj_new_i64(0, (int64_t)sizeof(rt_videowidget));
    if (!w)
        return NULL;
    memset(w, 0, sizeof(*w));
    w->player = player;
    w->video_width = vw;
    w->video_height = vh;
    w->show_controls = 1;
    w->looping = 0;
    w->volume = 1.0;

    /* Create image widget for video display */
    w->image_widget = rt_image_new(parent);
    if (w->image_widget) {
        rt_image_set_scale_mode(w->image_widget, 1); /* VG_IMAGE_SCALE_FIT */
        rt_widget_set_size(w->image_widget, vw, vh);
    }

    /* Allocate RGBA conversion buffer */
    w->rgba_buf_size = vw * vh * 4;
    w->rgba_buf = (uint8_t *)malloc((size_t)w->rgba_buf_size);

    rt_obj_set_finalizer(w, videowidget_finalizer);

    /* Display first frame */
    rt_videowidget_update(w, 0.0);

    return w;
}

void rt_videowidget_play(void *obj) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    rt_videoplayer_play(w->player);
}

void rt_videowidget_pause(void *obj) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    rt_videoplayer_pause(w->player);
}

void rt_videowidget_stop(void *obj) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    rt_videoplayer_stop(w->player);
}

void rt_videowidget_update(void *obj, double dt) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;

    /* Advance video playback */
    if (dt > 0.0)
        rt_videoplayer_update(w->player, dt);

    /* Check for loop */
    if (w->looping && rt_videoplayer_get_is_playing(w->player) == 0 &&
        rt_videoplayer_get_position(w->player) > 0.0) {
        rt_videoplayer_stop(w->player);
        rt_videoplayer_play(w->player);
    }

    /* Update image widget with current frame */
    void *frame = rt_videoplayer_get_frame(w->player);
    if (frame && w->image_widget && w->rgba_buf) {
        px_view *px = (px_view *)frame;
        if (px->data && px->width == w->video_width && px->height == w->video_height) {
            pixels_to_rgba_bytes(px->data, w->rgba_buf, w->video_width,
                                  w->video_height);
            rt_image_set_pixels(w->image_widget, w->rgba_buf,
                                 w->video_width, w->video_height);
        }
    }

}

void rt_videowidget_set_show_controls(void *obj, int8_t show) {
    if (!obj)
        return;
    ((rt_videowidget *)obj)->show_controls = show;
}

void rt_videowidget_set_loop(void *obj, int8_t loop) {
    if (!obj)
        return;
    ((rt_videowidget *)obj)->looping = loop;
}

void rt_videowidget_set_volume(void *obj, double vol) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    w->volume = vol;
    rt_videoplayer_set_volume(w->player, vol);
}

int64_t rt_videowidget_get_is_playing(void *obj) {
    if (!obj)
        return 0;
    return rt_videoplayer_get_is_playing(((rt_videowidget *)obj)->player);
}

double rt_videowidget_get_position(void *obj) {
    if (!obj)
        return 0.0;
    return rt_videoplayer_get_position(((rt_videowidget *)obj)->player);
}

double rt_videowidget_get_duration(void *obj) {
    if (!obj)
        return 0.0;
    return rt_videoplayer_get_duration(((rt_videowidget *)obj)->player);
}

#endif /* VIPER_ENABLE_GRAPHICS */
