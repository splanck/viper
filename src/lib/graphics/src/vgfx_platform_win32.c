//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperGFX Windows Win32 Backend
//
// Platform-specific implementation using Win32 GDI on Windows systems.
// Provides window creation, event handling, framebuffer blitting via DIB
// sections, and timing functions for Windows.
//
// Architecture:
//   - HWND: Native Win32 window handle
//   - DIB Section: Device-independent bitmap for framebuffer
//   - HDC: Device contexts (window DC and memory DC for double-buffering)
//   - WndProc: Window procedure for message handling
//   - BitBlt: Fast blit operation from memory DC to window DC
//
// Key Win32 Concepts:
//   - RegisterClass: Register window class (once per process)
//   - CreateWindowEx: Create native window
//   - DIB Section: Create bitmap with direct pixel access
//   - PeekMessage/DispatchMessage: Non-blocking message processing
//   - WM_* Messages: Window manager messages (close, resize, input, etc.)
//   - Virtual Key Codes: VK_* constants for keyboard input
//   - QueryPerformanceCounter: High-resolution monotonic timer
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Windows Win32 backend implementation for ViperGFX.
/// @details Uses Win32 GDI and DIB sections to provide window management
///          and framebuffer presentation on Windows systems.

#include "vgfx_internal.h"

#ifdef _WIN32

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

//===----------------------------------------------------------------------===//
// Platform Data Structure
//===----------------------------------------------------------------------===//

/// @brief Platform-specific data for Win32 windows.
/// @details Allocated and owned by the platform backend.  Stored in
///          vgfx_window->platform_data.  Contains Win32 HWND, device contexts,
///          DIB section for framebuffer, and cached dimensions.
///
/// @invariant hwnd != NULL implies hdc != NULL && memdc != NULL && hbmp != NULL
typedef struct
{
    HINSTANCE hInstance; ///< Application instance handle
    HWND hwnd;           ///< Native Win32 window handle
    HDC hdc;             ///< Device context for window
    HDC memdc;           ///< Memory DC for off-screen rendering
    HBITMAP hbmp;        ///< DIB section bitmap handle
    void *dib_pixels;    ///< Pointer to DIB pixel data (BGRA format)
    int width;           ///< Cached window width
    int height;          ///< Cached window height
    int close_requested; ///< 1 if WM_CLOSE received, 0 otherwise
} vgfx_win32_data;

//===----------------------------------------------------------------------===//
// Forward Declarations
//===----------------------------------------------------------------------===//

static LRESULT CALLBACK vgfx_win32_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

/// @brief Convert UTF-8 string to UTF-16 (wide string).
/// @details Allocates a new wide string buffer and converts the UTF-8 input.
///          Caller must free the returned pointer with free().
///
/// @param utf8 UTF-8 encoded input string (NULL-terminated)
/// @return Allocated UTF-16 string, or NULL on failure
///
/// @post Return value must be freed with free() by caller
static WCHAR *utf8_to_utf16(const char *utf8)
{
    if (!utf8)
        return NULL;

    /* Get required buffer size */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (wlen <= 0)
        return NULL;

    /* Allocate buffer */
    WCHAR *wstr = (WCHAR *)malloc(wlen * sizeof(WCHAR));
    if (!wstr)
        return NULL;

    /* Convert */
    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wstr, wlen) == 0)
    {
        free(wstr);
        return NULL;
    }

    return wstr;
}

//===----------------------------------------------------------------------===//
// Key Code Translation
//===----------------------------------------------------------------------===//

/// @brief Translate Win32 virtual key code to vgfx_key_t.
/// @details Maps VK_* constants to ViperGFX key codes.  Handles A-Z, 0-9,
///          Space, arrows, Enter, Escape.  Unrecognized keys return
///          VGFX_KEY_UNKNOWN.
///
/// @param vk Win32 virtual key code from wParam
/// @return Corresponding vgfx_key_t, or VGFX_KEY_UNKNOWN if not recognized
///
/// @details Key mapping:
///            - VK_A - VK_Z: Mapped to vgfx_key_t enum values (uppercase)
///            - VK_0 - VK_9: Mapped to vgfx_key_t enum values
///            - VK_SPACE: VGFX_KEY_SPACE
///            - VK_LEFT/RIGHT/UP/DOWN: Arrow keys
///            - VK_RETURN: VGFX_KEY_ENTER
///            - VK_ESCAPE: VGFX_KEY_ESCAPE
static vgfx_key_t translate_vk(WPARAM vk)
{
    /* Letters A-Z (VK_A = 0x41 = 'A') */
    if (vk >= 'A' && vk <= 'Z')
    {
        return (vgfx_key_t)vk;
    }

    /* Digits 0-9 (VK_0 = 0x30 = '0') */
    if (vk >= '0' && vk <= '9')
    {
        return (vgfx_key_t)vk;
    }

    /* Special keys */
    switch (vk)
    {
        case VK_SPACE:
            return VGFX_KEY_SPACE;
        case VK_RETURN:
            return VGFX_KEY_ENTER;
        case VK_ESCAPE:
            return VGFX_KEY_ESCAPE;
        case VK_LEFT:
            return VGFX_KEY_LEFT;
        case VK_RIGHT:
            return VGFX_KEY_RIGHT;
        case VK_UP:
            return VGFX_KEY_UP;
        case VK_DOWN:
            return VGFX_KEY_DOWN;
        default:
            return VGFX_KEY_UNKNOWN;
    }
}

