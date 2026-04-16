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
#include "rt_string.h"
#include "rt_videoplayer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern const char *rt_string_cstr(rt_string str);
extern int64_t rt_pixels_width(void *pixels);
extern int64_t rt_pixels_height(void *pixels);

/* GUI widget creation functions */
extern void *rt_image_new(void *parent);
extern void rt_image_set_pixels(void *image, void *pixels, int64_t w, int64_t h);
extern void rt_image_set_scale_mode(void *image, int64_t mode);
extern void rt_widget_destroy(void *widget);
extern void rt_widget_set_size(void *widget, int64_t w, int64_t h);
extern void rt_widget_set_flex(void *widget, double flex);
extern void rt_widget_set_visible(void *widget, int64_t visible);
extern int64_t rt_widget_was_clicked(void *widget);
extern void *rt_vbox_new(void);
extern void *rt_hbox_new(void);
extern void rt_container_set_spacing(void *container, double spacing);
extern void rt_widget_add_child(void *parent, void *child);
extern void *rt_button_new(void *parent, void *text);
extern void *rt_slider_new(void *parent, int64_t horizontal);
extern void rt_slider_set_value(void *slider, double value);
extern double rt_slider_get_value(void *slider);
extern rt_string rt_string_from_bytes(const char *data, size_t len);

/* VideoPlayer functions */
extern void *rt_videoplayer_open(void *path);
extern void rt_videoplayer_play(void *vp);
extern void rt_videoplayer_pause(void *vp);
extern void rt_videoplayer_stop(void *vp);
extern void rt_videoplayer_update(void *vp, double dt);
extern void rt_videoplayer_seek(void *vp, double seconds);
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
    void *player;          /* rt_videoplayer */
    void *root_widget;     /* VBox container attached to parent */
    void *image_widget;    /* vg_image_t for video display */
    void *controls_widget; /* HBox container for transport controls */
    void *play_button;
    void *pause_button;
    void *stop_button;
    void *position_slider;
    /* Config */
    int8_t show_controls;
    int8_t looping;
    double volume;
    double slider_last_value;
    /* Cached dimensions */
    int32_t video_width;
    int32_t video_height;
    /* RGBA buffer for vg_image_set_pixels (Viper Pixels uses 0xRRGGBBAA uint32,
     * but vg_image expects byte-order RGBA) */
    uint8_t *rgba_buf;
    int32_t rgba_buf_size;
} rt_videowidget;

static void videowidget_dispose(rt_videowidget *w, int destroy_widget_tree);

static void videowidget_finalizer(void *obj) {
    videowidget_dispose((rt_videowidget *)obj, 0);
}

static void release_gc_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void videowidget_dispose(rt_videowidget *w, int destroy_widget_tree) {
    if (!w)
        return;

    if (destroy_widget_tree && w->root_widget) {
        rt_widget_destroy(w->root_widget);
    }

    w->root_widget = NULL;
    w->image_widget = NULL;
    w->controls_widget = NULL;
    w->play_button = NULL;
    w->pause_button = NULL;
    w->stop_button = NULL;
    w->position_slider = NULL;

    free(w->rgba_buf);
    w->rgba_buf = NULL;
    w->rgba_buf_size = 0;

    if (w->player) {
        release_gc_object(w->player);
        w->player = NULL;
    }
}

static double clamp_volume(double vol) {
    if (vol < 0.0)
        return 0.0;
    if (vol > 1.0)
        return 1.0;
    return vol;
}

/// @brief Convert Viper Pixels (uint32 0xRRGGBBAA) to byte-order RGBA for vg_image.
static void pixels_to_rgba_bytes(const uint32_t *src, uint8_t *dst, int32_t width, int32_t height) {
    int32_t count = width * height;
    for (int32_t i = 0; i < count; i++) {
        uint32_t px = src[i];
        dst[i * 4 + 0] = (uint8_t)((px >> 24) & 0xFF); /* R */
        dst[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF); /* G */
        dst[i * 4 + 2] = (uint8_t)((px >> 8) & 0xFF);  /* B */
        dst[i * 4 + 3] = (uint8_t)(px & 0xFF);         /* A */
    }
}

