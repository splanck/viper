//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/src/vgfx_platform_win32.c
// Purpose: Implement the native Win32 window, input, and framebuffer backend.
// Key invariants:
//   - Windows SDK base types are available before dependent subsystem headers.
//   - Window and IME state is owned by the backend's vgfx_win32_data object.
// Ownership/Lifetime:
//   - Platform data lives from window creation through platform destruction.
//   - Temporary UTF conversion buffers are released by their immediate caller.
// Links: src/lib/graphics/src/vgfx_internal.h, src/lib/graphics/CMakeLists.txt
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
/// @brief Windows Win32 backend implementation for ZannaGFX.
/// @details Uses Win32 GDI and DIB sections to provide window management
///          and framebuffer presentation on Windows systems.

#include "vgfx_internal.h"

#ifdef _WIN32

#include <windows.h>

#include <imm.h>
#include <limits.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windowsx.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

//===----------------------------------------------------------------------===//
// Platform Data Structure
//===----------------------------------------------------------------------===//

/// @brief Platform-specific data for Win32 windows.
/// @details Allocated and owned by the platform backend.  Stored in
///          vgfx_window->platform_data.  Contains Win32 HWND, device contexts,
///          DIB section for framebuffer, and cached dimensions.
///
/// @invariant hwnd != NULL implies hdc != NULL && memdc != NULL && hbmp != NULL
typedef struct {
    HINSTANCE hInstance;          ///< Application instance handle
    HWND hwnd;                    ///< Native Win32 window handle
    HDC hdc;                      ///< Device context for window
    HDC memdc;                    ///< Memory DC for off-screen rendering
    HBITMAP hbmp;                 ///< DIB section bitmap handle
    HGDIOBJ old_bitmap;           ///< Original object selected into memdc
    void *dib_pixels;             ///< Pointer to DIB pixel data (BGRA format)
    int dib_width;                ///< Current DIB width in physical pixels
    int dib_height;               ///< Current DIB height in physical pixels
    int width;                    ///< Cached client width in physical pixels
    int height;                   ///< Cached client height in physical pixels
    int close_requested;          ///< 1 if WM_CLOSE received, 0 otherwise
    WCHAR pending_high_surrogate; ///< Pending UTF-16 high surrogate from WM_CHAR
    int ime_active;               ///< 1 between native IME start and commit/cancel boundaries
    int ime_committed;            ///< 1 after result text was emitted for the active session
    DWORD saved_style;            ///< Windowed style saved for fullscreen restore
    DWORD saved_exstyle;          ///< Windowed ex-style saved for fullscreen restore
    RECT saved_rect;              ///< Windowed bounds saved for fullscreen restore
    int is_fullscreen;            ///< 1 if this window is currently fullscreen
    int cursor_type;              ///< Current cursor type for WM_SETCURSOR
} vgfx_win32_data;

//===----------------------------------------------------------------------===//
// Forward Declarations
//===----------------------------------------------------------------------===//

static LRESULT CALLBACK vgfx_win32_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static INIT_ONCE g_vgfx_win32_dpi_awareness_once = INIT_ONCE_STATIC_INIT;
static INIT_ONCE g_vgfx_win32_window_class_once = INIT_ONCE_STATIC_INIT;
static HWND g_vgfx_win32_clipboard_owner = NULL;
static char *g_vgfx_win32_clipboard_text = NULL;

/// @brief Allocate an aligned framebuffer buffer with the Win32 CRT allocator.
/// @details Windows requires `_aligned_malloc` so that the matching
///          `_aligned_free` can release the buffer.  The platform-neutral core
///          calls this adapter instead of using `_WIN32` conditionals.
/// @param alignment Required byte alignment; must be a power of two.
/// @param size Number of bytes requested.
/// @return Aligned allocation on success, or NULL for invalid input/OOM.
void *vgfx_platform_aligned_alloc(size_t alignment, size_t size) {
    if (size == 0)
        return NULL;
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    return _aligned_malloc(size, alignment);
}

/// @brief Free a buffer returned by `vgfx_platform_aligned_alloc()`.
/// @details Uses `_aligned_free`, the required companion for `_aligned_malloc`.
///          Passing NULL is permitted by the CRT.
/// @param ptr Pointer returned by `vgfx_platform_aligned_alloc()`, or NULL.
void vgfx_platform_aligned_free(void *ptr) {
    _aligned_free(ptr);
}

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
static WCHAR *utf8_to_utf16(const char *utf8) {
    if (!utf8)
        return NULL;

    /* Get required buffer size */
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    if (wlen <= 0)
        return NULL;

    /* Allocate buffer */
    WCHAR *wstr = (WCHAR *)malloc(wlen * sizeof(WCHAR));
    if (!wstr)
        return NULL;

    /* Convert */
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wstr, wlen) == 0) {
        free(wstr);
        return NULL;
    }

    return wstr;
}

static int utf16_to_utf8_buffer(const WCHAR *wstr, char *out, size_t out_size) {
    if (!wstr || !out || out_size == 0)
        return 0;

    int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr, -1, NULL, 0, NULL, NULL);
    if (needed <= 0 || (size_t)needed > out_size)
        return 0;

    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr, -1, out, needed, NULL, NULL) == 0)
        return 0;
    return 1;
}

/// @brief Open the Win32 clipboard with a short retry window.
/// @details The clipboard is a process-global resource and transiently fails
///          when another process owns it. Retrying for a few milliseconds makes
///          copy/paste operations more reliable without blocking indefinitely.
/// @return Non-zero when OpenClipboard succeeded.
static int win32_open_clipboard_retry(void) {
    HWND owner = (g_vgfx_win32_clipboard_owner && IsWindow(g_vgfx_win32_clipboard_owner))
                     ? g_vgfx_win32_clipboard_owner
                     : NULL;
    for (int attempt = 0; attempt < 8; attempt++) {
        if (OpenClipboard(owner))
            return 1;
        Sleep(1);
    }
    return 0;
}

/// @brief Store a process-local copy of the last text placed on the clipboard.
/// @details Win32 clipboard writes can be unavailable in headless or locked
///          desktop sessions.  Keeping a fallback preserves in-process
///          copy/paste semantics while system clipboard integration remains the
///          preferred path whenever the OS accepts it.
static void win32_store_clipboard_text(const char *text) {
    char *copy = NULL;
    if (text) {
        size_t len = strlen(text);
        if (len < SIZE_MAX) {
            copy = (char *)malloc(len + 1u);
            if (copy)
                memcpy(copy, text, len + 1u);
        }
    }

    free(g_vgfx_win32_clipboard_text);
    g_vgfx_win32_clipboard_text = copy;
}

/// @brief Return a caller-owned copy of the process-local clipboard fallback.
static char *win32_dup_clipboard_text(void) {
    if (!g_vgfx_win32_clipboard_text)
        return NULL;

    size_t len = strlen(g_vgfx_win32_clipboard_text);
    if (len >= SIZE_MAX)
        return NULL;
    char *copy = (char *)malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, g_vgfx_win32_clipboard_text, len + 1u);
    return copy;
}

static void win32_client_to_physical_mouse(
    struct vgfx_window *win, int32_t client_x, int32_t client_y, int32_t *out_x, int32_t *out_y) {
    (void)win;
    if (out_x)
        *out_x = client_x;
    if (out_y)
        *out_y = client_y;
}

static int32_t win32_public_to_client_coord(const struct vgfx_window *win, int32_t value) {
    return vgfx_internal_scale_up_i32(value, vgfx_internal_coord_scale(win));
}

static int32_t win32_logical_window_to_client_coord(const struct vgfx_window *win, int32_t value) {
    float scale = win ? vgfx_internal_sanitize_scale(win->scale_factor) : 1.0f;
    return vgfx_internal_scale_up_i32(value, scale);
}

static BOOL win32_adjust_window_rect_for_scale(
    const struct vgfx_window *win, LPRECT rect, DWORD style, BOOL has_menu, DWORD exstyle) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL(WINAPI * AdjustWindowRectExForDpiFn)(LPRECT, DWORD, BOOL, DWORD, UINT);
        AdjustWindowRectExForDpiFn fn =
            (AdjustWindowRectExForDpiFn)(void *)GetProcAddress(user32, "AdjustWindowRectExForDpi");
        if (fn) {
            float scale = (win && win->scale_factor >= 1.0f) ? win->scale_factor : 1.0f;
            UINT dpi = (UINT)vgfx_internal_round_scaled(scale * 96.0f);
            if (dpi < 96)
                dpi = 96;
            if (fn(rect, style, has_menu, exstyle, dpi))
                return TRUE;
        }
    }
    return AdjustWindowRectEx(rect, style, has_menu, exstyle);
}

