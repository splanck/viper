/*
 * ViperGFX - Windows Win32 Backend
 * Platform-specific window creation, event loop, framebuffer blitting
 */

#include "vgfx_internal.h"

#ifdef _WIN32

#include <windows.h>

/* Initialize platform-specific window resources */
int vgfx_platform_init_window(struct vgfx_window* win,
                               const vgfx_window_params_t* params) {
    /* Stub: Will create HWND via CreateWindowEx and DIB section */
    (void)win;
    (void)params;
    return 1;  /* Success */
}

/* Destroy platform-specific window resources */
void vgfx_platform_destroy_window(struct vgfx_window* win) {
    /* Stub: Will destroy HWND and cleanup GDI resources */
    (void)win;
}

/* Process OS events and translate into ViperGFX events */
int vgfx_platform_process_events(struct vgfx_window* win) {
    /* Stub: Will process message queue via PeekMessage/DispatchMessage */
    (void)win;
    return 1;  /* Success */
}

/* Present framebuffer to window (blit) */
int vgfx_platform_present(struct vgfx_window* win) {
    /* Stub: Will blit win->pixels to window via BitBlt or StretchDIBits */
    (void)win;
    return 1;  /* Success */
}

/* High-resolution timer in milliseconds since arbitrary epoch */
int64_t vgfx_platform_now_ms(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (int64_t)((counter.QuadPart * 1000) / freq.QuadPart);
}

/* Sleep for specified number of milliseconds */
void vgfx_platform_sleep_ms(int32_t ms) {
    if (ms > 0) {
        Sleep((DWORD)ms);
    }
}

#endif /* _WIN32 */