/// @brief Construct a video-playback GUI widget. Opens the file via `rt_videoplayer_open`,
/// builds a vbox containing an Image widget (for frames) plus an hbox of Play/Pause/Stop buttons
/// and a position-slider. Pre-allocates an RGBA conversion buffer sized to the video. Returns
/// NULL if the file can't be opened or the video has zero dimensions.
void *rt_videowidget_new(void *parent, void *path) {
    if (!parent || !path)
        return NULL;

    /* Open video file */
    void *player = rt_videoplayer_open(path);
    if (!player)
        return NULL;

    int32_t vw = (int32_t)rt_videoplayer_get_width(player);
    int32_t vh = (int32_t)rt_videoplayer_get_height(player);
    if (vw <= 0 || vh <= 0) {
        release_gc_object(player);
        return NULL;
    }

    /* Create widget */
    rt_videowidget *w = (rt_videowidget *)rt_obj_new_i64(0, (int64_t)sizeof(rt_videowidget));
    if (!w) {
        release_gc_object(player);
        return NULL;
    }
    memset(w, 0, sizeof(*w));
    w->player = player;
    w->video_width = vw;
    w->video_height = vh;
    w->show_controls = 1;
    w->looping = 0;
    w->volume = 1.0;
    w->slider_last_value = 0.0;

    w->root_widget = rt_vbox_new();
    if (!w->root_widget) {
        videowidget_dispose(w, 0);
        release_gc_object(w);
        return NULL;
    }
    rt_container_set_spacing(w->root_widget, 8.0);

    /* Create image widget for video display */
    w->image_widget = rt_image_new(w->root_widget);
    if (!w->image_widget) {
        videowidget_dispose(w, 1);
        release_gc_object(w);
        return NULL;
    }
    rt_image_set_scale_mode(w->image_widget, 1); /* VG_IMAGE_SCALE_FIT */
    rt_widget_set_size(w->image_widget, vw, vh);
    rt_widget_set_flex(w->image_widget, 1.0);

    w->controls_widget = rt_hbox_new();
    if (w->controls_widget) {
        rt_container_set_spacing(w->controls_widget, 8.0);
        w->play_button =
            rt_button_new(w->controls_widget, rt_string_from_bytes("Play", strlen("Play")));
        w->pause_button =
            rt_button_new(w->controls_widget, rt_string_from_bytes("Pause", strlen("Pause")));
        w->stop_button =
            rt_button_new(w->controls_widget, rt_string_from_bytes("Stop", strlen("Stop")));
        w->position_slider = rt_slider_new(w->controls_widget, 1);
        if (w->position_slider) {
            rt_widget_set_flex(w->position_slider, 1.0);
            rt_slider_set_value(w->position_slider, 0.0);
        }
    }

    /* Allocate RGBA conversion buffer */
    w->rgba_buf_size = vw * vh * 4;
    w->rgba_buf = (uint8_t *)malloc((size_t)w->rgba_buf_size);
    if (!w->rgba_buf) {
        videowidget_dispose(w, 1);
        release_gc_object(w);
        return NULL;
    }

    rt_obj_set_finalizer(w, videowidget_finalizer);
    rt_widget_add_child(parent, w->root_widget);
    if (w->controls_widget)
        rt_widget_add_child(w->root_widget, w->controls_widget);

    /* Display first frame */
    rt_videowidget_update(w, 0.0);

    return w;
}

/// @brief Destroy the video widget subtree and release the owned VideoPlayer.
void rt_videowidget_destroy(void *obj) {
    if (!obj)
        return;
    videowidget_dispose((rt_videowidget *)obj, 1);
}

/// @brief Begin or resume video playback. Forwards to the underlying VideoPlayer.
void rt_videowidget_play(void *obj) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    if (!w->player)
        return;
    rt_videoplayer_play(w->player);
}

/// @brief Pause playback (preserves position). Resume with `_play`.
void rt_videowidget_pause(void *obj) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    if (!w->player)
        return;
    rt_videoplayer_pause(w->player);
}