static HCURSOR win32_cursor_handle(int32_t type) {
    static LPCTSTR const s_cursor_ids[] = {
        IDC_ARROW,  /* 0: default */
        IDC_HAND,   /* 1: pointer */
        IDC_IBEAM,  /* 2: text    */
        IDC_SIZEWE, /* 3: resize_h */
        IDC_SIZENS, /* 4: resize_v */
        IDC_WAIT,   /* 5: wait    */
    };
    int idx = (type >= 0 && type < 6) ? type : 0;
    return LoadCursor(NULL, s_cursor_ids[idx]);
}

static void win32_apply_cursor(vgfx_win32_data *w32) {
    if (!w32)
        return;
    HCURSOR hc = win32_cursor_handle(w32->cursor_type);
    if (hc)
        SetCursor(hc);
}

/// @brief Declare process DPI awareness once when the Win32 backend first needs DPI.
/// @details Newer Windows SDKs expose SetProcessDpiAwarenessContext dynamically.
///          Calling it once avoids repeated process-wide state changes while
///          preserving compatibility with older user32.dll versions.
/// @param init_once Windows one-time initialization token.
/// @param parameter Unused.
/// @param context Unused.
/// @return TRUE so InitOnceExecuteOnce records completion.
static BOOL CALLBACK win32_set_dpi_awareness_once(PINIT_ONCE init_once,
                                                  PVOID parameter,
                                                  PVOID *context) {
    (void)init_once;
    (void)parameter;
    (void)context;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL(WINAPI * SPDA_fn)(HANDLE);
        SPDA_fn fn = (SPDA_fn)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fn)
            fn((HANDLE)(intptr_t)(-2)); /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */
    }
    return TRUE;
}

/// @brief Load an application icon placed beside the running executable.
/// @details The convention `<executable-basename>.ico` lets packaged Zanna applications carry
///          their own identity without adding icon concerns to the runtime C ABI.  Zanna Studio is
///          installed as `zannastudio.exe` plus `zannastudio.ico`; other applications fall back to the
///          standard Windows application icon when no adjacent icon is present.
static HICON win32_load_adjacent_application_icon(int width, int height) {
    WCHAR path[32768];
    DWORD length = GetModuleFileNameW(NULL, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    if (length == 0 || length >= (DWORD)(sizeof(path) / sizeof(path[0]) - 5))
        return NULL;

    WCHAR *filename = path;
    WCHAR *extension = NULL;
    for (WCHAR *cursor = path; *cursor; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            filename = cursor + 1;
            extension = NULL;
        } else if (*cursor == L'.') {
            extension = cursor;
        }
    }
    if (!extension || extension < filename)
        extension = path + length;
    extension[0] = L'.';
    extension[1] = L'i';
    extension[2] = L'c';
    extension[3] = L'o';
    extension[4] = L'\0';

    return (HICON)LoadImageW(
        NULL, path, IMAGE_ICON, width, height, LR_LOADFROMFILE | LR_DEFAULTSIZE);
}

/// @brief Register the ZannaGFX Win32 window class exactly once per process.
/// @details Multiple windows may be created from different threads.  This
///          callback is executed by InitOnceExecuteOnce so RegisterClassExW is
///          not raced by a plain static flag.  ERROR_CLASS_ALREADY_EXISTS is
///          treated as success to coexist with already-registered compatible
///          classes in the same process.
/// @param init_once Windows one-time initialization token.
/// @param parameter HINSTANCE to associate with the window class.
/// @param context Unused.
/// @return TRUE on successful registration or already-registered class.
static BOOL CALLBACK win32_register_window_class_once(PINIT_ONCE init_once,
                                                      PVOID parameter,
                                                      PVOID *context) {
    (void)init_once;
    (void)context;
    HINSTANCE hInstance = (HINSTANCE)parameter;
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = vgfx_win32_wndproc;
    wc.hInstance = hInstance;
    wc.hIcon = win32_load_adjacent_application_icon(GetSystemMetrics(SM_CXICON),
                                                    GetSystemMetrics(SM_CYICON));
    wc.hIconSm = win32_load_adjacent_application_icon(GetSystemMetrics(SM_CXSMICON),
                                                      GetSystemMetrics(SM_CYSMICON));
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    if (!wc.hIconSm)
        wc.hIconSm = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"ZannaGFXClass";

    if (RegisterClassExW(&wc))
        return TRUE;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? TRUE : FALSE;
}

static int win32_recreate_dib(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->memdc)
        return 0;

    if (w32->hbmp) {
        if (w32->old_bitmap)
            SelectObject(w32->memdc, w32->old_bitmap);
        DeleteObject(w32->hbmp);
        w32->hbmp = NULL;
        w32->dib_pixels = NULL;
        w32->dib_width = 0;
        w32->dib_height = 0;
    }

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = win->width;
    bmi.bmiHeader.biHeight = -win->height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    w32->hbmp = CreateDIBSection(w32->memdc, &bmi, DIB_RGB_COLORS, &w32->dib_pixels, NULL, 0);
    if (!w32->hbmp || !w32->dib_pixels) {
        w32->hbmp = NULL;
        w32->dib_pixels = NULL;
        w32->dib_width = 0;
        w32->dib_height = 0;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create Win32 DIB section");
        return 0;
    }

    HGDIOBJ previous = SelectObject(w32->memdc, w32->hbmp);
    if (!w32->old_bitmap)
        w32->old_bitmap = previous;
    w32->dib_width = win->width;
    w32->dib_height = win->height;
    return 1;
}

static int win32_create_dib_for_size(
    vgfx_win32_data *w32, int width, int height, HBITMAP *out_hbmp, void **out_pixels) {
    if (!w32 || !w32->memdc || width <= 0 || height <= 0 || !out_hbmp || !out_pixels)
        return 0;

    *out_hbmp = NULL;
    *out_pixels = NULL;

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *pixels = NULL;
    HBITMAP hbmp = CreateDIBSection(w32->memdc, &bmi, DIB_RGB_COLORS, &pixels, NULL, 0);
    if (!hbmp || !pixels) {
        if (hbmp)
            DeleteObject(hbmp);
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create Win32 DIB section");
        return 0;
    }

    *out_hbmp = hbmp;
    *out_pixels = pixels;
    return 1;
}

static int win32_resize_backing_store(struct vgfx_window *win,
                                      int client_w,
                                      int client_h,
                                      int64_t timestamp) {
    if (!win || !win->platform_data || client_w <= 0 || client_h <= 0)
        return 0;
    if (client_w > VGFX_MAX_WIDTH || client_h > VGFX_MAX_HEIGHT) {
        vgfx_internal_set_error(VGFX_ERR_INVALID_PARAM, "Win32 resize exceeds framebuffer limits");
        return 0;
    }

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    int phys_w = client_w;
    int phys_h = client_h;

    if (client_w == w32->width && client_h == w32->height && phys_w == win->width &&
        phys_h == win->height && w32->memdc && w32->hdc && w32->hbmp && w32->dib_pixels) {
        return 1;
    }

    HBITMAP new_hbmp = NULL;
    void *new_pixels = NULL;
    if (w32->memdc && !win32_create_dib_for_size(w32, phys_w, phys_h, &new_hbmp, &new_pixels))
        return 0;

    HGDIOBJ previous_bitmap = NULL;
    HBITMAP old_hbmp = w32->hbmp;
    if (new_hbmp) {
        previous_bitmap = SelectObject(w32->memdc, new_hbmp);
        if (!previous_bitmap || previous_bitmap == HGDI_ERROR) {
            DeleteObject(new_hbmp);
            vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to select resized Win32 DIB");
            return 0;
        }
        if (!w32->old_bitmap)
            w32->old_bitmap = previous_bitmap;
    }

    if (!vgfx_internal_resize_framebuffer(win, phys_w, phys_h)) {
        if (new_hbmp) {
            SelectObject(w32->memdc, previous_bitmap);
            DeleteObject(new_hbmp);
        }
        return 0;
    }

    if (new_hbmp) {
        w32->hbmp = new_hbmp;
        w32->dib_pixels = new_pixels;
        w32->dib_width = phys_w;
        w32->dib_height = phys_h;
        if (old_hbmp)
            DeleteObject(old_hbmp);
    }

    w32->width = client_w;
    w32->height = client_h;

    if (w32->memdc && w32->hdc) {
        vgfx_event_t event = {0};
        vgfx_internal_init_resize_event(&event, win, timestamp, phys_w, phys_h);
        vgfx_internal_enqueue_event(win, &event);
    }
    return 1;
}

//===----------------------------------------------------------------------===//
// Key Code Translation
//===----------------------------------------------------------------------===//