//===----------------------------------------------------------------------===//
// Window Procedure
//===----------------------------------------------------------------------===//

/// @brief Window procedure for ViperGFX Win32 windows.
/// @details Processes Win32 messages and translates them to ViperGFX events.
///          The vgfx_window* pointer is stored in GWLP_USERDATA during window
///          creation, allowing us to access the window state from messages.
///
/// @param hwnd Window handle receiving the message
/// @param msg Message identifier (WM_*)
/// @param wparam Message-specific parameter (often event data)
/// @param lparam Message-specific parameter (often coordinates or flags)
/// @return Result value depends on message type
///
/// @details Handles:
///            - WM_CLOSE: Window close button clicked
///            - WM_SIZE: Window resized
///            - WM_SETFOCUS/WM_KILLFOCUS: Focus change
///            - WM_KEYDOWN/WM_KEYUP: Keyboard input
///            - WM_MOUSEMOVE: Mouse movement
///            - WM_LBUTTONDOWN/UP, WM_RBUTTONDOWN/UP, WM_MBUTTONDOWN/UP: Mouse buttons
static LRESULT CALLBACK vgfx_win32_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    /* Retrieve vgfx_window pointer stored in GWLP_USERDATA */
    struct vgfx_window *win = (struct vgfx_window *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (!win)
    {
        /* Window not fully initialized yet, use default processing */
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    int64_t timestamp = vgfx_platform_now_ms();

    switch (msg)
    {
        case WM_CLOSE:
        {
            /* User clicked close button - enqueue CLOSE event but don't destroy window */
            if (win->prevent_close)
                return 0; /* Blocked by application */
            if (w32)
            {
                w32->close_requested = 1;
            }
            win->close_requested = 1;

            vgfx_event_t event = {.type = VGFX_EVENT_CLOSE, .time_ms = timestamp};
            vgfx_internal_enqueue_event(win, &event);
            return 0; /* Handled (don't call DefWindowProc) */
        }

        case WM_SIZE:
        {
            /* Window resized.  With system DPI awareness, LOWORD/HIWORD give DIP
             * (logical) client dimensions.  Multiply by scale_factor to get the
             * physical pixel size for framebuffer operations. */
            int dip_w = LOWORD(lparam);
            int dip_h = HIWORD(lparam);

            if (w32 && (dip_w != w32->width || dip_h != w32->height))
            {
                w32->width  = dip_w;  /* keep logical for StretchBlt dest */
                w32->height = dip_h;

                /* Physical dimensions for the framebuffer */
                int phys_w = (int)(dip_w * win->scale_factor);
                int phys_h = (int)(dip_h * win->scale_factor);
                win->width  = phys_w;
                win->height = phys_h;
                win->stride = phys_w * 4;

                vgfx_event_t event = {.type = VGFX_EVENT_RESIZE,
                                      .time_ms = timestamp,
                                      .data.resize = {.width = phys_w, .height = phys_h}};
                vgfx_internal_enqueue_event(win, &event);
            }
            return 0;
        }

        case WM_SETFOCUS:
        {
            /* Window gained focus */
            win->is_focused = 1;
            vgfx_event_t event = {.type = VGFX_EVENT_FOCUS_GAINED, .time_ms = timestamp};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_KILLFOCUS:
        {
            /* Window lost focus */
            win->is_focused = 0;
            vgfx_event_t event = {.type = VGFX_EVENT_FOCUS_LOST, .time_ms = timestamp};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_KEYDOWN:
        {
            /* Key pressed */
            vgfx_key_t key = translate_vk(wparam);
            if (key != VGFX_KEY_UNKNOWN && key < 512)
            {
                /* Detect repeat: bit 30 of lparam indicates previous key state */
                int is_repeat = (lparam & (1 << 30)) ? 1 : 0;
                win->key_state[key] = 1; /* Update input state */

                vgfx_event_t event = {.type = VGFX_EVENT_KEY_DOWN,
                                      .time_ms = timestamp,
                                      .data.key = {.key = key, .is_repeat = is_repeat}};
                vgfx_internal_enqueue_event(win, &event);
            }
            return 0;
        }

        case WM_KEYUP:
        {
            /* Key released */
            vgfx_key_t key = translate_vk(wparam);
            if (key != VGFX_KEY_UNKNOWN && key < 512)
            {
                win->key_state[key] = 0; /* Update input state */

                vgfx_event_t event = {.type = VGFX_EVENT_KEY_UP,
                                      .time_ms = timestamp,
                                      .data.key = {.key = key, .is_repeat = 0}};
                vgfx_internal_enqueue_event(win, &event);
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            /* Mouse moved.  With system DPI awareness, lparam gives DIP coords.
             * Scale to physical pixels so hit-testing matches the physical framebuffer. */
            int32_t x = (int32_t)((short)LOWORD(lparam) * win->scale_factor);
            int32_t y = (int32_t)((short)HIWORD(lparam) * win->scale_factor);

            win->mouse_x = x; /* Update input state */
            win->mouse_y = y;

            vgfx_event_t event = {.type = VGFX_EVENT_MOUSE_MOVE,
                                  .time_ms = timestamp,
                                  .data.mouse_move = {.x = x, .y = y}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_LBUTTONDOWN:
        {
            /* Left mouse button pressed — scale DIP coords to physical pixels */
            int32_t x = (int32_t)((short)LOWORD(lparam) * win->scale_factor);
            int32_t y = (int32_t)((short)HIWORD(lparam) * win->scale_factor);

            win->mouse_button_state[VGFX_MOUSE_LEFT] = 1;

            vgfx_event_t event = {.type = VGFX_EVENT_MOUSE_DOWN,
                                  .time_ms = timestamp,
                                  .data.mouse_button = {.x = x, .y = y, .button = VGFX_MOUSE_LEFT}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_LBUTTONUP:
        {
            /* Left mouse button released — scale DIP coords to physical pixels */
            int32_t x = (int32_t)((short)LOWORD(lparam) * win->scale_factor);
            int32_t y = (int32_t)((short)HIWORD(lparam) * win->scale_factor);

            win->mouse_button_state[VGFX_MOUSE_LEFT] = 0;

            vgfx_event_t event = {.type = VGFX_EVENT_MOUSE_UP,
                                  .time_ms = timestamp,
                                  .data.mouse_button = {.x = x, .y = y, .button = VGFX_MOUSE_LEFT}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_RBUTTONDOWN:
        {
            /* Right mouse button pressed — scale DIP coords to physical pixels */
            int32_t x = (int32_t)((short)LOWORD(lparam) * win->scale_factor);
            int32_t y = (int32_t)((short)HIWORD(lparam) * win->scale_factor);

            win->mouse_button_state[VGFX_MOUSE_RIGHT] = 1;

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_DOWN,
                .time_ms = timestamp,
                .data.mouse_button = {.x = x, .y = y, .button = VGFX_MOUSE_RIGHT}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_RBUTTONUP:
        {
            /* Right mouse button released — scale DIP coords to physical pixels */
            int32_t x = (int32_t)((short)LOWORD(lparam) * win->scale_factor);
            int32_t y = (int32_t)((short)HIWORD(lparam) * win->scale_factor);

            win->mouse_button_state[VGFX_MOUSE_RIGHT] = 0;

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_UP,
                .time_ms = timestamp,
                .data.mouse_button = {.x = x, .y = y, .button = VGFX_MOUSE_RIGHT}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_MBUTTONDOWN:
        {
            /* Middle mouse button pressed — scale DIP coords to physical pixels */
            int32_t x = (int32_t)((short)LOWORD(lparam) * win->scale_factor);
            int32_t y = (int32_t)((short)HIWORD(lparam) * win->scale_factor);

            win->mouse_button_state[VGFX_MOUSE_MIDDLE] = 1;

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_DOWN,
                .time_ms = timestamp,
                .data.mouse_button = {.x = x, .y = y, .button = VGFX_MOUSE_MIDDLE}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_MBUTTONUP:
        {
            /* Middle mouse button released — scale DIP coords to physical pixels */
            int32_t x = (int32_t)((short)LOWORD(lparam) * win->scale_factor);
            int32_t y = (int32_t)((short)HIWORD(lparam) * win->scale_factor);

            win->mouse_button_state[VGFX_MOUSE_MIDDLE] = 0;

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_UP,
                .time_ms = timestamp,
                .data.mouse_button = {.x = x, .y = y, .button = VGFX_MOUSE_MIDDLE}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_PAINT:
        {
            /* Window needs redraw - validate to prevent endless loop */
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        default:
            /* Use default window procedure for unhandled messages */
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

//===----------------------------------------------------------------------===//
// Platform API Implementation
//===----------------------------------------------------------------------===//

/// @brief Query the HiDPI backing scale factor from the Win32 display system.
/// @details Declares system DPI awareness (if not already set via manifest),
///          then queries the primary monitor's logical pixels-per-inch via
///          GetDeviceCaps.  Dividing by the standard 96 DPI gives the scale:
///            96 DPI  → 1.0   (standard display)
///           192 DPI  → 2.0   (200% scaling)
///           144 DPI  → 1.5   (150% scaling)
///
///          DPI_AWARENESS_CONTEXT_SYSTEM_AWARE is loaded dynamically so the
///          code compiles against older SDKs.  With system awareness active:
///            - CreateWindowExW dimensions are in DIP (device-independent pixel)
///            - Mouse/WM_SIZE coordinates are also in DIP
///            - Windows does NOT auto-scale rendered content
///          Multiply DIP coords by scale_factor to convert to physical pixels.
///
/// @note This must be called before any windows are created.
/// @return Scale factor ≥ 1.0
float vgfx_platform_get_display_scale(void)
{
    /* Declare system DPI awareness so GetDeviceCaps returns the real DPI.
     * DPI_AWARENESS_CONTEXT_SYSTEM_AWARE = (HANDLE)(intptr_t)(-2).
     * Loaded dynamically to avoid hard SDK version dependency. */
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        typedef BOOL (WINAPI *SPDA_fn)(HANDLE);
        SPDA_fn fn = (SPDA_fn)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn)
            fn((HANDLE)(intptr_t)(-2)); /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */
    }

    /* Query the primary monitor's DPI.  With awareness set, this returns the
     * real system DPI rather than the virtualised 96 DPI given to unaware
     * processes. */
    HDC hdc = GetDC(NULL);
    int dpi  = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);

    if (dpi < 96)
        dpi = 96; /* clamp against bogus values */

    return (float)dpi / 96.0f;
}

/// @brief Initialize platform-specific window resources for Win32.
/// @details Registers window class (once), creates Win32 window, sets up DIB
///          section for framebuffer, and makes window visible.  The DIB section
///          allows direct pixel access for efficient blitting.
///
/// @param win    Pointer to the ViperGFX window structure (framebuffer already allocated)
/// @param params Window creation parameters (title, dimensions, resizable flag)
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  params != NULL
/// @pre  win->pixels != NULL (framebuffer allocated by vgfx_create_window)
/// @post On success: Win32 window created and visible, platform_data allocated
/// @post On failure: platform_data NULL, error set
///
/// @details The window is:
///            - Overlapped with title bar, borders, system menu
///            - Resizable (if params->resizable is true)
///            - Has close button (generates CLOSE event, doesn't auto-destroy)
int vgfx_platform_init_window(struct vgfx_window *win, const vgfx_window_params_t *params)
{
    if (!win || !params)
        return 0;

    /* Allocate platform data structure */
    vgfx_win32_data *w32 = (vgfx_win32_data *)calloc(1, sizeof(vgfx_win32_data));
    if (!w32)
    {
        vgfx_internal_set_error(VGFX_ERR_ALLOC, "Failed to allocate Win32 platform data");
        return 0;
    }

    win->platform_data = w32;
    w32->close_requested = 0;
    w32->width = params->width;
    w32->height = params->height;

    /* Get application instance */
    w32->hInstance = GetModuleHandleW(NULL);

    /* Register window class (once per process) */
    static int class_registered = 0;
    if (!class_registered)
    {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = vgfx_win32_wndproc;
        wc.hInstance = w32->hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"ViperGFXClass";

        if (!RegisterClassExW(&wc))
        {
            free(w32);
            win->platform_data = NULL;
            vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to register Win32 window class");
            return 0;
        }
        class_registered = 1;
    }

    /* Convert UTF-8 title to UTF-16 */
    WCHAR *wtitle = utf8_to_utf16(params->title);
    if (!wtitle)
    {
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to convert window title");
        return 0;
    }

    /* Determine window style */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (params->resizable)
    {
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }

    /* Adjust window rect to account for borders/title bar */
    RECT rect = {0, 0, params->width, params->height};
    AdjustWindowRect(&rect, style, FALSE);
    int win_width = rect.right - rect.left;
    int win_height = rect.bottom - rect.top;

    /* Create window */
    w32->hwnd = CreateWindowExW(0,                /* Extended style */
                                L"ViperGFXClass", /* Class name */
                                wtitle,           /* Window title */
                                style,            /* Style */
                                CW_USEDEFAULT,    /* X position (default) */
                                CW_USEDEFAULT,    /* Y position (default) */
                                win_width,        /* Width (including borders) */
                                win_height,       /* Height (including borders) */
                                NULL,             /* Parent window */
                                NULL,             /* Menu */
                                w32->hInstance,   /* Instance */
                                NULL              /* Additional data */
    );

    free(wtitle);

    if (!w32->hwnd)
    {
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create Win32 window");
        return 0;
    }

    /* Store vgfx_window pointer in window user data for WndProc access */
    SetWindowLongPtrW(w32->hwnd, GWLP_USERDATA, (LONG_PTR)win);

    /* Get device context for window */
    w32->hdc = GetDC(w32->hwnd);
    if (!w32->hdc)
    {
        DestroyWindow(w32->hwnd);
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to get Win32 DC");
        return 0;
    }

    /* Create memory DC for double-buffering */
    w32->memdc = CreateCompatibleDC(w32->hdc);
    if (!w32->memdc)
    {
        ReleaseDC(w32->hwnd, w32->hdc);
        DestroyWindow(w32->hwnd);
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create memory DC");
        return 0;
    }

    /* Create DIB section for framebuffer (32-bit BGRA, top-down).
     * Use physical dimensions (win->width × win->height) so the bitmap
     * holds one pixel per physical screen pixel on HiDPI displays.
     * The present function uses StretchBlt to map it into the logical window. */
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = win->width;
    bmi.bmiHeader.biHeight = -win->height; /* Negative = top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    w32->hbmp = CreateDIBSection(w32->memdc,
                                 &bmi,
                                 DIB_RGB_COLORS,
                                 &w32->dib_pixels, /* Pointer to pixel data */
                                 NULL,
                                 0);

    if (!w32->hbmp || !w32->dib_pixels)
    {
        DeleteDC(w32->memdc);
        ReleaseDC(w32->hwnd, w32->hdc);
        DestroyWindow(w32->hwnd);
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create DIB section");
        return 0;
    }

    /* Select DIB into memory DC */
    SelectObject(w32->memdc, w32->hbmp);

    /* Show and update window */
    ShowWindow(w32->hwnd, SW_SHOW);
    UpdateWindow(w32->hwnd);

    return 1;
}

/// @brief Destroy platform-specific window resources for Win32.
/// @details Destroys Win32 window, deletes device contexts and DIB section,
///          and frees platform data.  Safe to call even if init failed.
///
/// @param win Pointer to the ViperGFX window structure
///
/// @pre  win != NULL
/// @post platform_data freed and set to NULL
/// @post Win32 window destroyed (if it existed)
void vgfx_platform_destroy_window(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;

    /* Delete DIB section */
    if (w32->hbmp)
    {
        DeleteObject(w32->hbmp);
        w32->hbmp = NULL;
        w32->dib_pixels = NULL;
    }

    /* Delete memory DC */
    if (w32->memdc)
    {
        DeleteDC(w32->memdc);
        w32->memdc = NULL;
    }

    /* Release window DC */
    if (w32->hdc && w32->hwnd)
    {
        ReleaseDC(w32->hwnd, w32->hdc);
        w32->hdc = NULL;
    }

    /* Destroy window */
    if (w32->hwnd)
    {
        DestroyWindow(w32->hwnd);
        w32->hwnd = NULL;
    }

    /* Free platform data */
    free(w32);
    win->platform_data = NULL;
}

/// @brief Process pending Win32 messages and translate to ViperGFX events.
/// @details Polls the Win32 message queue in non-blocking mode (PeekMessage
///          with PM_REMOVE).  Messages are translated and dispatched to the
///          window procedure, which enqueues vgfx_event_t.  Also updates
///          win->key_state, win->mouse_x, win->mouse_y, and
///          win->mouse_button_state.
///
/// @param win Pointer to the ViperGFX window structure
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  win->platform_data != NULL
/// @post All pending messages processed and translated
/// @post win->key_state and win->mouse_* updated to reflect current input state
/// @post Corresponding vgfx_event_t enqueued for each message
int vgfx_platform_process_events(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return 0;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
        return 0;

    /* Process all pending messages without blocking */
    MSG msg;
    while (PeekMessageW(&msg, w32->hwnd, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 1;
}

/// @brief Present (blit) the framebuffer to the Win32 window.
/// @details Copies the ViperGFX framebuffer (win->pixels, RGBA format) to the
///          DIB section (BGRA format) with pixel format conversion, then blits
///          the DIB to the window using BitBlt.
///
/// @param win Pointer to the ViperGFX window structure
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  win->pixels != NULL (framebuffer valid)
/// @pre  win->platform_data != NULL
/// @post Framebuffer contents visible in Win32 window
///
/// @details Pixel format conversion:
///            - Source (win->pixels): RGBA (Red, Green, Blue, Alpha)
///            - Destination (DIB): BGRA (Blue, Green, Red, Alpha)
///            - Conversion swaps R and B channels
int vgfx_platform_present(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return 0;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd || !w32->hdc || !w32->memdc || !w32->dib_pixels)
        return 0;

    /* Copy framebuffer to DIB with RGBA→BGRA conversion.
     * Process 4 pixels per iteration: load as uint32_t (little-endian layout:
     * bytes [R,G,B,A] → value 0xAABBGGRR), swap R↔B via bitmask, store.
     * The remaining 0–3 pixels are handled by the scalar tail loop. */
    const uint8_t *src = win->pixels;
    uint8_t *dst = (uint8_t *)w32->dib_pixels;
    int pixel_count = win->width * win->height;
    int batch = pixel_count & ~3; /* round down to nearest multiple of 4 */

    for (int i = 0; i < batch; i += 4, src += 16, dst += 16)
    {
        uint32_t p0, p1, p2, p3;
        memcpy(&p0, src,     4);
        memcpy(&p1, src + 4, 4);
        memcpy(&p2, src + 8, 4);
        memcpy(&p3, src + 12, 4);
        /* Swap R (bits 7:0) and B (bits 23:16), preserve G and A */
        p0 = (p0 & 0xFF00FF00u) | ((p0 >> 16) & 0xFFu) | ((p0 & 0xFFu) << 16);
        p1 = (p1 & 0xFF00FF00u) | ((p1 >> 16) & 0xFFu) | ((p1 & 0xFFu) << 16);
        p2 = (p2 & 0xFF00FF00u) | ((p2 >> 16) & 0xFFu) | ((p2 & 0xFFu) << 16);
        p3 = (p3 & 0xFF00FF00u) | ((p3 >> 16) & 0xFFu) | ((p3 & 0xFFu) << 16);
        memcpy(dst,      &p0, 4);
        memcpy(dst + 4,  &p1, 4);
        memcpy(dst + 8,  &p2, 4);
        memcpy(dst + 12, &p3, 4);
    }
    /* Scalar tail: handle remaining 0-3 pixels */
    for (int i = batch; i < pixel_count; i++, src += 4, dst += 4)
    {
        dst[0] = src[2]; /* B */
        dst[1] = src[1]; /* G */
        dst[2] = src[0]; /* R */
        dst[3] = src[3]; /* A */
    }

    /* Blit from memory DC (physical pixels) to window DC (logical DIP units).
     * With system DPI awareness the window DC coordinate space is in DIP;
     * StretchBlt maps the physical-size DIB (win->width × win->height pixels)
     * into the logical window rect (w32->width × w32->height DIP).  On a
     * 2× HiDPI display this renders 1 DIB pixel per physical screen pixel. */
    if (!StretchBlt(w32->hdc,    /* Destination DC (window, DIP coords) */
                    0, 0,
                    w32->width,  /* Destination width in DIP */
                    w32->height, /* Destination height in DIP */
                    w32->memdc,  /* Source DC (physical DIB) */
                    0, 0,
                    win->width,  /* Source width in physical pixels */
                    win->height, /* Source height in physical pixels */
                    SRCCOPY))
    {
        return 0;
    }

    return 1;
}

/// @brief Get current high-resolution timestamp in milliseconds.
/// @details Returns a monotonic timestamp using QueryPerformanceCounter with
///          millisecond precision.  Never decreases, used for frame timing.
///
/// @return Milliseconds since arbitrary epoch (monotonic)
///
/// @post Return value >= previous calls within the same process
int64_t vgfx_platform_now_ms(void)
{
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (int64_t)((counter.QuadPart * 1000) / freq.QuadPart);
}

/// @brief Sleep for the specified duration in milliseconds.
/// @details Uses Win32 Sleep() function.  If ms <= 0, returns immediately
///          without sleeping.  Used for FPS limiting.
///
/// @param ms Duration to sleep in milliseconds
void vgfx_platform_sleep_ms(int32_t ms)
{
    if (ms > 0)
    {
        Sleep((DWORD)ms);
    }
}

//===----------------------------------------------------------------------===//
// Clipboard Operations
//===----------------------------------------------------------------------===//

/// @brief Check if the clipboard contains data in the specified format.
/// @param format Clipboard format to check for
/// @return 1 if data is available, 0 otherwise
int vgfx_clipboard_has_format(vgfx_clipboard_format_t format)
{
    switch (format)
    {
        case VGFX_CLIPBOARD_TEXT:
            return IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                   IsClipboardFormatAvailable(CF_TEXT);
        case VGFX_CLIPBOARD_HTML:
            // HTML format is registered dynamically
            {
                UINT cf_html = RegisterClipboardFormatW(L"HTML Format");
                return cf_html ? IsClipboardFormatAvailable(cf_html) : 0;
            }
        case VGFX_CLIPBOARD_IMAGE:
            return IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB);
        case VGFX_CLIPBOARD_FILES:
            return IsClipboardFormatAvailable(CF_HDROP);
        default:
            return 0;
    }
}

/// @brief Get text from the clipboard.
/// @details Returns a malloc'd UTF-8 string containing the clipboard text.
///          The caller is responsible for freeing the returned string.
/// @return Clipboard text (caller must free), or NULL if not available
char *vgfx_clipboard_get_text(void)
{
    if (!OpenClipboard(NULL))
        return NULL;

    char *result = NULL;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);

    if (hData)
    {
        WCHAR *wstr = (WCHAR *)GlobalLock(hData);
        if (wstr)
        {
            /* Convert UTF-16 to UTF-8 */
            int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
            if (len > 0)
            {
                result = (char *)malloc(len);
                if (result)
                {
                    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result, len, NULL, NULL);
                }
            }
            GlobalUnlock(hData);
        }
    }

    CloseClipboard();
    return result;
}

/// @brief Set text to the clipboard.
/// @details Copies the specified UTF-8 string to the system clipboard.
/// @param text Text to copy (NULL clears text from clipboard)
void vgfx_clipboard_set_text(const char *text)
{
    if (!OpenClipboard(NULL))
        return;

    EmptyClipboard();

    if (text)
    {
        /* Convert UTF-8 to UTF-16 */
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
        if (wlen > 0)
        {
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(WCHAR));
            if (hMem)
            {
                WCHAR *wstr = (WCHAR *)GlobalLock(hMem);
                if (wstr)
                {
                    MultiByteToWideChar(CP_UTF8, 0, text, -1, wstr, wlen);
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
                else
                {
                    GlobalFree(hMem);
                }
            }
        }
    }

    CloseClipboard();
}

/// @brief Clear all clipboard contents.
void vgfx_clipboard_clear(void)
{
    if (OpenClipboard(NULL))
    {
        EmptyClipboard();
        CloseClipboard();
    }
}

//===----------------------------------------------------------------------===//
// Window Title and Fullscreen
//===----------------------------------------------------------------------===//

/// @brief Saved window state for restoring from fullscreen.
typedef struct
{
    DWORD style;
    DWORD exStyle;
    RECT rect;
    int is_fullscreen;
} vgfx_win32_saved_state;

/// @brief Global saved window state (one window at a time for simplicity).
static vgfx_win32_saved_state g_saved_state = {0};

/// @brief Set the window title.
/// @details Updates the Win32 window's title bar text using SetWindowTextW.
///
/// @param win   Pointer to the window structure
/// @param title New title string (UTF-8)
void vgfx_platform_set_title(struct vgfx_window *win, const char *title)
{
    if (!win || !win->platform_data || !title)
        return;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
        return;

    /* Convert UTF-8 to UTF-16 */
    WCHAR *wtitle = utf8_to_utf16(title);
    if (wtitle)
    {
        SetWindowTextW(w32->hwnd, wtitle);
        free(wtitle);
    }
}

/// @brief Set the window to fullscreen or windowed mode.
/// @details Uses the borderless fullscreen approach: removes window decorations
///          and resizes the window to cover the entire screen. The window state
///          is saved to restore later when exiting fullscreen.
///
/// @param win        Pointer to the window structure
/// @param fullscreen 1 for fullscreen, 0 for windowed
/// @return 1 on success, 0 on failure
int vgfx_platform_set_fullscreen(struct vgfx_window *win, int fullscreen)
{
    if (!win || !win->platform_data)
        return 0;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
        return 0;

    if (fullscreen && !g_saved_state.is_fullscreen)
    {
        /* Save current window state */
        g_saved_state.style = GetWindowLong(w32->hwnd, GWL_STYLE);
        g_saved_state.exStyle = GetWindowLong(w32->hwnd, GWL_EXSTYLE);
        GetWindowRect(w32->hwnd, &g_saved_state.rect);
        g_saved_state.is_fullscreen = 1;

        /* Get monitor info for the monitor containing this window */
        HMONITOR hMonitor = MonitorFromWindow(w32->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(hMonitor, &mi);

        /* Remove window decorations and maximize to monitor size */
        SetWindowLong(w32->hwnd, GWL_STYLE, g_saved_state.style & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowLong(w32->hwnd,
                      GWL_EXSTYLE,
                      g_saved_state.exStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                                WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

        SetWindowPos(w32->hwnd,
                     HWND_TOP,
                     mi.rcMonitor.left,
                     mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    else if (!fullscreen && g_saved_state.is_fullscreen)
    {
        /* Restore previous window state */
        SetWindowLong(w32->hwnd, GWL_STYLE, g_saved_state.style);
        SetWindowLong(w32->hwnd, GWL_EXSTYLE, g_saved_state.exStyle);

        SetWindowPos(w32->hwnd,
                     NULL,
                     g_saved_state.rect.left,
                     g_saved_state.rect.top,
                     g_saved_state.rect.right - g_saved_state.rect.left,
                     g_saved_state.rect.bottom - g_saved_state.rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

        g_saved_state.is_fullscreen = 0;
    }

    return 1;
}

/// @brief Check if the window is in fullscreen mode.
/// @param win Pointer to the window structure
/// @return 1 if fullscreen, 0 if windowed
int vgfx_platform_is_fullscreen(struct vgfx_window *win)
{
    (void)win;
    return g_saved_state.is_fullscreen;
}

/// @brief Minimize (iconify) the window.
void vgfx_platform_minimize(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        ShowWindow(w32->hwnd, SW_MINIMIZE);
}

/// @brief Maximize the window.
void vgfx_platform_maximize(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        ShowWindow(w32->hwnd, SW_MAXIMIZE);
}

/// @brief Restore the window from minimized or maximized state.
void vgfx_platform_restore(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        ShowWindow(w32->hwnd, SW_RESTORE);
}

/// @brief Return 1 if the window is minimized (iconic), 0 otherwise.
int32_t vgfx_platform_is_minimized(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return 0;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    return (w32->hwnd && IsIconic(w32->hwnd)) ? 1 : 0;
}

/// @brief Return 1 if the window is maximized (zoomed), 0 otherwise.
int32_t vgfx_platform_is_maximized(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return 0;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    return (w32->hwnd && IsZoomed(w32->hwnd)) ? 1 : 0;
}

/// @brief Get the window's top-left screen position.
void vgfx_platform_get_position(struct vgfx_window *win, int32_t *x, int32_t *y)
{
    if (!win || !win->platform_data || !x || !y)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
    {
        *x = 0;
        *y = 0;
        return;
    }
    RECT r = {0};
    GetWindowRect(w32->hwnd, &r);
    *x = (int32_t)r.left;
    *y = (int32_t)r.top;
}

/// @brief Move the window to the given screen position.
void vgfx_platform_set_position(struct vgfx_window *win, int32_t x, int32_t y)
{
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        SetWindowPos(w32->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/// @brief Bring the window to the foreground and give it focus.
void vgfx_platform_focus(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        SetForegroundWindow(w32->hwnd);
}

/// @brief Return 1 if the window currently has keyboard focus.
int32_t vgfx_platform_is_focused(struct vgfx_window *win)
{
    return (win && win->is_focused) ? 1 : 0;
}

/// @brief Set whether the close button dismisses the window.
void vgfx_platform_set_prevent_close(struct vgfx_window *win, int32_t prevent)
{
    if (win)
        win->prevent_close = prevent;
}

/// @brief Change the cursor shape for this window.
/// @param type 0=arrow, 1=hand, 2=ibeam, 3=resize_h, 4=resize_v, 5=wait
void vgfx_platform_set_cursor(struct vgfx_window *win, int32_t type)
{
    (void)win;
    static LPCWSTR const s_cursor_ids[] = {
        IDC_ARROW,  /* 0: default */
        IDC_HAND,   /* 1: pointer */
        IDC_IBEAM,  /* 2: text    */
        IDC_SIZEWE, /* 3: resize_h */
        IDC_SIZENS, /* 4: resize_v */
        IDC_WAIT,   /* 5: wait    */
    };
    int idx = (type >= 0 && type < 6) ? type : 0;
    HCURSOR hc = LoadCursorW(NULL, s_cursor_ids[idx]);
    if (hc)
        SetCursor(hc);
}

/// @brief Show or hide the mouse cursor.
void vgfx_platform_set_cursor_visible(struct vgfx_window *win, int32_t visible)
{
    (void)win;
    /* ShowCursor is reference-counted; track the state manually to avoid drift */
    static int32_t s_cursor_visible = 1;
    int want = visible ? 1 : 0;
    if (want == s_cursor_visible)
        return;
    s_cursor_visible = want;
    if (want)
        ShowCursor(TRUE);
    else
        ShowCursor(FALSE);
}

void vgfx_platform_get_monitor_size(struct vgfx_window *win, int32_t *out_w, int32_t *out_h)
{
    (void)win;
    if (out_w)
        *out_w = (int32_t)GetSystemMetrics(SM_CXSCREEN);
    if (out_h)
        *out_h = (int32_t)GetSystemMetrics(SM_CYSCREEN);
}

void vgfx_platform_set_window_size(struct vgfx_window *win, int32_t w, int32_t h)
{
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *data = (vgfx_win32_data *)win->platform_data;
    if (!data->hwnd)
        return;
    SetWindowPos(data->hwnd, NULL, 0, 0, w, h,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

#endif /* _WIN32 */
