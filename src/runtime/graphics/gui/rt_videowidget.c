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
#include "rt_gui.h"
#include "rt_pixels.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_videoplayer.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);

/* GUI parent validation shim implemented by rt_gui_widgets.c. Kept as a
 * single external dependency so isolated VideoWidget contract tests can stub
 * the runtime GUI layer without linking all widget/app objects. */
extern void *rt_gui_widget_parent_container_checked(void *handle);

/* VideoPlayer functions */
extern void *rt_videoplayer_open(rt_string path);
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

typedef struct {
    uint64_t magic;
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
} rt_videowidget;

static void videowidget_dispose(rt_videowidget *w, int destroy_widget_tree);

#define RT_VIDEOWIDGET_MAGIC UINT64_C(0x5254564944454F57)

static rt_videowidget **s_videowidget_wrappers = NULL;
static size_t s_videowidget_wrapper_count = 0;
static size_t s_videowidget_wrapper_cap = 0;

/// @brief Record a wrapper in the global VideoWidget registry (idempotent).
/// @details The registry is the source of truth for handle validation: videowidget_checked
///          only trusts an opaque `void*` once it is found here (then verifies the magic
///          tag), guarding against forged/freed handles. Capacity doubles from 8.
/// @return 1 on success or if already present; 0 on overflow or realloc failure.
static int videowidget_register_wrapper(rt_videowidget *w) {
    if (!w)
        return 0;
    for (size_t i = 0; i < s_videowidget_wrapper_count; i++) {
        if (s_videowidget_wrappers[i] == w)
            return 1;
    }
    if (s_videowidget_wrapper_count >= s_videowidget_wrapper_cap) {
        size_t new_cap = s_videowidget_wrapper_cap ? s_videowidget_wrapper_cap : 8;
        while (new_cap <= s_videowidget_wrapper_count) {
            if (new_cap > SIZE_MAX / 2)
                return 0;
            new_cap *= 2;
        }
        if (new_cap > SIZE_MAX / sizeof(rt_videowidget *))
            return 0;
        void *p = realloc(s_videowidget_wrappers, new_cap * sizeof(rt_videowidget *));
        if (!p)
            return 0;
        s_videowidget_wrappers = (rt_videowidget **)p;
        s_videowidget_wrapper_cap = new_cap;
    }
    s_videowidget_wrappers[s_videowidget_wrapper_count++] = w;
    return 1;
}

/// @brief Remove a wrapper from the VideoWidget registry, compacting the array. No-op if absent.
static void videowidget_unregister_wrapper(rt_videowidget *w) {
    if (!w)
        return;
    for (size_t i = 0; i < s_videowidget_wrapper_count; i++) {
        if (s_videowidget_wrappers[i] != w)
            continue;
        memmove(&s_videowidget_wrappers[i],
                &s_videowidget_wrappers[i + 1],
                (s_videowidget_wrapper_count - i - 1) * sizeof(*s_videowidget_wrappers));
        s_videowidget_wrapper_count--;
        return;
    }
}

/// @brief True if @p w is a currently-registered wrapper; backs handle validation.
static int videowidget_wrapper_is_registered(const rt_videowidget *w) {
    if (!w)
        return 0;
    for (size_t i = 0; i < s_videowidget_wrapper_count; i++) {
        if (s_videowidget_wrappers[i] == w)
            return 1;
    }
    return 0;
}

/// @brief Safe-cast an opaque handle to a VideoWidget: NULL unless it is a live
///        registered wrapper carrying the expected magic tag.
static rt_videowidget *videowidget_checked(void *obj) {
    rt_videowidget *w = (rt_videowidget *)obj;
    return videowidget_wrapper_is_registered(w) && w->magic == RT_VIDEOWIDGET_MAGIC ? w : NULL;
}

/// @brief GC finalizer for a VideoWidget.
/// @details The wrapper owns the transport subtree it created. If user code
///          drops the wrapper while the GUI tree is still attached, destroy the
///          subtree so controls are not left orphaned with a released player.
static void videowidget_finalizer(void *obj) {
    videowidget_dispose((rt_videowidget *)obj, 1);
}