/// @brief Translate Win32 virtual key code to vgfx_key_t.
/// @details Maps VK_* constants to ZannaGFX key codes.  Handles A-Z, 0-9,
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
static vgfx_key_t translate_vk(WPARAM vk) {
    /* Letters A-Z (VK_A = 0x41 = 'A') */
    if (vk >= 'A' && vk <= 'Z') {
        return (vgfx_key_t)vk;
    }

    /* Digits 0-9 (VK_0 = 0x30 = '0') */
    if (vk >= '0' && vk <= '9') {
        return (vgfx_key_t)vk;
    }

    /* Special keys */
    switch (vk) {
        case VK_SPACE:
            return VGFX_KEY_SPACE;
        case VK_RETURN:
            return VGFX_KEY_ENTER;
        case VK_ESCAPE:
            return VGFX_KEY_ESCAPE;
        case VK_BACK:
            return VGFX_KEY_BACKSPACE;
        case VK_DELETE:
            return VGFX_KEY_DELETE;
        case VK_TAB:
            return VGFX_KEY_TAB;
        case VK_HOME:
            return VGFX_KEY_HOME;
        case VK_END:
            return VGFX_KEY_END;
        case VK_PRIOR:
            return VGFX_KEY_PAGE_UP;
        case VK_NEXT:
            return VGFX_KEY_PAGE_DOWN;
        case VK_LEFT:
            return VGFX_KEY_LEFT;
        case VK_RIGHT:
            return VGFX_KEY_RIGHT;
        case VK_UP:
            return VGFX_KEY_UP;
        case VK_DOWN:
            return VGFX_KEY_DOWN;
        /* Punctuation / OEM keys. Win32 delivers these as VK_OEM_* virtual codes that
           are NOT their ASCII values, so map each to the ASCII of its unshifted
           character — vgfx_key_t is ASCII-based. This makes symbol-based shortcuts
           (e.g. Ctrl+'=' / Ctrl+'-' for zoom in/out) work. */
        case VK_OEM_PLUS:
            return (vgfx_key_t)'=';
        case VK_OEM_MINUS:
            return (vgfx_key_t)'-';
        case VK_OEM_COMMA:
            return (vgfx_key_t)',';
        case VK_OEM_PERIOD:
            return (vgfx_key_t)'.';
        case VK_OEM_1:
            return (vgfx_key_t)';';
        case VK_OEM_2:
            return (vgfx_key_t)'/';
        case VK_OEM_3:
            return (vgfx_key_t)'`';
        case VK_OEM_4:
            return (vgfx_key_t)'[';
        case VK_OEM_5:
            return (vgfx_key_t)'\\';
        case VK_OEM_6:
            return (vgfx_key_t)']';
        case VK_OEM_7:
            return (vgfx_key_t)'\'';
        /* Numpad +/- map to the zoom shortcut keys ('='/'-') as a convenience,
           matching the common editor/browser behaviour for Ctrl+numpad zoom. */
        case VK_ADD:
            return (vgfx_key_t)'=';
        case VK_SUBTRACT:
            return (vgfx_key_t)'-';
        default:
            return VGFX_KEY_UNKNOWN;
    }
}

static int win32_modifiers(void) {
    int mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        mods |= VGFX_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        mods |= VGFX_MOD_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000)
        mods |= VGFX_MOD_ALT;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
        mods |= VGFX_MOD_CMD;
    return mods;
}

/// @brief Count Unicode scalar/codepoint positions in a bounded UTF-16 sequence.
/// @details Valid surrogate pairs count once. Lone surrogates count once as malformed platform
///          input, matching the deterministic replacement behavior used by later UTF-8 conversion.
/// @param text Borrowed UTF-16 code units; may be NULL only when @p unit_count is zero.
/// @param unit_count Number of readable UTF-16 code units.
/// @return Unicode-codepoint count represented by the prefix.
static size_t win32_utf16_codepoint_count(const WCHAR *text, size_t unit_count) {
    if (!text && unit_count != 0)
        return 0;
    size_t count = 0;
    for (size_t index = 0; index < unit_count; index++, count++) {
        WCHAR high = text[index];
        if (high >= 0xD800 && high <= 0xDBFF && index + 1u < unit_count) {
            WCHAR low = text[index + 1u];
            if (low >= 0xDC00 && low <= 0xDFFF)
                index++;
        }
    }
    return count;
}

/// @brief Resolve the selected/target segment in an IMM32 preedit string.
/// @details IMM32 exposes one attribute byte per UTF-16 code unit. A contiguous target-converted
///          or target-not-converted segment becomes the visible preedit selection. If no target
///          exists, `GCS_CURSORPOS` becomes a zero-length caret. Results are converted from UTF-16
///          units to the ZannaGFX event contract's Unicode-codepoint offsets.
/// @param context Active IMM32 input context.
/// @param text Borrowed preedit UTF-16 code units.
/// @param unit_count Number of readable code units in @p text.
/// @param out_start Receives preedit selection/caret start in codepoints.
/// @param out_length Receives selected preedit codepoint count.
static void win32_ime_selection(
    HIMC context, const WCHAR *text, size_t unit_count, int32_t *out_start, int32_t *out_length) {
    if (!out_start || !out_length)
        return;
    *out_start = 0;
    *out_length = 0;

    size_t target_start = unit_count;
    size_t target_end = unit_count;
    LONG attribute_bytes = ImmGetCompositionStringW(context, GCS_COMPATTR, NULL, 0);
    if (attribute_bytes > 0) {
        BYTE *attributes = (BYTE *)malloc((size_t)attribute_bytes);
        if (attributes &&
            ImmGetCompositionStringW(context, GCS_COMPATTR, attributes, (DWORD)attribute_bytes) ==
                attribute_bytes) {
            size_t available =
                (size_t)attribute_bytes < unit_count ? (size_t)attribute_bytes : unit_count;
            for (size_t index = 0; index < available; index++) {
                BYTE attribute = attributes[index];
                if (attribute != ATTR_TARGET_CONVERTED && attribute != ATTR_TARGET_NOTCONVERTED)
                    continue;
                if (target_start == unit_count)
                    target_start = index;
                target_end = index + 1u;
            }
        }
        free(attributes);
    }

    if (target_start == unit_count) {
        LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, NULL, 0);
        target_start = cursor > 0 ? (size_t)cursor : 0;
        if (target_start > unit_count)
            target_start = unit_count;
        target_end = target_start;
    }

    size_t start_codepoints = win32_utf16_codepoint_count(text, target_start);
    size_t end_codepoints = win32_utf16_codepoint_count(text, target_end);
    if (start_codepoints > (size_t)INT32_MAX)
        start_codepoints = (size_t)INT32_MAX;
    if (end_codepoints > (size_t)INT32_MAX)
        end_codepoints = (size_t)INT32_MAX;
    *out_start = (int32_t)start_codepoints;
    *out_length =
        end_codepoints >= start_codepoints ? (int32_t)(end_codepoints - start_codepoints) : 0;
}

/// @brief Copy an IMM32 composition/result string into one queued ZannaGFX event.
/// @details Retrieves a bounded native UTF-16 value, converts it to UTF-8 using Win32's Unicode
///          conversion API, derives target selection for preedit updates, and queues the complete
///          value-type lifecycle event. Allocation or native conversion failure records an event
///          overflow instead of emitting partially initialized text.
/// @param win Window receiving the native IME event.
/// @param context Active IMM32 input context borrowed for this call.
/// @param type COMPOSITION_UPDATE for `GCS_COMPSTR` or COMPOSITION_COMMIT for `GCS_RESULTSTR`.
/// @param string_index IMM32 string selector.
/// @param timestamp Monotonic event timestamp in milliseconds.
/// @return One when a lifecycle event was enqueued; otherwise zero.
static int win32_enqueue_ime_string(struct vgfx_window *win,
                                    HIMC context,
                                    vgfx_event_type_t type,
                                    DWORD string_index,
                                    int64_t timestamp) {
    if (!win || !context)
        return 0;
    LONG byte_count = ImmGetCompositionStringW(context, string_index, NULL, 0);
    if (byte_count < 0 || ((size_t)byte_count % sizeof(WCHAR)) != 0)
        return 0;
    size_t unit_count = (size_t)byte_count / sizeof(WCHAR);
    if (unit_count > (size_t)INT_MAX)
        return 0;

    WCHAR *wide = (WCHAR *)calloc(unit_count + 1u, sizeof(WCHAR));
    if (!wide) {
        vgfx_internal_note_event_overflow(win);
        return 0;
    }
    if (byte_count > 0 &&
        ImmGetCompositionStringW(context, string_index, wide, (DWORD)byte_count) != byte_count) {
        free(wide);
        return 0;
    }

    int utf8_length =
        unit_count == 0
            ? 0
            : WideCharToMultiByte(
                  CP_UTF8, WC_ERR_INVALID_CHARS, wide, (int)unit_count, NULL, 0, NULL, NULL);
    if (utf8_length <= 0 && unit_count > 0) {
        utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide, (int)unit_count, NULL, 0, NULL, NULL);
    }
    if (utf8_length < 0) {
        free(wide);
        return 0;
    }
    char *utf8 = (char *)malloc((size_t)utf8_length + 1u);
    if (!utf8) {
        free(wide);
        vgfx_internal_note_event_overflow(win);
        return 0;
    }
    if (utf8_length > 0 &&
        WideCharToMultiByte(CP_UTF8, 0, wide, (int)unit_count, utf8, utf8_length, NULL, NULL) !=
            utf8_length) {
        free(utf8);
        free(wide);
        return 0;
    }
    utf8[utf8_length] = '\0';

    int32_t selection_start = 0;
    int32_t selection_length = 0;
    if (type == VGFX_EVENT_COMPOSITION_UPDATE) {
        win32_ime_selection(context, wide, unit_count, &selection_start, &selection_length);
    }
    vgfx_event_t event;
    int initialized = vgfx_internal_init_composition_event(&event,
                                                           type,
                                                           timestamp,
                                                           utf8,
                                                           (size_t)utf8_length,
                                                           selection_start,
                                                           selection_length,
                                                           -1,
                                                           -1,
                                                           win32_modifiers());
    free(utf8);
    free(wide);
    return initialized ? vgfx_internal_enqueue_event(win, &event) : 0;
}

