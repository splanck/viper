//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_platform_linux_auto.c
// Purpose: Select and dispatch the Linux Wayland or X11 graphics adapter at runtime.
// Key invariants: The first successful window fixes one backend for the process lifetime.
// Ownership/Lifetime: Owns only process-lifetime selection state; adapters own native objects.
// Links: docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#include "vgfx_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    VGFX_LINUX_BACKEND_UNSELECTED = 0,
    VGFX_LINUX_BACKEND_WAYLAND,
    VGFX_LINUX_BACKEND_X11
} vgfx_linux_backend_t;

static pthread_mutex_t g_backend_mutex = PTHREAD_MUTEX_INITIALIZER;
static vgfx_linux_backend_t g_backend = VGFX_LINUX_BACKEND_UNSELECTED;

#define DECLARE_ADAPTER(prefix)                                                                    \
    void *prefix##_vgfx_platform_aligned_alloc(size_t, size_t);                                    \
    void prefix##_vgfx_platform_aligned_free(void *);                                              \
    int64_t prefix##_vgfx_platform_now_ms(void);                                                   \
    void prefix##_vgfx_platform_sleep_ms(int32_t);                                                 \
    void prefix##_vgfx_platform_yield(void);                                                       \
    float prefix##_vgfx_platform_get_display_scale(void);                                          \
    int prefix##_vgfx_platform_get_display_logical_size(int32_t *, int32_t *);                     \
    int prefix##_vgfx_platform_init_window(struct vgfx_window *, const vgfx_window_params_t *);    \
    void prefix##_vgfx_platform_destroy_window(struct vgfx_window *);                              \
    int prefix##_vgfx_platform_wait_events(struct vgfx_window *, int32_t);                         \
    int prefix##_vgfx_platform_process_events(struct vgfx_window *);                               \
    int prefix##_vgfx_platform_present(struct vgfx_window *);                                      \
    void prefix##_vgfx_platform_set_title(struct vgfx_window *, const char *);                     \
    int prefix##_vgfx_platform_set_fullscreen(struct vgfx_window *, int);                          \
    int prefix##_vgfx_platform_is_fullscreen(struct vgfx_window *);                                \
    void prefix##_vgfx_platform_minimize(struct vgfx_window *);                                    \
    void prefix##_vgfx_platform_maximize(struct vgfx_window *);                                    \
    void prefix##_vgfx_platform_restore(struct vgfx_window *);                                     \
    int32_t prefix##_vgfx_platform_is_minimized(struct vgfx_window *);                             \
    int32_t prefix##_vgfx_platform_is_maximized(struct vgfx_window *);                             \
    void prefix##_vgfx_platform_get_position(struct vgfx_window *, int32_t *, int32_t *);          \
    void prefix##_vgfx_platform_set_position(struct vgfx_window *, int32_t, int32_t);              \
    void prefix##_vgfx_platform_focus(struct vgfx_window *);                                       \
    void prefix##_vgfx_platform_request_foreground(struct vgfx_window *);                          \
    int32_t prefix##_vgfx_platform_is_focused(struct vgfx_window *);                               \
    void prefix##_vgfx_platform_set_prevent_close(struct vgfx_window *, int32_t);                  \
    void prefix##_vgfx_platform_set_cursor(struct vgfx_window *, int32_t);                         \
    void prefix##_vgfx_platform_set_cursor_visible(struct vgfx_window *, int32_t);                 \
    void prefix##_vgfx_platform_hide_cursor(void);                                                 \
    void prefix##_vgfx_platform_show_cursor(void);                                                 \
    void prefix##_vgfx_platform_get_monitor_size(struct vgfx_window *, int32_t *, int32_t *);      \
    void prefix##_vgfx_platform_set_window_size(struct vgfx_window *, int32_t, int32_t);           \
    void prefix##_vgfx_platform_set_window_min_size(struct vgfx_window *, int32_t, int32_t);       \
    int prefix##_vgfx_clipboard_has_format(vgfx_clipboard_format_t);                               \
    char *prefix##_vgfx_clipboard_get_text(void);                                                  \
    void prefix##_vgfx_clipboard_set_text(const char *);                                           \
    void prefix##_vgfx_clipboard_clear(void);                                                      \
    void *prefix##_vgfx_get_native_view(vgfx_window_t);                                            \
    void *prefix##_vgfx_get_native_display(vgfx_window_t);                                         \
    int prefix##_vgfx_get_native_handles(vgfx_window_t, vgfx_native_handles_t *);                  \
    vgfx_window_capabilities_t prefix##_vgfx_get_window_capabilities(vgfx_window_t);               \
    void prefix##_vgfx_platform_warp_cursor(vgfx_window_t, int32_t, int32_t);                      \
    int prefix##_vgfx_platform_set_relative_mouse(struct vgfx_window *, int);                      \
    int prefix##_vgfx_platform_set_text_input_enabled(struct vgfx_window *, int32_t);              \
    int prefix##_vgfx_platform_set_text_input_state(struct vgfx_window *,                          \
                                                    const vgfx_text_input_state_t *)