/// @brief Release a GC-managed object if it exists, freeing it when the refcount drops to zero.
static void release_gc_object(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Release all resources held by a VideoWidget, optionally destroying the GUI tree.
/// @details Clears all widget pointers (image, controls, buttons, slider) to prevent
///   dangling references after disposal. When @p destroy_widget_tree is non-zero it
///   also calls `rt_widget_destroy` on the root, which tears down the entire GUI
///   subtree — used during explicit `VideoWidget.Destroy` calls and GC finalization.
static void videowidget_dispose(rt_videowidget *w, int destroy_widget_tree) {
    if (!w || w->magic != RT_VIDEOWIDGET_MAGIC)
        return;
    videowidget_unregister_wrapper(w);

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

    if (w->player) {
        release_gc_object(w->player);
        w->player = NULL;
    }
    w->magic = 0;
}

/// @brief Clamp a volume value to [0.0, 1.0] before passing it to the video player.
/// @details Values outside [0, 1] are undefined behavior in most audio backends.
///   Clamping here shields the player from invalid values supplied by user code.
static double clamp_volume(double vol) {
    if (!isfinite(vol))
        return 0.0;
    if (vol < 0.0)
        return 0.0;
    if (vol > 1.0)
        return 1.0;
    return vol;
}

/// @brief Construct a video-playback GUI widget. Opens the file via `rt_videoplayer_open`,
/// builds a vbox containing an Image widget (for frames) plus an hbox of Play/Pause/Stop buttons
/// and a position-slider. Returns NULL if the file can't be opened or the video has zero
/// dimensions.
void *rt_videowidget_new(void *parent, rt_string path) {
    RT_ASSERT_MAIN_THREAD();
    if (!parent || !path)
        return NULL;
    void *parent_widget = rt_gui_widget_parent_container_checked(parent);
    if (!parent_widget)
        return NULL;

    /* Open video file */
    void *player = rt_videoplayer_open(path);
    if (!player)
        return NULL;

    int64_t vw64 = rt_videoplayer_get_width(player);
    int64_t vh64 = rt_videoplayer_get_height(player);
    if (vw64 <= 0 || vh64 <= 0 || vw64 > INT32_MAX || vh64 > INT32_MAX) {
        release_gc_object(player);
        return NULL;
    }
    int32_t vw = (int32_t)vw64;
    int32_t vh = (int32_t)vh64;

    /* Create widget */
    rt_videowidget *w = (rt_videowidget *)rt_obj_new_i64(0, (int64_t)sizeof(rt_videowidget));
    if (!w) {
        release_gc_object(player);
        return NULL;
    }
    memset(w, 0, sizeof(*w));
    w->magic = RT_VIDEOWIDGET_MAGIC;
    w->player = player;
    w->video_width = vw;
    w->video_height = vh;
    w->show_controls = 1;
    w->looping = 0;
    w->volume = 1.0;
    w->slider_last_value = 0.0;
    if (!videowidget_register_wrapper(w)) {
        videowidget_dispose(w, 0);
        release_gc_object(w);
        return NULL;
    }

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
            rt_slider_set_range(w->position_slider, 0.0, 1.0);
            rt_slider_set_value(w->position_slider, 0.0);
        }
    }

    rt_obj_set_finalizer(w, videowidget_finalizer);
    rt_widget_add_child(parent_widget, w->root_widget);
    if (w->controls_widget)
        rt_widget_add_child(w->root_widget, w->controls_widget);

    /* Display first frame */
    rt_videowidget_update(w, 0.0);

    return w;
}

/// @brief Destroy the video widget subtree and release the owned VideoPlayer.
void rt_videowidget_destroy(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return;
    videowidget_dispose(w, 1);
}

/// @brief Begin or resume video playback. Forwards to the underlying VideoPlayer.
void rt_videowidget_play(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return;
    if (!w->player)
        return;
    rt_videoplayer_play(w->player);
}

/// @brief Pause playback (preserves position). Resume with `_play`.
void rt_videowidget_pause(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return;
    if (!w->player)
        return;
    rt_videoplayer_pause(w->player);
}

