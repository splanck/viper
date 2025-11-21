/*
 * ViperGFX - Linux X11 Backend
 * Platform-specific window creation, event loop, framebuffer blitting
 */

#include "vgfx_internal.h"

#if defined(__linux__) || defined(__unix__)

#include <time.h>
#include <unistd.h>

/* Initialize platform-specific window resources */
int vgfx_platform_init_window(struct vgfx_window* win,
                               const vgfx_window_params_t* params) {
    /* Stub: Will create X11 Window and XImage */
    (void)win;
    (void)params;
    return 1;  /* Success */
}

/* Destroy platform-specific window resources */
void vgfx_platform_destroy_window(struct vgfx_window* win) {
    /* Stub: Will destroy X11 Window and cleanup resources */
    (void)win;
}

/* Process OS events and translate into ViperGFX events */
int vgfx_platform_process_events(struct vgfx_window* win) {
    /* Stub: Will process X11 event queue via XPending/XNextEvent */
    (void)win;
    return 1;  /* Success */
}

/* Present framebuffer to window (blit) */
int vgfx_platform_present(struct vgfx_window* win) {
    /* Stub: Will blit win->pixels to X11 window via XPutImage */
    (void)win;
    return 1;  /* Success */
}

/* High-resolution timer in milliseconds since arbitrary epoch */
int64_t vgfx_platform_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Sleep for specified number of milliseconds */
void vgfx_platform_sleep_ms(int32_t ms) {
    if (ms > 0) {
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (ms % 1000) * 1000000;
        nanosleep(&ts, NULL);
    }
}

#endif /* __linux__ || __unix__ */