/// @brief Enqueue a text-free native IME start or cancellation boundary.
/// @param win Window receiving the composition boundary.
/// @param type COMPOSITION_START or COMPOSITION_CANCEL.
/// @param timestamp Monotonic event timestamp in milliseconds.
static void win32_enqueue_ime_boundary(struct vgfx_window *win,
                                       vgfx_event_type_t type,
                                       int64_t timestamp) {
    vgfx_event_t event;
    if (vgfx_internal_init_composition_event(
            &event, type, timestamp, "", 0, 0, 0, -1, -1, win32_modifiers())) {
        vgfx_internal_enqueue_event(win, &event);
    }
}

//===----------------------------------------------------------------------===//
// Window Procedure
//===----------------------------------------------------------------------===//

/// @brief Window procedure for ZannaGFX Win32 windows.
/// @details Processes Win32 messages and translates them to ZannaGFX events.
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
static void win32_apply_cursor_clip(struct vgfx_window *win, int enable);

static LRESULT CALLBACK vgfx_win32_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    /* Retrieve vgfx_window pointer stored in GWLP_USERDATA */
    struct vgfx_window *win = (struct vgfx_window *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (!win) {
        /* Window not fully initialized yet, use default processing */
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    int64_t timestamp = vgfx_platform_now_ms();

    switch (msg) {
        case WM_CLOSE: {
            /* User clicked close button - enqueue CLOSE event but don't destroy window */
            vgfx_internal_event_lock(win);
            int prevent_close = win->prevent_close;
            vgfx_internal_event_unlock(win);
            if (!prevent_close && w32) {
                w32->close_requested = 1;
            }
            if (!prevent_close) {
                vgfx_internal_set_close_requested(win, 1);
            }

            vgfx_event_t event = {.type = VGFX_EVENT_CLOSE, .time_ms = timestamp};
            vgfx_internal_enqueue_event(win, &event);
            return 0; /* Handled (don't call DefWindowProc) */
        }

        case WM_SIZE: {
            int client_w = LOWORD(lparam);
            int client_h = HIWORD(lparam);

            if (!w32 || client_w <= 0 || client_h <= 0)
                return 0;

            (void)win32_resize_backing_store(win, client_w, client_h, timestamp);
            return 0;
        }

        case WM_DPICHANGED: {
            UINT dpi_x = LOWORD(wparam);
            UINT dpi_y = HIWORD(wparam);
            UINT dpi = dpi_x > dpi_y ? dpi_x : dpi_y;
            if (dpi < 96)
                dpi = 96;
            vgfx_internal_refresh_scale_factor(win, (float)dpi / 96.0f);

            RECT *suggested = (RECT *)lparam;
            if (suggested) {
                SetWindowPos(hwnd,
                             NULL,
                             suggested->left,
                             suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (w32 && w32->hwnd) {
                RECT client = {0};
                if (GetClientRect(w32->hwnd, &client)) {
                    int client_w = (int)(client.right - client.left);
                    int client_h = (int)(client.bottom - client.top);
                    (void)win32_resize_backing_store(win, client_w, client_h, timestamp);
                }
            }
            return 0;
        }

        case WM_SETFOCUS: {
            /* Window gained focus */
            vgfx_internal_set_focus_state(win, 1);
            /* Re-confine the cursor while relative (raw) mouse mode is on. */
            if (win->relative_mouse_enabled)
                win32_apply_cursor_clip(win, 1);
            vgfx_event_t event = {.type = VGFX_EVENT_FOCUS_GAINED, .time_ms = timestamp};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_KILLFOCUS: {
            /* Window lost focus */
            vgfx_internal_set_focus_state(win, 0);
            if (w32) {
                w32->pending_high_surrogate = 0;
                if (w32->ime_active)
                    win32_enqueue_ime_boundary(win, VGFX_EVENT_COMPOSITION_CANCEL, timestamp);
                w32->ime_active = 0;
                w32->ime_committed = 0;
            }
            vgfx_internal_clear_input_state(win);
            /* Release the cursor clip so the rest of the desktop is usable
             * while unfocused (re-applied on WM_SETFOCUS). */
            if (win->relative_mouse_enabled)
                win32_apply_cursor_clip(win, 0);
            vgfx_event_t event = {.type = VGFX_EVENT_FOCUS_LOST, .time_ms = timestamp};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_INPUT: {
            /* Raw mouse input for relative (FPS mouse-look) mode. Deltas are
             * unaccelerated hardware counts; injected absolute motion (e.g.
             * SetCursorPos warps) reports MOUSE_MOVE_ABSOLUTE and is skipped. */
            if (!win->relative_mouse_enabled || !win->relative_mouse_native)
                return DefWindowProcW(hwnd, msg, wparam, lparam);

            RAWINPUT raw;
            UINT raw_size = sizeof(raw);
            UINT copied = GetRawInputData(
                (HRAWINPUT)lparam, RID_INPUT, &raw, &raw_size, sizeof(RAWINPUTHEADER));
            if (copied != (UINT)-1 && raw.header.dwType == RIM_TYPEMOUSE &&
                (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                double cs = (double)vgfx_internal_coord_scale(win);
                if (cs <= 0.0)
                    cs = 1.0;
                vgfx_internal_add_relative_delta(
                    win, (double)raw.data.mouse.lLastX / cs, (double)raw.data.mouse.lLastY / cs);
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        case WM_IME_SETCONTEXT:
            /* ZannaGUI renders preedit itself. Keep candidate/status UI enabled
               while suppressing the legacy system composition window. */
            if (wparam)
                lparam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
            return DefWindowProcW(hwnd, msg, wparam, lparam);

        case WM_IME_STARTCOMPOSITION:
            if (w32) {
                if (w32->ime_active)
                    win32_enqueue_ime_boundary(win, VGFX_EVENT_COMPOSITION_CANCEL, timestamp);
                w32->ime_active = 1;
                w32->ime_committed = 0;
                win32_enqueue_ime_boundary(win, VGFX_EVENT_COMPOSITION_START, timestamp);
            }
            return 0;

        case WM_IME_COMPOSITION: {
            if (!w32)
                return 0;
            HIMC context = ImmGetContext(hwnd);
            if (!context)
                return 0;

            if ((lparam & GCS_RESULTSTR) != 0) {
                if (!w32->ime_active)
                    win32_enqueue_ime_boundary(win, VGFX_EVENT_COMPOSITION_START, timestamp);
                if (win32_enqueue_ime_string(
                        win, context, VGFX_EVENT_COMPOSITION_COMMIT, GCS_RESULTSTR, timestamp)) {
                    w32->ime_committed = 1;
                }
                w32->ime_active = 0;
            }

            if ((lparam & (GCS_COMPSTR | GCS_CURSORPOS | GCS_COMPATTR)) != 0) {
                if (!w32->ime_active) {
                    win32_enqueue_ime_boundary(win, VGFX_EVENT_COMPOSITION_START, timestamp);
                    w32->ime_active = 1;
                    w32->ime_committed = 0;
                }
                (void)win32_enqueue_ime_string(
                    win, context, VGFX_EVENT_COMPOSITION_UPDATE, GCS_COMPSTR, timestamp);
            }

            ImmReleaseContext(hwnd, context);
            return 0;
        }

        case WM_IME_ENDCOMPOSITION:
            if (w32) {
                if (w32->ime_active && !w32->ime_committed)
                    win32_enqueue_ime_boundary(win, VGFX_EVENT_COMPOSITION_CANCEL, timestamp);
                w32->ime_active = 0;
                w32->ime_committed = 0;
            }
            return 0;

        case WM_KEYDOWN: {
            /* Key pressed */
            if (w32)
                w32->pending_high_surrogate = 0;
            vgfx_key_t key = translate_vk(wparam);
            if (key != VGFX_KEY_UNKNOWN && key < 512) {
                /* Detect repeat: bit 30 of lparam indicates previous key state */
                int is_repeat = (lparam & (1 << 30)) ? 1 : 0;
                vgfx_internal_set_key_state(win, key, 1);

                vgfx_event_t event = {.type = VGFX_EVENT_KEY_DOWN,
                                      .time_ms = timestamp,
                                      .data.key = {.key = key,
                                                   .is_repeat = is_repeat,
                                                   .modifiers = win32_modifiers()}};
                vgfx_internal_enqueue_event(win, &event);
            }
            return 0;
        }

        case WM_KEYUP: {
            /* Key released */
            vgfx_key_t key = translate_vk(wparam);
            if (key != VGFX_KEY_UNKNOWN && key < 512) {
                vgfx_internal_set_key_state(win, key, 0);

                vgfx_event_t event = {
                    .type = VGFX_EVENT_KEY_UP,
                    .time_ms = timestamp,
                    .data.key = {.key = key, .is_repeat = 0, .modifiers = win32_modifiers()}};
                vgfx_internal_enqueue_event(win, &event);
            }
            return 0;
        }

        case WM_CHAR: {
            uint32_t codepoint = 0;
            WCHAR ch = (WCHAR)wparam;
            if (w32 && ch >= 0xD800 && ch <= 0xDBFF) {
                w32->pending_high_surrogate = ch;
                return 0;
            }
            if (w32 && ch >= 0xDC00 && ch <= 0xDFFF && w32->pending_high_surrogate) {
                codepoint = 0x10000 + ((((uint32_t)w32->pending_high_surrogate - 0xD800) << 10) |
                                       ((uint32_t)ch - 0xDC00));
                w32->pending_high_surrogate = 0;
            } else {
                if (w32)
                    w32->pending_high_surrogate = 0;
                if (ch >= 0xDC00 && ch <= 0xDFFF)
                    codepoint = 0xFFFD;
                else if (ch >= 0xD800 && ch <= 0xDBFF)
                    codepoint = 0xFFFD;
                else
                    codepoint = (uint32_t)ch;
            }

            int modifiers = win32_modifiers();
            if (vgfx_internal_should_emit_text_input(codepoint, modifiers)) {
                vgfx_event_t event = {
                    .type = VGFX_EVENT_TEXT_INPUT,
                    .time_ms = timestamp,
                    .data.text = {.codepoint = codepoint, .modifiers = modifiers}};
                vgfx_internal_enqueue_event(win, &event);
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int32_t x = 0;
            int32_t y = 0;
            win32_client_to_physical_mouse(
                win, (int32_t)(short)LOWORD(lparam), (int32_t)(short)HIWORD(lparam), &x, &y);

            vgfx_internal_set_mouse_position(win, x, y);

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_MOVE,
                .time_ms = timestamp,
                .data.mouse_move = {.x = x, .y = y, .modifiers = win32_modifiers()}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int32_t x = 0;
            int32_t y = 0;
            win32_client_to_physical_mouse(
                win, (int32_t)(short)LOWORD(lparam), (int32_t)(short)HIWORD(lparam), &x, &y);

            vgfx_internal_set_mouse_button_state(win, VGFX_MOUSE_LEFT, 1);
            vgfx_internal_set_mouse_position(win, x, y);

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_DOWN,
                .time_ms = timestamp,
                .data.mouse_button = {
                    .x = x, .y = y, .button = VGFX_MOUSE_LEFT, .modifiers = win32_modifiers()}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_LBUTTONUP: {
            int32_t x = 0;
            int32_t y = 0;
            win32_client_to_physical_mouse(
                win, (int32_t)(short)LOWORD(lparam), (int32_t)(short)HIWORD(lparam), &x, &y);

            vgfx_internal_set_mouse_button_state(win, VGFX_MOUSE_LEFT, 0);
            vgfx_internal_set_mouse_position(win, x, y);

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_UP,
                .time_ms = timestamp,
                .data.mouse_button = {
                    .x = x, .y = y, .button = VGFX_MOUSE_LEFT, .modifiers = win32_modifiers()}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_RBUTTONDOWN: {
            int32_t x = 0;
            int32_t y = 0;
            win32_client_to_physical_mouse(
                win, (int32_t)(short)LOWORD(lparam), (int32_t)(short)HIWORD(lparam), &x, &y);

            vgfx_internal_set_mouse_button_state(win, VGFX_MOUSE_RIGHT, 1);
            vgfx_internal_set_mouse_position(win, x, y);

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_DOWN,
                .time_ms = timestamp,
                .data.mouse_button = {
                    .x = x, .y = y, .button = VGFX_MOUSE_RIGHT, .modifiers = win32_modifiers()}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_RBUTTONUP: {
            int32_t x = 0;
            int32_t y = 0;
            win32_client_to_physical_mouse(
                win, (int32_t)(short)LOWORD(lparam), (int32_t)(short)HIWORD(lparam), &x, &y);

            vgfx_internal_set_mouse_button_state(win, VGFX_MOUSE_RIGHT, 0);
            vgfx_internal_set_mouse_position(win, x, y);

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_UP,
                .time_ms = timestamp,
                .data.mouse_button = {
                    .x = x, .y = y, .button = VGFX_MOUSE_RIGHT, .modifiers = win32_modifiers()}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_MBUTTONDOWN: {
            int32_t x = 0;
            int32_t y = 0;
            win32_client_to_physical_mouse(
                win, (int32_t)(short)LOWORD(lparam), (int32_t)(short)HIWORD(lparam), &x, &y);

            vgfx_internal_set_mouse_button_state(win, VGFX_MOUSE_MIDDLE, 1);
            vgfx_internal_set_mouse_position(win, x, y);

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_DOWN,
                .time_ms = timestamp,
                .data.mouse_button = {
                    .x = x, .y = y, .button = VGFX_MOUSE_MIDDLE, .modifiers = win32_modifiers()}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_MBUTTONUP: {
            int32_t x = 0;
            int32_t y = 0;
            win32_client_to_physical_mouse(
                win, (int32_t)(short)LOWORD(lparam), (int32_t)(short)HIWORD(lparam), &x, &y);

            vgfx_internal_set_mouse_button_state(win, VGFX_MOUSE_MIDDLE, 0);
            vgfx_internal_set_mouse_position(win, x, y);

            vgfx_event_t event = {
                .type = VGFX_EVENT_MOUSE_UP,
                .time_ms = timestamp,
                .data.mouse_button = {
                    .x = x, .y = y, .button = VGFX_MOUSE_MIDDLE, .modifiers = win32_modifiers()}};
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &pt);
            int32_t x = 0;
            int32_t y = 0;
            win32_client_to_physical_mouse(win, (int32_t)pt.x, (int32_t)pt.y, &x, &y);
            vgfx_internal_set_mouse_position(win, x, y);

            float delta = (float)GET_WHEEL_DELTA_WPARAM(wparam) / (float)WHEEL_DELTA;
            vgfx_event_t event = {.type = VGFX_EVENT_SCROLL,
                                  .time_ms = timestamp,
                                  .data.scroll = {.delta_x = 0.0f,
                                                  .delta_y = 0.0f,
                                                  .x = x,
                                                  .y = y,
                                                  .modifiers = win32_modifiers()}};
            if (msg == WM_MOUSEWHEEL)
                event.data.scroll.delta_y = -delta;
            else
                event.data.scroll.delta_x = delta;
            vgfx_internal_enqueue_event(win, &event);
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lparam) == HTCLIENT && w32) {
                win32_apply_cursor(w32);
                return TRUE;
            }
            break;
        }

        case WM_PAINT: {
            /* Window needs redraw - validate to prevent endless loop */
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            /* GPU backends own the visible surface. Letting GDI erase here can
             * flash black between swapchain presents and during startup/resizes. */
            return 1;

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wparam;
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < count; i++) {
                vgfx_event_t event = {0};
                event.type = VGFX_EVENT_FILE_DROP;
                event.time_ms = timestamp;
                UINT wlen = DragQueryFileW(hDrop, i, NULL, 0);
                if ((size_t)wlen > (SIZE_MAX / sizeof(WCHAR)) - 1u) {
                    vgfx_internal_note_event_overflow(win);
                    continue;
                }
                WCHAR *wpath = (WCHAR *)malloc(((size_t)wlen + 1u) * sizeof(WCHAR));
                if (!wpath)
                    continue;
                if (DragQueryFileW(hDrop, i, wpath, wlen + 1u) == wlen &&
                    utf16_to_utf8_buffer(
                        wpath, event.data.file_drop.path, sizeof(event.data.file_drop.path))) {
                    vgfx_internal_enqueue_event(win, &event);
                } else {
                    vgfx_internal_note_event_overflow(win);
                }
                free(wpath);
            }
            DragFinish(hDrop);
            return 0;
        }

        default:
            /* Use default window procedure for unhandled messages */
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
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
///          code compiles against older SDKs. With system awareness active,
///          raw Win32 window, mouse, and WM_SIZE coordinates are physical
///          screen pixels. ZannaGFX keeps its public Canvas coordinates
///          logical by scaling the requested client size and dividing through
///          coord_scale at the public API boundary.
///
/// @note This must be called before any windows are created.
/// @return Scale factor ≥ 1.0
float vgfx_platform_get_display_scale(void) {
    (void)InitOnceExecuteOnce(
        &g_vgfx_win32_dpi_awareness_once, win32_set_dpi_awareness_once, NULL, NULL);

    /* Query the primary monitor's DPI.  With awareness set, this returns the
     * real system DPI rather than the virtualised 96 DPI given to unaware
     * processes. */
    HDC hdc = GetDC(NULL);
    if (!hdc)
        return 1.0f;
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);

    if (dpi < 96)
        dpi = 96; /* clamp against bogus values */

    return (float)dpi / 96.0f;
}

/// @brief Query the primary display's logical dimensions.
/// @details GetSystemMetrics returns physical pixels under our DPI-aware
///          process; divide by the display scale to get vgfx logical units.
/// @return 1 on success, 0 when the metrics are unavailable.
int vgfx_platform_get_display_logical_size(int32_t *out_w, int32_t *out_h) {
    int phys_w = GetSystemMetrics(SM_CXSCREEN);
    int phys_h = GetSystemMetrics(SM_CYSCREEN);
    if (phys_w <= 0 || phys_h <= 0)
        return 0;
    float scale = vgfx_platform_get_display_scale();
    if (scale < 1.0f)
        scale = 1.0f;
    if (out_w)
        *out_w = (int32_t)((float)phys_w / scale);
    if (out_h)
        *out_h = (int32_t)((float)phys_h / scale);
    return 1;
}

/// @brief Initialize platform-specific window resources for Win32.
/// @details Registers window class (once), creates Win32 window, sets up DIB
///          section for framebuffer, and makes window visible.  The DIB section
///          allows direct pixel access for efficient blitting.
///
/// @param win    Pointer to the ZannaGFX window structure (framebuffer already allocated)
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
int vgfx_platform_init_window(struct vgfx_window *win, const vgfx_window_params_t *params) {
    if (!win || !params)
        return 0;

    /* Allocate platform data structure */
    vgfx_win32_data *w32 = (vgfx_win32_data *)calloc(1, sizeof(vgfx_win32_data));
    if (!w32) {
        vgfx_internal_set_error(VGFX_ERR_ALLOC, "Failed to allocate Win32 platform data");
        return 0;
    }

    win->platform_data = w32;
    w32->close_requested = 0;
    w32->width = win->width;
    w32->height = win->height;

    /* Get application instance */
    w32->hInstance = GetModuleHandleW(NULL);

    if (!InitOnceExecuteOnce(&g_vgfx_win32_window_class_once,
                             win32_register_window_class_once,
                             w32->hInstance,
                             NULL)) {
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to register Win32 window class");
        return 0;
    }

    /* Convert UTF-8 title to UTF-16 */
    WCHAR *wtitle = utf8_to_utf16(params->title);
    if (!wtitle) {
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to convert window title");
        return 0;
    }

    /* Determine window style */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (params->resizable) {
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }

    /* Adjust window rect to account for borders/title bar. Win32 client sizes
     * are physical pixels for DPI-aware processes, while params->width/height
     * are logical Canvas units. win->width/height already include the current
     * display scale and therefore match the backing framebuffer. */
    RECT rect = {0, 0, win->width, win->height};
    win32_adjust_window_rect_for_scale(win, &rect, style, FALSE, 0);
    int win_width = rect.right - rect.left;
    int win_height = rect.bottom - rect.top;

    /* Create window */
    w32->hwnd = CreateWindowExW(0,                /* Extended style */
                                L"ZannaGFXClass", /* Class name */
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

    if (!w32->hwnd) {
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create Win32 window");
        return 0;
    }

    /* Store vgfx_window pointer in window user data for WndProc access */
    SetWindowLongPtrW(w32->hwnd, GWLP_USERDATA, (LONG_PTR)win);

    /* Accept file drops */
    DragAcceptFiles(w32->hwnd, TRUE);

    /* Get device context for window */
    w32->hdc = GetDC(w32->hwnd);
    if (!w32->hdc) {
        DestroyWindow(w32->hwnd);
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to get Win32 DC");
        return 0;
    }

    /* Create memory DC for double-buffering */
    w32->memdc = CreateCompatibleDC(w32->hdc);
    if (!w32->memdc) {
        ReleaseDC(w32->hwnd, w32->hdc);
        DestroyWindow(w32->hwnd);
        free(w32);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create memory DC");
        return 0;
    }

    if (!win32_recreate_dib(win)) {
        DeleteDC(w32->memdc);
        ReleaseDC(w32->hwnd, w32->hdc);
        DestroyWindow(w32->hwnd);
        free(w32);
        win->platform_data = NULL;
        return 0;
    }

    /* Show and update window */
    ShowWindow(w32->hwnd, SW_SHOW);
    UpdateWindow(w32->hwnd);
    g_vgfx_win32_clipboard_owner = w32->hwnd;

    return 1;
}

/// @brief Destroy platform-specific window resources for Win32.
/// @details Destroys Win32 window, deletes device contexts and DIB section,
///          and frees platform data.  Safe to call even if init failed.
///
/// @param win Pointer to the ZannaGFX window structure
///
/// @pre  win != NULL
/// @post platform_data freed and set to NULL
/// @post Win32 window destroyed (if it existed)
void vgfx_platform_destroy_window(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;

    /* Delete DIB section */
    if (w32->hbmp) {
        if (w32->old_bitmap)
            SelectObject(w32->memdc, w32->old_bitmap);
        DeleteObject(w32->hbmp);
        w32->hbmp = NULL;
        w32->dib_pixels = NULL;
        w32->dib_width = 0;
        w32->dib_height = 0;
    }

    /* Delete memory DC */
    if (w32->memdc) {
        DeleteDC(w32->memdc);
        w32->memdc = NULL;
    }

    /* Release window DC */
    if (w32->hdc && w32->hwnd) {
        ReleaseDC(w32->hwnd, w32->hdc);
        w32->hdc = NULL;
    }

    /* Destroy window */
    if (w32->hwnd) {
        if (g_vgfx_win32_clipboard_owner == w32->hwnd)
            g_vgfx_win32_clipboard_owner = NULL;
        DestroyWindow(w32->hwnd);
        w32->hwnd = NULL;
    }

    /* Free platform data */
    free(w32);
    win->platform_data = NULL;
}

/// @brief Process pending Win32 messages and translate to ZannaGFX events.
/// @details Polls the Win32 message queue in non-blocking mode (PeekMessage
///          with PM_REMOVE).  Messages are translated and dispatched to the
///          window procedure, which enqueues vgfx_event_t.  Also updates
///          win->key_state, win->mouse_x, win->mouse_y, and
///          win->mouse_button_state.
///
/// @param win Pointer to the ZannaGFX window structure
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  win->platform_data != NULL
/// @post All pending messages processed and translated
/// @post win->key_state and win->mouse_* updated to reflect current input state
/// @post Corresponding vgfx_event_t enqueued for each message
int vgfx_platform_wait_events(struct vgfx_window *win, int32_t timeout_ms) {
    if (!win || !win->platform_data)
        return 0;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
        return 0;
    if (timeout_ms <= 0)
        return 0;

    /* Already have a message? Return without waiting. */
    MSG msg;
    if (PeekMessageW(&msg, w32->hwnd, 0, 0, PM_NOREMOVE))
        return 1;
    /* Block until input arrives on this thread's queue or the timeout elapses.
       Messages are left queued for vgfx_platform_process_events. */
    DWORD result =
        MsgWaitForMultipleObjectsEx(0, NULL, (DWORD)timeout_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    return result != WAIT_TIMEOUT ? 1 : 0;
}

int vgfx_platform_process_events(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
        return 0;

    /* Process all pending messages without blocking */
    MSG msg;
    while (PeekMessageW(&msg, w32->hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 1;
}

/// @brief Present (blit) the framebuffer to the Win32 window.
/// @details Copies the ZannaGFX framebuffer (win->pixels, RGBA format) to the
///          DIB section (BGRA format) with pixel format conversion, then blits
///          the DIB to the window using BitBlt.
///
/// @param win Pointer to the ZannaGFX window structure
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
int vgfx_platform_present(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;
    if (win->skip_software_present)
        return 1;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd || !w32->hdc || !w32->memdc || !w32->dib_pixels)
        return 0;
    if (w32->dib_width != win->width || w32->dib_height != win->height)
        return 0;

    /* Copy framebuffer to DIB with RGBA→BGRA conversion.
     * Process 4 pixels per iteration: load as uint32_t (little-endian layout:
     * bytes [R,G,B,A] → value 0xAABBGGRR), swap R↔B via bitmask, store.
     * The remaining 0–3 pixels are handled by the scalar tail loop. */
    const uint8_t *src = win->pixels;
    uint8_t *dst = (uint8_t *)w32->dib_pixels;
    size_t pixel_count = (size_t)win->width * (size_t)win->height;
    size_t batch = pixel_count & ~(size_t)3; /* round down to nearest multiple of 4 */

    for (size_t i = 0; i < batch; i += 4, src += 16, dst += 16) {
        uint32_t p0, p1, p2, p3;
        memcpy(&p0, src, 4);
        memcpy(&p1, src + 4, 4);
        memcpy(&p2, src + 8, 4);
        memcpy(&p3, src + 12, 4);
        /* Swap R (bits 7:0) and B (bits 23:16), preserve G and A */
        p0 = (p0 & 0xFF00FF00u) | ((p0 >> 16) & 0xFFu) | ((p0 & 0xFFu) << 16);
        p1 = (p1 & 0xFF00FF00u) | ((p1 >> 16) & 0xFFu) | ((p1 & 0xFFu) << 16);
        p2 = (p2 & 0xFF00FF00u) | ((p2 >> 16) & 0xFFu) | ((p2 & 0xFFu) << 16);
        p3 = (p3 & 0xFF00FF00u) | ((p3 >> 16) & 0xFFu) | ((p3 & 0xFFu) << 16);
        memcpy(dst, &p0, 4);
        memcpy(dst + 4, &p1, 4);
        memcpy(dst + 8, &p2, 4);
        memcpy(dst + 12, &p3, 4);
    }
    /* Scalar tail: handle remaining 0-3 pixels */
    for (size_t i = batch; i < pixel_count; i++, src += 4, dst += 4) {
        dst[0] = src[2]; /* B */
        dst[1] = src[1]; /* G */
        dst[2] = src[0]; /* R */
        dst[3] = src[3]; /* A */
    }

    /* Blit from memory DC to the DPI-aware window DC in physical pixels. */
    if (!StretchBlt(w32->hdc, /* Destination DC (window client pixels) */
                    0,
                    0,
                    w32->width,  /* Destination width in physical pixels */
                    w32->height, /* Destination height in physical pixels */
                    w32->memdc,  /* Source DC (physical DIB) */
                    0,
                    0,
                    win->width,  /* Source width in physical pixels */
                    win->height, /* Source height in physical pixels */
                    SRCCOPY)) {
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
int64_t vgfx_platform_now_ms(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    if (freq.QuadPart <= 0)
        return 0;
    long double millis = ((long double)counter.QuadPart * 1000.0L) / (long double)freq.QuadPart;
    if (millis > (long double)INT64_MAX)
        return INT64_MAX;
    return (int64_t)millis;
}

/// @brief Create a waitable timer for one frame-pacing sleep.
/// @details Uses CREATE_WAITABLE_TIMER_HIGH_RESOLUTION when available so frame
///          pacing is not quantized by the default scheduler tick.  The caller
///          owns the returned handle and must close it with CloseHandle().
/// @return A waitable timer handle on success, or NULL when both timer creation
///         attempts fail.
static HANDLE win32_create_sleep_timer(void) {
    HANDLE timer = CreateWaitableTimerExW(
        NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!timer) {
        timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
    }
    return timer;
}

/// @brief Sleep for the specified duration in milliseconds.
/// @details Uses a waitable timer instead of plain Sleep() so 60 FPS frame
///          pacing is not quantized by the default Windows scheduler tick.
///          If ms <= 0, returns immediately without sleeping.
///
/// @param ms Duration to sleep in milliseconds
void vgfx_platform_sleep_ms(int32_t ms) {
    if (ms <= 0)
        return;

    HANDLE timer = win32_create_sleep_timer();
    if (timer) {
        LARGE_INTEGER due_time;
        due_time.QuadPart = -(LONGLONG)ms * 10000LL;
        if (SetWaitableTimer(timer, &due_time, 0, NULL, NULL, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            CloseHandle(timer);
            return;
        }
        CloseHandle(timer);
    }

    Sleep((DWORD)ms);
}

/// @brief Yield the calling thread to another ready Windows thread.
/// @details Used for brief internal spin waits where sleeping for a full
///          scheduler tick would add visible event latency.
void vgfx_platform_yield(void) {
    SwitchToThread();
}

//===----------------------------------------------------------------------===//
// Clipboard Operations
//===----------------------------------------------------------------------===//

/// @brief Check if the clipboard contains data in the specified format.
/// @param format Clipboard format to check for
/// @return 1 if data is available, 0 otherwise
int vgfx_clipboard_has_format(vgfx_clipboard_format_t format) {
    switch (format) {
        case VGFX_CLIPBOARD_TEXT:
            return IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                   IsClipboardFormatAvailable(CF_TEXT) ||
                   (g_vgfx_win32_clipboard_text && g_vgfx_win32_clipboard_text[0] != '\0');
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
char *vgfx_clipboard_get_text(void) {
    if (!win32_open_clipboard_retry())
        return win32_dup_clipboard_text();

    char *result = NULL;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);

    if (hData) {
        WCHAR *wstr = (WCHAR *)GlobalLock(hData);
        if (wstr) {
            /* Convert UTF-16 to UTF-8 */
            int len =
                WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr, -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                result = (char *)malloc(len);
                if (result) {
                    if (WideCharToMultiByte(
                            CP_UTF8, WC_ERR_INVALID_CHARS, wstr, -1, result, len, NULL, NULL) ==
                        0) {
                        free(result);
                        result = NULL;
                    }
                }
            }
            GlobalUnlock(hData);
        }
    }

    CloseClipboard();
    return result ? result : win32_dup_clipboard_text();
}

/// @brief Set text to the clipboard.
/// @details Copies the specified UTF-8 string to the system clipboard.
/// @param text Text to copy (NULL clears text from clipboard)
void vgfx_clipboard_set_text(const char *text) {
    win32_store_clipboard_text(text ? text : "");

    if (!win32_open_clipboard_retry())
        return;

    EmptyClipboard();

    if (text) {
        /* Convert UTF-8 to UTF-16 */
        int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
        if (wlen > 0) {
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wlen * sizeof(WCHAR));
            if (hMem) {
                WCHAR *wstr = (WCHAR *)GlobalLock(hMem);
                if (wstr) {
                    int converted =
                        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wstr, wlen);
                    GlobalUnlock(hMem);
                    if (converted == 0 || !SetClipboardData(CF_UNICODETEXT, hMem))
                        GlobalFree(hMem);
                } else {
                    GlobalFree(hMem);
                }
            }
        }
    }

    CloseClipboard();
}

/// @brief Clear all clipboard contents.
void vgfx_clipboard_clear(void) {
    win32_store_clipboard_text("");

    if (win32_open_clipboard_retry()) {
        EmptyClipboard();
        CloseClipboard();
    }
}

//===----------------------------------------------------------------------===//
// Window Title and Fullscreen
//===----------------------------------------------------------------------===//

/// @brief Set the window title.
/// @details Updates the Win32 window's title bar text using SetWindowTextW.
///
/// @param win   Pointer to the window structure
/// @param title New title string (UTF-8)
void vgfx_platform_set_title(struct vgfx_window *win, const char *title) {
    if (!win || !win->platform_data || !title)
        return;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
        return;

    /* Convert UTF-8 to UTF-16 */
    WCHAR *wtitle = utf8_to_utf16(title);
    if (!wtitle) {
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to convert Win32 window title");
        return;
    }
    if (!SetWindowTextW(w32->hwnd, wtitle)) {
        free(wtitle);
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to set Win32 window title");
        return;
    }
    free(wtitle);
}

/// @brief Set the window to fullscreen or windowed mode.
/// @details Uses the borderless fullscreen approach: removes window decorations
///          and resizes the window to cover the entire screen. The window state
///          is saved to restore later when exiting fullscreen.
///
/// @param win        Pointer to the window structure
/// @param fullscreen 1 for fullscreen, 0 for windowed
/// @return 1 on success, 0 on failure
int vgfx_platform_set_fullscreen(struct vgfx_window *win, int fullscreen) {
    if (!win || !win->platform_data)
        return 0;

    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
        return 0;

    if (fullscreen && !w32->is_fullscreen) {
        /* Save current window state */
        w32->saved_style = (DWORD)GetWindowLong(w32->hwnd, GWL_STYLE);
        w32->saved_exstyle = (DWORD)GetWindowLong(w32->hwnd, GWL_EXSTYLE);
        GetWindowRect(w32->hwnd, &w32->saved_rect);
        w32->is_fullscreen = 1;

        /* Get monitor info for the monitor containing this window */
        HMONITOR hMonitor = MonitorFromWindow(w32->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(hMonitor, &mi);

        /* Remove window decorations and maximize to monitor size */
        SetWindowLong(w32->hwnd, GWL_STYLE, w32->saved_style & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowLong(w32->hwnd,
                      GWL_EXSTYLE,
                      w32->saved_exstyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                             WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

        SetWindowPos(w32->hwnd,
                     HWND_TOP,
                     mi.rcMonitor.left,
                     mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else if (!fullscreen && w32->is_fullscreen) {
        /* Restore previous window state */
        SetWindowLong(w32->hwnd, GWL_STYLE, w32->saved_style);
        SetWindowLong(w32->hwnd, GWL_EXSTYLE, w32->saved_exstyle);

        SetWindowPos(w32->hwnd,
                     NULL,
                     w32->saved_rect.left,
                     w32->saved_rect.top,
                     w32->saved_rect.right - w32->saved_rect.left,
                     w32->saved_rect.bottom - w32->saved_rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

        w32->is_fullscreen = 0;
    }

    return 1;
}

/// @brief Check if the window is in fullscreen mode.
/// @param win Pointer to the window structure
/// @return 1 if fullscreen, 0 if windowed
int vgfx_platform_is_fullscreen(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    return w32->is_fullscreen;
}

/// @brief Minimize (iconify) the window.
void vgfx_platform_minimize(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        ShowWindow(w32->hwnd, SW_MINIMIZE);
}

/// @brief Maximize the window.
void vgfx_platform_maximize(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        ShowWindow(w32->hwnd, SW_MAXIMIZE);
}

/// @brief Restore the window from minimized or maximized state.
void vgfx_platform_restore(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        ShowWindow(w32->hwnd, SW_RESTORE);
}

/// @brief Return 1 if the window is minimized (iconic), 0 otherwise.
int32_t vgfx_platform_is_minimized(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    return (w32->hwnd && IsIconic(w32->hwnd)) ? 1 : 0;
}

/// @brief Return 1 if the window is maximized (zoomed), 0 otherwise.
int32_t vgfx_platform_is_maximized(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    return (w32->hwnd && IsZoomed(w32->hwnd)) ? 1 : 0;
}

/// @brief Get the window's top-left screen position.
void vgfx_platform_get_position(struct vgfx_window *win, int32_t *x, int32_t *y) {
    if (x)
        *x = 0;
    if (y)
        *y = 0;
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
        return;
    RECT r = {0};
    if (!GetWindowRect(w32->hwnd, &r))
        return;
    if (x)
        *x = (int32_t)r.left;
    if (y)
        *y = (int32_t)r.top;
}

/// @brief Move the window to the given screen position.
void vgfx_platform_set_position(struct vgfx_window *win, int32_t x, int32_t y) {
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        SetWindowPos(w32->hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/// @brief Bring the window to the foreground and give it focus.
void vgfx_platform_focus(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd)
        SetForegroundWindow(w32->hwnd);
}

/// @brief Request foreground activation for the application window.
void vgfx_platform_request_foreground(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (w32->hwnd) {
        ShowWindow(w32->hwnd, SW_SHOWNORMAL);
        BringWindowToTop(w32->hwnd);
        SetForegroundWindow(w32->hwnd);
        SetFocus(w32->hwnd);
    }
}

/// @brief Return 1 if the window currently has keyboard focus.
int32_t vgfx_platform_is_focused(struct vgfx_window *win) {
    if (!win)
        return 0;
    vgfx_internal_event_lock(win);
    int32_t focused = win->is_focused;
    vgfx_internal_event_unlock(win);
    return focused ? 1 : 0;
}

/// @brief Set whether the close button dismisses the window.
void vgfx_platform_set_prevent_close(struct vgfx_window *win, int32_t prevent) {
    vgfx_internal_set_prevent_close(win, prevent);
}

/// @brief Change the cursor shape for this window.
/// @param type 0=arrow, 1=hand, 2=ibeam, 3=resize_h, 4=resize_v, 5=wait
void vgfx_platform_set_cursor(struct vgfx_window *win, int32_t type) {
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    w32->cursor_type = (type >= 0 && type < 6) ? type : 0;
    win32_apply_cursor(w32);
}

/// @brief Show or hide the mouse cursor.
void vgfx_platform_set_cursor_visible(struct vgfx_window *win, int32_t visible) {
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

void vgfx_platform_get_monitor_size(struct vgfx_window *win, int32_t *out_w, int32_t *out_h) {
    HMONITOR monitor = NULL;
    if (win && win->platform_data) {
        vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
        if (w32->hwnd)
            monitor = MonitorFromWindow(w32->hwnd, MONITOR_DEFAULTTONEAREST);
    }
    if (!monitor)
        monitor = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO mi = {0};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(monitor, &mi)) {
        if (out_w)
            *out_w = (int32_t)GetSystemMetrics(SM_CXSCREEN);
        if (out_h)
            *out_h = (int32_t)GetSystemMetrics(SM_CYSCREEN);
        return;
    }

    if (out_w)
        *out_w = mi.rcMonitor.right - mi.rcMonitor.left;
    if (out_h)
        *out_h = mi.rcMonitor.bottom - mi.rcMonitor.top;
}

void vgfx_platform_set_window_size(struct vgfx_window *win, int32_t w, int32_t h) {
    if (!win || !win->platform_data)
        return;
    vgfx_win32_data *data = (vgfx_win32_data *)win->platform_data;
    if (!data->hwnd)
        return;
    int32_t client_w = win32_logical_window_to_client_coord(win, w);
    int32_t client_h = win32_logical_window_to_client_coord(win, h);
    if (client_w <= 0 || client_h <= 0 || client_w > VGFX_MAX_WIDTH || client_h > VGFX_MAX_HEIGHT)
        return;
    RECT rect = {0, 0, client_w, client_h};
    DWORD style = (DWORD)GetWindowLong(data->hwnd, GWL_STYLE);
    DWORD exstyle = (DWORD)GetWindowLong(data->hwnd, GWL_EXSTYLE);
    win32_adjust_window_rect_for_scale(win, &rect, style, FALSE, exstyle);
    int64_t window_w = (int64_t)rect.right - (int64_t)rect.left;
    int64_t window_h = (int64_t)rect.bottom - (int64_t)rect.top;
    if (window_w <= 0 || window_h <= 0 || window_w > INT_MAX || window_h > INT_MAX)
        return;
    SetWindowPos(data->hwnd,
                 NULL,
                 0,
                 0,
                 (int)window_w,
                 (int)window_h,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT client = {0};
    if (GetClientRect(data->hwnd, &client)) {
        int client_w = (int)(client.right - client.left);
        int client_h = (int)(client.bottom - client.top);
        (void)win32_resize_backing_store(win, client_w, client_h, vgfx_platform_now_ms());
    }
}

void *vgfx_get_native_display(vgfx_window_t window) {
    (void)window;
    return NULL; /* Windows doesn't have a separate display connection */
}

void *vgfx_get_native_view(vgfx_window_t window) {
    if (!window)
        return NULL;
    vgfx_win32_data *w32 = (vgfx_win32_data *)window->platform_data;
    if (!w32)
        return NULL;
    return (void *)w32->hwnd;
}

void vgfx_platform_warp_cursor(vgfx_window_t window, int32_t x, int32_t y) {
    if (!window || !window->platform_data)
        return;
    vgfx_win32_data *w32 = (vgfx_win32_data *)window->platform_data;
    POINT pt = {(LONG)win32_public_to_client_coord(window, x),
                (LONG)win32_public_to_client_coord(window, y)};
    ClientToScreen(w32->hwnd, &pt);
    SetCursorPos(pt.x, pt.y);
}

/// @brief Confine or release the OS cursor to the window's client rect.
/// @details Used by relative mouse mode: raw WM_INPUT deltas drive look
///          motion, while the (hidden) system cursor is clipped so stray
///          movement can't click a different window or monitor.
static void win32_apply_cursor_clip(struct vgfx_window *win, int enable) {
    if (!win || !win->platform_data) {
        ClipCursor(NULL);
        return;
    }
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!enable || !w32->hwnd) {
        ClipCursor(NULL);
        return;
    }
    RECT client = {0};
    if (!GetClientRect(w32->hwnd, &client)) {
        ClipCursor(NULL);
        return;
    }
    POINT tl = {client.left, client.top};
    POINT br = {client.right, client.bottom};
    ClientToScreen(w32->hwnd, &tl);
    ClientToScreen(w32->hwnd, &br);
    RECT screen_rect = {tl.x, tl.y, br.x, br.y};
    ClipCursor(&screen_rect);
}

int vgfx_platform_set_relative_mouse(struct vgfx_window *win, int enabled) {
    if (!win || !win->platform_data)
        return 0;
    vgfx_win32_data *w32 = (vgfx_win32_data *)win->platform_data;
    if (!w32->hwnd)
        return 0;

    /* Register (or unregister) for raw mouse input. Raw deltas arrive as
     * WM_INPUT messages while this window is in the foreground; they are
     * unaccelerated hardware counts, independent of pointer ballistics. */
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01; /* HID_USAGE_PAGE_GENERIC */
    rid.usUsage = 0x02;     /* HID_USAGE_GENERIC_MOUSE */
    if (enabled) {
        rid.dwFlags = 0;
        rid.hwndTarget = w32->hwnd;
    } else {
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = NULL;
    }
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        ClipCursor(NULL);
        return 0;
    }
    win32_apply_cursor_clip(win, enabled);
    return 1;
}

void vgfx_platform_hide_cursor(void) {
    vgfx_platform_set_cursor_visible(NULL, 0);
}

void vgfx_platform_show_cursor(void) {
    vgfx_platform_set_cursor_visible(NULL, 1);
}

#endif /* _WIN32 */