/// @brief Stop playback and rewind to position 0.
void rt_videowidget_stop(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return;
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
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return;
    if (!w->player)
        return;

    if (w->play_button && rt_widget_was_clicked(w->play_button))
        rt_videoplayer_play(w->player);
    if (w->pause_button && rt_widget_was_clicked(w->pause_button))
        rt_videoplayer_pause(w->player);
    if (w->stop_button && rt_widget_was_clicked(w->stop_button))
        rt_videoplayer_stop(w->player);

    /* Advance video playback */
    if (isfinite(dt) && dt > 0.0)
        rt_videoplayer_update(w->player, dt);

    /* Check for natural end-of-video before auto-looping. Paused videos also
       report !is_playing, so position must be at the end of the stream. */
    if (w->looping && rt_videoplayer_get_is_playing(w->player) == 0 &&
        rt_videoplayer_get_duration(w->player) > 0.0 &&
        rt_videoplayer_get_position(w->player) >= rt_videoplayer_get_duration(w->player) - 0.001) {
        rt_videoplayer_stop(w->player);
        rt_videoplayer_play(w->player);
    }

    /* Update image widget with current frame */
    void *frame = rt_videoplayer_get_frame(w->player);
    if (frame && w->image_widget) {
        int64_t frame_w = rt_pixels_width(frame);
        int64_t frame_h = rt_pixels_height(frame);
        const uint32_t *raw = rt_pixels_raw_buffer(frame);
        if (raw && frame_w > 0 && frame_h > 0 && frame_w <= INT32_MAX && frame_h <= INT32_MAX) {
            rt_image_set_pixels(w->image_widget, frame, frame_w, frame_h);
            if (w->video_width != (int32_t)frame_w || w->video_height != (int32_t)frame_h) {
                w->video_width = (int32_t)frame_w;
                w->video_height = (int32_t)frame_h;
                rt_widget_set_size(w->image_widget, frame_w, frame_h);
            }
        }
    }

    if (w->position_slider) {
        double duration = rt_videoplayer_get_duration(w->player);
        if (isfinite(duration) && duration > 0.0) {
            double slider_value = rt_slider_get_value(w->position_slider);
            if (!isfinite(slider_value))
                slider_value = 0.0;
            if (slider_value < 0.0)
                slider_value = 0.0;
            if (slider_value > 1.0)
                slider_value = 1.0;
            if (slider_value != w->slider_last_value) {
                rt_videoplayer_seek(w->player, slider_value * duration);
                w->slider_last_value = slider_value;
            }
            double playback_value = rt_videoplayer_get_position(w->player) / duration;
            if (!isfinite(playback_value))
                playback_value = 0.0;
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
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return;
    w->show_controls = show;
    if (w->controls_widget)
        rt_widget_set_visible(w->controls_widget, show != 0);
}

int64_t rt_videowidget_get_show_controls(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return 0;
    return w->show_controls ? 1 : 0;
}

/// @brief Enable auto-loop. When enabled, the widget restarts playback on natural end-of-video.
void rt_videowidget_set_loop(void *obj, int8_t loop) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return;
    w->looping = loop;
}

int64_t rt_videowidget_get_loop(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return 0;
    return w->looping ? 1 : 0;
}

/// @brief Set audio output level [0.0, 1.0]. Clamped via `clamp_volume`. Forwarded to player.
void rt_videowidget_set_volume(void *obj, double vol) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return;
    vol = clamp_volume(vol);
    w->volume = vol;
    if (w->player)
        rt_videoplayer_set_volume(w->player, vol);
}

/// @brief Return 1 if the underlying VideoPlayer is currently playing, else 0.
int64_t rt_videowidget_get_is_playing(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return 0;
    return w->player ? rt_videoplayer_get_is_playing(w->player) : 0;
}

/// @brief Current playback position in seconds (forwarded from the underlying VideoPlayer).
double rt_videowidget_get_position(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return 0.0;
    return w->player ? rt_videoplayer_get_position(w->player) : 0.0;
}

/// @brief Total video duration in seconds (forwarded from the underlying VideoPlayer).
double rt_videowidget_get_duration(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (!w)
        return 0.0;
    return w->player ? rt_videoplayer_get_duration(w->player) : 0.0;
}

void *rt_videowidget_get_root(void *obj) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    return w ? w->root_widget : NULL;
}

void rt_videowidget_set_visible(void *obj, int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (w && w->root_widget)
        rt_widget_set_visible(w->root_widget, visible);
}

void rt_videowidget_set_enabled(void *obj, int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (w && w->root_widget)
        rt_widget_set_enabled(w->root_widget, enabled);
}

void rt_videowidget_set_size(void *obj, int64_t width, int64_t height) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (w && w->root_widget)
        rt_widget_set_size(w->root_widget, width, height);
}

void rt_videowidget_set_preferred_size(void *obj, double width, double height) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (w && w->root_widget)
        rt_widget_set_preferred_size(w->root_widget, width, height);
}

void rt_videowidget_set_max_size(void *obj, double width, double height) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (w && w->root_widget)
        rt_widget_set_max_size(w->root_widget, width, height);
}

void rt_videowidget_set_flex(void *obj, double flex) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (w && w->root_widget)
        rt_widget_set_flex(w->root_widget, flex);
}

void rt_videowidget_set_margin(void *obj, int64_t margin) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (w && w->root_widget)
        rt_widget_set_margin(w->root_widget, margin);
}

void rt_videowidget_set_position(void *obj, int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (w && w->root_widget)
        rt_widget_set_position(w->root_widget, x, y);
}

void rt_videowidget_add_child(void *obj, void *child) {
    RT_ASSERT_MAIN_THREAD();
    rt_videowidget *w = videowidget_checked(obj);
    if (w && w->root_widget)
        rt_widget_add_child(w->root_widget, child);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