DECLARE_ADAPTER(vgfx_wayland);
#if defined(VGFX_AUTO_HAS_X11)
DECLARE_ADAPTER(vgfx_x11);
#endif

static int env_has_value(const char *name) {
    const char *value = getenv(name);
    return value && value[0] != '\0';
}

static vgfx_linux_backend_t preferred_backend(void) {
    if (g_backend != VGFX_LINUX_BACKEND_UNSELECTED)
        return g_backend;
    return env_has_value("WAYLAND_DISPLAY") ? VGFX_LINUX_BACKEND_WAYLAND
#if defined(VGFX_AUTO_HAS_X11)
                                            : VGFX_LINUX_BACKEND_X11;
#else
                                            : VGFX_LINUX_BACKEND_WAYLAND;
#endif
}

int vgfx_platform_init_window(struct vgfx_window *win, const vgfx_window_params_t *params) {
    int result = 0;
    (void)pthread_mutex_lock(&g_backend_mutex);
    if (g_backend == VGFX_LINUX_BACKEND_WAYLAND)
        result = vgfx_wayland_vgfx_platform_init_window(win, params);
#if defined(VGFX_AUTO_HAS_X11)
    else if (g_backend == VGFX_LINUX_BACKEND_X11)
        result = vgfx_x11_vgfx_platform_init_window(win, params);
#endif
    else {
        if (env_has_value("WAYLAND_DISPLAY") &&
            vgfx_wayland_vgfx_platform_init_window(win, params)) {
            g_backend = VGFX_LINUX_BACKEND_WAYLAND;
            result = 1;
        }
#if defined(VGFX_AUTO_HAS_X11)
        else if (env_has_value("DISPLAY") && vgfx_x11_vgfx_platform_init_window(win, params)) {
            g_backend = VGFX_LINUX_BACKEND_X11;
            result = 1;
        }
#endif
        else if (!env_has_value("WAYLAND_DISPLAY")) {
            vgfx_internal_set_error(VGFX_ERR_PLATFORM,
                                    "no usable Linux display: WAYLAND_DISPLAY is unset"
#if defined(VGFX_AUTO_HAS_X11)
                                    " and DISPLAY is unset or X11 initialization failed"
#else
                                    " and this build has no X11 adapter"
#endif
            );
        }
    }
    (void)pthread_mutex_unlock(&g_backend_mutex);
    return result;
}

#define DISPATCH_RET(type, name, params, args, fallback)                                           \
    type name params {                                                                             \
        if (preferred_backend() == VGFX_LINUX_BACKEND_WAYLAND)                                     \
            return vgfx_wayland_##name args;                                                       \
        (void)0;                                                                                   \
        /* X11 is compiled only when its development surface was found. */                         \
        VGFX_AUTO_X11_RETURN(name, args)                                                           \
        return fallback;                                                                           \
    }

#define DISPATCH_VOID(name, params, args)                                                          \
    void name params {                                                                             \
        if (preferred_backend() == VGFX_LINUX_BACKEND_WAYLAND) {                                   \
            vgfx_wayland_##name args;                                                              \
            return;                                                                                \
        }                                                                                          \
        VGFX_AUTO_X11_CALL(name, args)                                                             \
    }

#if defined(VGFX_AUTO_HAS_X11)
#define VGFX_AUTO_X11_RETURN(name, args) return vgfx_x11_##name args;
#define VGFX_AUTO_X11_CALL(name, args) vgfx_x11_##name args;
#else
#define VGFX_AUTO_X11_RETURN(name, args)
#define VGFX_AUTO_X11_CALL(name, args)
#endif