/// @brief Stop playback and rewind to position 0.
void rt_videowidget_stop(void *obj) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    if (!w->player)
        return;
    rt_videoplayer_stop(w->player);
}

/// @brief Per-frame tick: handle button clicks (play/pause/stop), advance video by `dt` seconds,
/// auto-loop when configured (rewind+play on natural end), decode the current frame to RGBA and
/// blit into the Image widget, and bidirectionally sync the position slider with playback time.
/// Slider drags cause seeks; playback time updates the slider position. Caller invokes once per
/// frame from the GUI loop.
void rt_videowidget_update(void *obj, double dt) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    if (!w->player)
        return;

    if (w->play_button && rt_widget_was_clicked(w->play_button))
        rt_videoplayer_play(w->player);
    if (w->pause_button && rt_widget_was_clicked(w->pause_button))
        rt_videoplayer_pause(w->player);
    if (w->stop_button && rt_widget_was_clicked(w->stop_button))
        rt_videoplayer_stop(w->player);

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
            pixels_to_rgba_bytes(px->data, w->rgba_buf, w->video_width, w->video_height);
            rt_image_set_pixels(w->image_widget, w->rgba_buf, w->video_width, w->video_height);
        }
    }

    if (w->position_slider) {
        double duration = rt_videoplayer_get_duration(w->player);
        if (duration > 0.0) {
            double slider_value = rt_slider_get_value(w->position_slider);
            if (slider_value < 0.0)
                slider_value = 0.0;
            if (slider_value > 1.0)
                slider_value = 1.0;
            if (slider_value != w->slider_last_value) {
                rt_videoplayer_seek(w->player, slider_value * duration);
                w->slider_last_value = slider_value;
            }
            double playback_value = rt_videoplayer_get_position(w->player) / duration;
            if (playback_value < 0.0)
                playback_value = 0.0;
            if (playback_value > 1.0)
                playback_value = 1.0;
            rt_slider_set_value(w->position_slider, playback_value);
            w->slider_last_value = playback_value;
        }
    }
}

/// @brief Toggle visibility of the play/pause/stop/slider strip (image widget always visible).
void rt_videowidget_set_show_controls(void *obj, int8_t show) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    w->show_controls = show;
    if (w->controls_widget)
        rt_widget_set_visible(w->controls_widget, show != 0);
}

/// @brief Enable auto-loop. When enabled, the widget restarts playback on natural end-of-video.
void rt_videowidget_set_loop(void *obj, int8_t loop) {
    if (!obj)
        return;
    ((rt_videowidget *)obj)->looping = loop;
}

/// @brief Set audio output level [0.0, 1.0]. Clamped via `clamp_volume`. Forwarded to player.
void rt_videowidget_set_volume(void *obj, double vol) {
    if (!obj)
        return;
    rt_videowidget *w = (rt_videowidget *)obj;
    vol = clamp_volume(vol);
    w->volume = vol;
    if (w->player)
        rt_videoplayer_set_volume(w->player, vol);
}

/// @brief Return 1 if the underlying VideoPlayer is currently playing, else 0.
int64_t rt_videowidget_get_is_playing(void *obj) {
    if (!obj)
        return 0;
    rt_videowidget *w = (rt_videowidget *)obj;
    return w->player ? rt_videoplayer_get_is_playing(w->player) : 0;
}

/// @brief Current playback position in seconds (forwarded from the underlying VideoPlayer).
double rt_videowidget_get_position(void *obj) {
    if (!obj)
        return 0.0;
    rt_videowidget *w = (rt_videowidget *)obj;
    return w->player ? rt_videoplayer_get_position(w->player) : 0.0;
}

/// @brief Total video duration in seconds (forwarded from the underlying VideoPlayer).
double rt_videowidget_get_duration(void *obj) {
    if (!obj)
        return 0.0;
    rt_videowidget *w = (rt_videowidget *)obj;
    return w->player ? rt_videoplayer_get_duration(w->player) : 0.0;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