DISPATCH_RET(void *, vgfx_platform_aligned_alloc, (size_t a, size_t s), (a, s), NULL)
DISPATCH_VOID(vgfx_platform_aligned_free, (void *p), (p))
DISPATCH_RET(int64_t, vgfx_platform_now_ms, (void), (), 0)
DISPATCH_VOID(vgfx_platform_sleep_ms, (int32_t ms), (ms))
DISPATCH_VOID(vgfx_platform_yield, (void), ())
DISPATCH_RET(float, vgfx_platform_get_display_scale, (void), (), 1.0f)
DISPATCH_RET(int, vgfx_platform_get_display_logical_size, (int32_t *w, int32_t *h), (w, h), 0)
DISPATCH_VOID(vgfx_platform_destroy_window, (struct vgfx_window * w), (w))
DISPATCH_RET(int, vgfx_platform_wait_events, (struct vgfx_window * w, int32_t ms), (w, ms), 0)
DISPATCH_RET(int, vgfx_platform_process_events, (struct vgfx_window * w), (w), 0)
DISPATCH_RET(int, vgfx_platform_present, (struct vgfx_window * w), (w), 0)
DISPATCH_VOID(vgfx_platform_set_title, (struct vgfx_window * w, const char *s), (w, s))
DISPATCH_RET(int, vgfx_platform_set_fullscreen, (struct vgfx_window * w, int v), (w, v), 0)
DISPATCH_RET(int, vgfx_platform_is_fullscreen, (struct vgfx_window * w), (w), 0)
DISPATCH_VOID(vgfx_platform_minimize, (struct vgfx_window * w), (w))
DISPATCH_VOID(vgfx_platform_maximize, (struct vgfx_window * w), (w))
DISPATCH_VOID(vgfx_platform_restore, (struct vgfx_window * w), (w))
DISPATCH_RET(int32_t, vgfx_platform_is_minimized, (struct vgfx_window * w), (w), 0)
DISPATCH_RET(int32_t, vgfx_platform_is_maximized, (struct vgfx_window * w), (w), 0)
DISPATCH_VOID(vgfx_platform_get_position,
              (struct vgfx_window * w, int32_t *x, int32_t *y),
              (w, x, y))
DISPATCH_VOID(vgfx_platform_set_position, (struct vgfx_window * w, int32_t x, int32_t y), (w, x, y))
DISPATCH_VOID(vgfx_platform_focus, (struct vgfx_window * w), (w))
DISPATCH_VOID(vgfx_platform_request_foreground, (struct vgfx_window * w), (w))
DISPATCH_RET(int32_t, vgfx_platform_is_focused, (struct vgfx_window * w), (w), 0)
DISPATCH_VOID(vgfx_platform_set_prevent_close, (struct vgfx_window * w, int32_t v), (w, v))
DISPATCH_VOID(vgfx_platform_set_cursor, (struct vgfx_window * w, int32_t v), (w, v))
DISPATCH_VOID(vgfx_platform_set_cursor_visible, (struct vgfx_window * w, int32_t v), (w, v))
DISPATCH_VOID(vgfx_platform_hide_cursor, (void), ())
DISPATCH_VOID(vgfx_platform_show_cursor, (void), ())
DISPATCH_VOID(vgfx_platform_get_monitor_size,
              (struct vgfx_window * w, int32_t *x, int32_t *y),
              (w, x, y))
DISPATCH_VOID(vgfx_platform_set_window_size,
              (struct vgfx_window * w, int32_t x, int32_t y),
              (w, x, y))
DISPATCH_VOID(vgfx_platform_set_window_min_size,
              (struct vgfx_window * w, int32_t x, int32_t y),
              (w, x, y))
DISPATCH_RET(int, vgfx_clipboard_has_format, (vgfx_clipboard_format_t f), (f), 0)
DISPATCH_RET(char *, vgfx_clipboard_get_text, (void), (), NULL)
DISPATCH_VOID(vgfx_clipboard_set_text, (const char *s), (s))
DISPATCH_VOID(vgfx_clipboard_clear, (void), ())
DISPATCH_RET(void *, vgfx_get_native_view, (vgfx_window_t w), (w), NULL)
DISPATCH_RET(void *, vgfx_get_native_display, (vgfx_window_t w), (w), NULL)
DISPATCH_RET(int, vgfx_get_native_handles, (vgfx_window_t w, vgfx_native_handles_t *h), (w, h), 0)
DISPATCH_RET(vgfx_window_capabilities_t, vgfx_get_window_capabilities, (vgfx_window_t w), (w), 0)
DISPATCH_VOID(vgfx_platform_warp_cursor, (vgfx_window_t w, int32_t x, int32_t y), (w, x, y))
DISPATCH_RET(int, vgfx_platform_set_relative_mouse, (struct vgfx_window * w, int v), (w, v), 0)
DISPATCH_RET(
    int, vgfx_platform_set_text_input_enabled, (struct vgfx_window * w, int32_t v), (w, v), 0)
DISPATCH_RET(int,
             vgfx_platform_set_text_input_state,
             (struct vgfx_window * w, const vgfx_text_input_state_t *s),
             (w, s),
             0)
