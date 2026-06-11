//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperGFX Linux X11 Backend
//
// Platform-specific implementation using X11 (Xlib) on Linux/Unix systems.
// Provides window creation, event handling, framebuffer blitting, and timing
// functions for X11-based systems.
//
// Architecture:
//   - Display: X11 connection to the X server
//   - Window: Native X11 window handle
//   - XImage: Wrapper for framebuffer data for efficient blitting
//   - GC (Graphics Context): X11 drawing context
//   - Atom: WM_DELETE_WINDOW protocol for close button handling
//
// Key X11 Concepts:
//   - XOpenDisplay: Establish connection to X server
//   - XCreateWindow: Create native window
//   - XImage: Wrap framebuffer for blitting with XPutImage
//   - XPending/XNextEvent: Non-blocking event polling
//   - ClientMessage: Window manager protocol messages (close, etc.)
//   - KeySym: X11 keyboard symbol mapping via XLookupKeysym
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Linux X11 backend implementation for ViperGFX.
/// @details Uses Xlib to provide window management and framebuffer
///          presentation on Linux and Unix systems.

#include "vgfx_internal.h"

#if defined(__linux__) || defined(__unix__)

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

//===----------------------------------------------------------------------===//
// Platform Data Structure
//===----------------------------------------------------------------------===//

/// @brief Platform-specific data for X11 windows.
/// @details Allocated and owned by the platform backend.  Stored in
///          vgfx_window->platform_data.  Contains X11 Display connection,
///          Window handle, XImage for blitting, and WM protocol atoms.
///
/// @invariant display != NULL implies window != 0 && gc != NULL
typedef struct {
    Display *display;       ///< X11 connection to server
    int screen;             ///< Screen number
    Window window;          ///< Native X11 window handle
    GC gc;                  ///< Graphics context for drawing
    Atom wm_delete_window;  ///< Atom for WM_DELETE_WINDOW protocol
    XImage *ximage;         ///< XImage wrapper for presentation buffer
    uint8_t *ximage_buf;    ///< BGRA presentation buffer (R↔B swizzled from win->pixels)
    Visual *visual;         ///< Visual used for window and XImage
    int depth;              ///< Depth matching visual (24 or 32)
    Colormap colormap;      ///< Colormap for the chosen visual (None if default)
    size_t ximage_buf_size; ///< Size of ximage_buf in bytes
    int width;              ///< Cached window width
    int height;             ///< Cached window height
    int close_requested;    ///< 1 if WM_DELETE_WINDOW received, 0 otherwise
    // XDND (drag-and-drop) atoms
    Atom xdnd_aware;                  ///< XdndAware atom
    Atom xdnd_enter;                  ///< XdndEnter atom
    Atom xdnd_position;               ///< XdndPosition atom
    Atom xdnd_status;                 ///< XdndStatus atom
    Atom xdnd_drop;                   ///< XdndDrop atom
    Atom xdnd_finished;               ///< XdndFinished atom
    Atom xdnd_selection;              ///< XdndSelection atom
    Atom xdnd_type_list;              ///< XdndTypeList atom
    Atom text_uri_list;               ///< text/uri-list MIME type atom
    Window xdnd_source;               ///< Source window for current drag
    Atom clipboard_atom;              ///< CLIPBOARD selection atom
    Atom utf8_string_atom;            ///< UTF8_STRING target atom
    Atom targets_atom;                ///< TARGETS target atom
    Atom incr_atom;                   ///< INCR target/property atom
    Atom clipboard_property_atom;     ///< Property used for selection conversion
    char *clipboard_text;             ///< Owned text while this window owns CLIPBOARD
    XIM xim;                          ///< Input method for UTF-8 text input
    XIC xic;                          ///< Input context for UTF-8 text input
    int cursor_type;                  ///< Last requested cursor type
    int cursor_visible;               ///< 1 if cursor should be visible
    Cursor blank_cursor;              ///< Cached invisible cursor
    struct vgfx_window *owner_window; ///< Backlink for multi-window global services
    struct vgfx_window *next_window;  ///< Intrusive list of live X11 windows
} vgfx_x11_data;

static struct vgfx_window *g_vgfx_cursor_window = NULL;
static struct vgfx_window *g_vgfx_clipboard_window = NULL;
static struct vgfx_window *g_vgfx_x11_windows = NULL;

enum {
    /*
     * X11/X.h standardizes Button1..Button5 only. Many servers report
     * horizontal wheel motion as raw button codes 6 and 7, so keep those
     * values local instead of relying on non-portable macros.
     */
    VGFX_X11_BUTTON_SCROLL_LEFT = 6,
    VGFX_X11_BUTTON_SCROLL_RIGHT = 7,
};

static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9')
        return (int)(c - '0');
    if (c >= 'a' && c <= 'f')
        return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F')
        return (int)(c - 'A') + 10;
    return -1;
}

static int percent_decode_path(const char *src, size_t len, char *dst, size_t dst_cap) {
    if (!dst || dst_cap == 0)
        return 0;

    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '\0')
            break;
        if (out + 1 >= dst_cap) {
            dst[0] = '\0';
            return 0;
        }
        if (ch == '%' && i + 2 < len) {
            int hi = hex_value((unsigned char)src[i + 1]);
            int lo = hex_value((unsigned char)src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        dst[out++] = (char)ch;
    }
    dst[out] = '\0';
    return 1;
}

static void enqueue_xdnd_uri_line(struct vgfx_window *win,
                                  int64_t timestamp,
                                  const char *line,
                                  size_t line_len) {
    while (line_len > 0 && (line[line_len - 1] == '\r' || line[line_len - 1] == '\0'))
        line_len--;
    if (line_len == 0 || line[0] == '#')
        return;

    const char *path = line;
    size_t path_len = line_len;
    if (line_len >= 7 && strncmp(line, "file://", 7) == 0) {
        path = line + 7;
        path_len = line_len - 7;
        if (path_len >= 10 && strncmp(path, "localhost/", 10) == 0) {
            path += 9;
            path_len -= 9;
        } else if (path_len > 0 && path[0] != '/') {
            const char *slash = memchr(path, '/', path_len);
            if (!slash)
                return;
            path_len -= (size_t)(slash - path);
            path = slash;
        }
    }

    vgfx_event_t vgfx_event = {0};
    vgfx_event.type = VGFX_EVENT_FILE_DROP;
    vgfx_event.time_ms = timestamp;
    if (!percent_decode_path(path,
                             path_len,
                             vgfx_event.data.file_drop.path,
                             sizeof(vgfx_event.data.file_drop.path))) {
        vgfx_internal_note_event_overflow(win);
        return;
    }
    if (vgfx_event.data.file_drop.path[0] != '\0')
        vgfx_internal_enqueue_event(win, &vgfx_event);
}

static void parse_xdnd_uri_list(struct vgfx_window *win,
                                int64_t timestamp,
                                const unsigned char *data,
                                size_t len) {
    size_t line_start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || data[i] == '\n') {
            enqueue_xdnd_uri_line(win, timestamp, (const char *)data + line_start, i - line_start);
            line_start = i + 1;
        }
    }
}

//===----------------------------------------------------------------------===//
// Key Code Translation
//===----------------------------------------------------------------------===//

/// @brief Translate X11 KeySym to vgfx_key_t.
/// @details Maps X11 keysyms (obtained via XLookupKeysym) to ViperGFX key
///          codes.  Handles A-Z, 0-9, Space, arrows, Enter, Escape.
///          Unrecognized keys return VGFX_KEY_UNKNOWN.
///
/// @param keysym X11 KeySym from XLookupKeysym()
/// @return Corresponding vgfx_key_t, or VGFX_KEY_UNKNOWN if not recognized
///
/// @details Key mapping:
///            - A-Z: Mapped to vgfx_key_t enum values (uppercase)
///            - 0-9: Mapped to vgfx_key_t enum values
///            - Space: VGFX_KEY_SPACE
///            - Arrows: VGFX_KEY_LEFT/RIGHT/UP/DOWN
///            - Enter/Return: VGFX_KEY_ENTER
///            - Escape: VGFX_KEY_ESCAPE
static vgfx_key_t translate_keysym(KeySym keysym) {
    /* Lowercase letters (convert to uppercase) */
    if (keysym >= XK_a && keysym <= XK_z) {
        return (vgfx_key_t)('A' + (keysym - XK_a));
    }

    /* Uppercase letters */
    if (keysym >= XK_A && keysym <= XK_Z) {
        return (vgfx_key_t)keysym;
    }

    /* Digits 0-9 */
    if (keysym >= XK_0 && keysym <= XK_9) {
        return (vgfx_key_t)keysym;
    }

    /* Other printable ASCII (punctuation/symbols): X11 Latin-1 keysyms equal their
       ASCII codes, so '=' (XK_equal) and '-' (XK_minus) — which back the zoom
       shortcuts — map straight through. Placed after the a-z block above so lowercase
       letters are still folded to uppercase; special keys live in the 0xff00+ range
       and are unaffected by this check. */
    if (keysym >= 0x20 && keysym <= 0x7e) {
        return (vgfx_key_t)keysym;
    }

    /* Special keys */
    switch (keysym) {
        case XK_space:
            return VGFX_KEY_SPACE;
        case XK_Return:
            return VGFX_KEY_ENTER;
        case XK_KP_Enter:
            return VGFX_KEY_ENTER;
        case XK_Escape:
            return VGFX_KEY_ESCAPE;
        case XK_BackSpace:
            return VGFX_KEY_BACKSPACE;
        case XK_Delete:
        case XK_KP_Delete:
            return VGFX_KEY_DELETE;
        case XK_Tab:
        case XK_ISO_Left_Tab:
        case XK_KP_Tab:
            return VGFX_KEY_TAB;
        case XK_Left:
        case XK_KP_Left:
            return VGFX_KEY_LEFT;
        case XK_Right:
        case XK_KP_Right:
            return VGFX_KEY_RIGHT;
        case XK_Up:
        case XK_KP_Up:
            return VGFX_KEY_UP;
        case XK_Down:
        case XK_KP_Down:
            return VGFX_KEY_DOWN;
        case XK_Home:
        case XK_KP_Home:
            return VGFX_KEY_HOME;
        case XK_End:
        case XK_KP_End:
            return VGFX_KEY_END;
        case XK_Page_Up:
        case XK_KP_Page_Up:
            return VGFX_KEY_PAGE_UP;
        case XK_Page_Down:
        case XK_KP_Page_Down:
            return VGFX_KEY_PAGE_DOWN;
        default:
            return VGFX_KEY_UNKNOWN;
    }
}

static int x11_modifiers(unsigned int state) {
    int mods = 0;
    if (state & ShiftMask)
        mods |= VGFX_MOD_SHIFT;
    if (state & ControlMask)
        mods |= VGFX_MOD_CTRL;
    if (state & Mod1Mask)
        mods |= VGFX_MOD_ALT;
    if (state & Mod4Mask)
        mods |= VGFX_MOD_CMD;
    return mods;
}

static int utf8_decode_codepoint(const char *bytes, int len, uint32_t *out_codepoint) {
    const unsigned char *s = (const unsigned char *)bytes;
    if (!s || len <= 0 || !out_codepoint)
        return 0;

    *out_codepoint = 0;
    if (s[0] < 0x80) {
        *out_codepoint = s[0];
        return 1;
    }
    if (len >= 2 && s[0] >= 0xC2 && s[0] <= 0xDF && (s[1] & 0xC0) == 0x80) {
        *out_codepoint = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        return 2;
    }
    if (len >= 3 && s[0] >= 0xE0 && s[0] <= 0xEF && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80) {
        if ((s[0] == 0xE0 && s[1] < 0xA0) || (s[0] == 0xED && s[1] >= 0xA0))
            return 1;
        *out_codepoint = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) |
                         (uint32_t)(s[2] & 0x3F);
        return 3;
    }
    if (len >= 4 && s[0] >= 0xF0 && s[0] <= 0xF4 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        if ((s[0] == 0xF0 && s[1] < 0x90) || (s[0] == 0xF4 && s[1] > 0x8F))
            return 1;
        *out_codepoint = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
                         ((uint32_t)(s[2] & 0x3F) << 6) | (uint32_t)(s[3] & 0x3F);
        return 4;
    }

    return 1;
}

static void x11_enqueue_text_input_events(
    struct vgfx_window *win, int64_t timestamp, int mods, const char *text, int text_len) {
    int offset = 0;

    if (!win || !text || text_len <= 0)
        return;

    while (offset < text_len) {
        uint32_t codepoint = 0;
        int consumed = utf8_decode_codepoint(text + offset, text_len - offset, &codepoint);
        if (consumed <= 0)
            break;
        offset += consumed;

        if (vgfx_internal_should_emit_text_input(codepoint, mods)) {
            vgfx_event_t text_event = {.type = VGFX_EVENT_TEXT_INPUT,
                                       .time_ms = timestamp,
                                       .data.text = {.codepoint = codepoint, .modifiers = mods}};
            vgfx_internal_enqueue_event(win, &text_event);
        }
    }
}

static int32_t x11_logical_to_physical(const struct vgfx_window *win, int32_t logical) {
    float scale = win ? vgfx_internal_sanitize_scale(win->scale_factor) : 1.0f;
    return vgfx_internal_scale_up_i32(logical, scale);
}

static int x11_window_usable(const struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;
    const vgfx_x11_data *x11 = (const vgfx_x11_data *)win->platform_data;
    return x11 && x11->display && x11->window;
}

static void x11_register_window(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    x11->owner_window = win;
    x11->next_window = g_vgfx_x11_windows;
    g_vgfx_x11_windows = win;
    if (!g_vgfx_cursor_window)
        g_vgfx_cursor_window = win;
    if (!g_vgfx_clipboard_window)
        g_vgfx_clipboard_window = win;
}

static void x11_unregister_window(struct vgfx_window *win) {
    struct vgfx_window **cursor = &g_vgfx_x11_windows;
    while (*cursor) {
        vgfx_x11_data *x11 = (vgfx_x11_data *)(*cursor)->platform_data;
        if (*cursor == win) {
            *cursor = x11 ? x11->next_window : NULL;
            break;
        }
        if (!x11)
            break;
        cursor = &x11->next_window;
    }
    if (g_vgfx_cursor_window == win)
        g_vgfx_cursor_window = g_vgfx_x11_windows;
    if (g_vgfx_clipboard_window == win)
        g_vgfx_clipboard_window = g_vgfx_x11_windows;
}

static struct vgfx_window *x11_clipboard_window(void) {
    if (x11_window_usable(g_vgfx_clipboard_window))
        return g_vgfx_clipboard_window;

    for (struct vgfx_window *win = g_vgfx_x11_windows; win;) {
        vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
        struct vgfx_window *next = x11 ? x11->next_window : NULL;
        if (x11_window_usable(win) && win->is_focused) {
            g_vgfx_clipboard_window = win;
            return win;
        }
        win = next;
    }

    if (x11_window_usable(g_vgfx_cursor_window)) {
        g_vgfx_clipboard_window = g_vgfx_cursor_window;
        return g_vgfx_clipboard_window;
    }
    if (x11_window_usable(g_vgfx_x11_windows)) {
        g_vgfx_clipboard_window = g_vgfx_x11_windows;
        return g_vgfx_clipboard_window;
    }

    g_vgfx_clipboard_window = NULL;
    return NULL;
}

static int x11_create_ximage_resources(vgfx_x11_data *x11,
                                       int32_t width,
                                       int32_t height,
                                       int32_t stride,
                                       XImage **out_image,
                                       uint8_t **out_buf,
                                       size_t *out_size) {
    if (!x11 || !out_image || !out_buf || !out_size)
        return 0;

    *out_image = NULL;
    *out_buf = NULL;
    *out_size = 0;

    if (!x11->display)
        return 0;

    if (height <= 0 || stride <= 0 || (size_t)height > SIZE_MAX / (size_t)stride) {
        vgfx_internal_set_error(VGFX_ERR_INVALID_PARAM, "Invalid XImage buffer dimensions");
        return 0;
    }
    size_t buf_size = (size_t)height * (size_t)stride;
    uint8_t *buf = (uint8_t *)calloc(1, buf_size);
    if (!buf) {
        vgfx_internal_set_error(VGFX_ERR_ALLOC, "Failed to allocate XImage buffer");
        return 0;
    }

    XImage *image = XCreateImage(
        x11->display, x11->visual, x11->depth, ZPixmap, 0, (char *)buf, width, height, 32, stride);
    if (!image) {
        free(buf);
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create XImage");
        return 0;
    }

    image->byte_order = LSBFirst;
    *out_image = image;
    *out_buf = buf;
    *out_size = buf_size;
    return 1;
}

static void x11_replace_ximage(vgfx_x11_data *x11,
                               XImage *new_image,
                               uint8_t *new_buf,
                               size_t new_size) {
    if (!x11)
        return;

    if (x11->ximage) {
        x11->ximage->data = NULL;
        XDestroyImage(x11->ximage);
    }
    free(x11->ximage_buf);
    x11->ximage = new_image;
    x11->ximage_buf = new_buf;
    x11->ximage_buf_size = new_size;
}

static int x11_recreate_ximage(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    XImage *new_image = NULL;
    uint8_t *new_buf = NULL;
    size_t new_size = 0;
    if (!x11_create_ximage_resources(
            x11, win->width, win->height, win->stride, &new_image, &new_buf, &new_size)) {
        return 0;
    }

    x11_replace_ximage(x11, new_image, new_buf, new_size);
    x11->width = win->width;
    x11->height = win->height;
    return 1;
}

static int x11_resize_backing_store(
    struct vgfx_window *win, int32_t new_w, int32_t new_h, int64_t timestamp, int emit_event) {
    if (!win || !win->platform_data)
        return 0;
    if (new_w <= 0 || new_h <= 0 || new_w > VGFX_MAX_WIDTH || new_h > VGFX_MAX_HEIGHT ||
        new_w > INT32_MAX / 4) {
        vgfx_internal_set_error(VGFX_ERR_INVALID_PARAM, "X11 resize exceeds framebuffer limits");
        return 0;
    }

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    int32_t new_stride = new_w * 4;
    XImage *new_image = NULL;
    uint8_t *new_buf = NULL;
    size_t new_size = 0;
    if (!x11_create_ximage_resources(
            x11, new_w, new_h, new_stride, &new_image, &new_buf, &new_size)) {
        return 0;
    }

    if (!vgfx_internal_resize_framebuffer(win, new_w, new_h)) {
        if (new_image) {
            new_image->data = NULL;
            XDestroyImage(new_image);
        }
        free(new_buf);
        return 0;
    }

    x11_replace_ximage(x11, new_image, new_buf, new_size);
    x11->width = new_w;
    x11->height = new_h;

    if (emit_event) {
        vgfx_event_t event = {0};
        vgfx_internal_init_resize_event(&event, win, timestamp, new_w, new_h);
        vgfx_internal_enqueue_event(win, &event);
    }

    return 1;
}

static void x11_cleanup_platform(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;

    x11_unregister_window(win);

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;

    if (x11->ximage) {
        x11->ximage->data = NULL;
        XDestroyImage(x11->ximage);
        x11->ximage = NULL;
    }

    free(x11->ximage_buf);
    x11->ximage_buf = NULL;
    x11->ximage_buf_size = 0;

    free(x11->clipboard_text);
    x11->clipboard_text = NULL;

    if (x11->display) {
        if (x11->gc) {
            XFreeGC(x11->display, x11->gc);
            x11->gc = NULL;
        }
        if (x11->blank_cursor) {
            XFreeCursor(x11->display, x11->blank_cursor);
            x11->blank_cursor = 0;
        }
        if (x11->xic) {
            XDestroyIC(x11->xic);
            x11->xic = NULL;
        }
        if (x11->xim) {
            XCloseIM(x11->xim);
            x11->xim = NULL;
        }
        if (x11->colormap && x11->colormap != DefaultColormap(x11->display, x11->screen)) {
            XFreeColormap(x11->display, x11->colormap);
            x11->colormap = 0;
        }
        if (x11->window) {
            XDestroyWindow(x11->display, x11->window);
            x11->window = 0;
        }
        XCloseDisplay(x11->display);
        x11->display = NULL;
    }

    free(x11);
    win->platform_data = NULL;
}

//===----------------------------------------------------------------------===//
// Platform API Implementation
//===----------------------------------------------------------------------===//

/// @brief Query the HiDPI backing scale factor for the X11 display.
/// @details Tries three sources in priority order:
///
///   1. GDK_SCALE env var — set by GNOME/Mutter on both Wayland and X11.
///      Example: GDK_SCALE=2 on a HiDPI GNOME desktop.
///
///   2. QT_SCALE_FACTOR env var — set by KDE Plasma.
///      Example: QT_SCALE_FACTOR=1.5 on a KDE 150% display.
///
///   3. Xft.dpi from the X11 resource database (XResourceManagerString).
///      Standard 96 DPI → scale 1.0; 192 DPI → scale 2.0.
///
///   4. Fallback: 1.0 (standard 96 DPI display).
///
/// @note X11 always reports physical pixel coordinates in events and
///       XConfigureNotify, so no additional scaling is needed in the
///       event handlers or resize handler on Linux.
///
/// @return Scale factor ≥ 1.0
float vgfx_platform_get_display_scale(void) {
    /* Priority 1: GDK_SCALE env var (GNOME/Mutter on Wayland and X11) */
    const char *gdk = getenv("GDK_SCALE");
    if (gdk) {
        float s = strtof(gdk, NULL);
        if (s >= 1.0f)
            return vgfx_internal_sanitize_scale(s);
    }

    /* Priority 2: QT_SCALE_FACTOR (KDE Plasma) */
    const char *qt = getenv("QT_SCALE_FACTOR");
    if (qt) {
        float s = strtof(qt, NULL);
        if (s >= 1.0f)
            return vgfx_internal_sanitize_scale(s);
    }

    /* Priority 3: Xft.dpi from the X11 resource database.
     * Open a temporary display connection just for this query so we don't
     * interfere with the window's own Display connection (opened later). */
    Display *dpy = XOpenDisplay(NULL);
    if (dpy) {
        float scale = 1.0f;
        const char *rms = XResourceManagerString(dpy);
        if (rms) {
            /* Search for "Xft.dpi:\t96" or "Xft.dpi: 192" etc. */
            const char *pos = strstr(rms, "Xft.dpi:");
            if (pos) {
                pos += 8; /* skip "Xft.dpi:" */
                while (*pos == ' ' || *pos == '\t')
                    pos++;
                float dpi = strtof(pos, NULL);
                if (dpi >= 96.0f)
                    scale = dpi / 96.0f;
            }
        }
        XCloseDisplay(dpy);
        return vgfx_internal_sanitize_scale(scale);
    }

    return 1.0f; /* fallback: assume standard 96 DPI */
}

/// @brief Initialize platform-specific window resources for X11.
/// @details Opens connection to X server, creates X11 window with appropriate
///          attributes, sets up WM_DELETE_WINDOW protocol for close button,
///          creates XImage wrapper for framebuffer, and makes window visible.
///
/// @param win    Pointer to the ViperGFX window structure (framebuffer already allocated)
/// @param params Window creation parameters (title, dimensions, resizable flag)
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  params != NULL
/// @pre  win->pixels != NULL (framebuffer allocated by vgfx_create_window)
/// @post On success: X11 window created and visible, platform_data allocated
/// @post On failure: platform_data NULL, error set
///
/// @details The window is:
///            - Has a title bar
///            - Can be closed (intercepts WM_DELETE_WINDOW)
///            - Receives keyboard and mouse input
///            - 32-bit depth for direct RGBA rendering
int vgfx_platform_init_window(struct vgfx_window *win, const vgfx_window_params_t *params) {
    if (!win || !params)
        return 0;

    /* Allocate platform data structure */
    vgfx_x11_data *x11 = (vgfx_x11_data *)calloc(1, sizeof(vgfx_x11_data));
    if (!x11) {
        vgfx_internal_set_error(VGFX_ERR_ALLOC, "Failed to allocate X11 platform data");
        return 0;
    }

    win->platform_data = x11;
    x11_register_window(win);
    x11->close_requested = 0;
    x11->cursor_type = 0;
    x11->cursor_visible = 1;
    x11->width = win->width;
    x11->height = win->height;

    /* Open connection to X server */
    x11->display = XOpenDisplay(NULL);
    if (!x11->display) {
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to open X11 display");
        x11_cleanup_platform(win);
        return 0;
    }

    x11->screen = DefaultScreen(x11->display);
    Window root = RootWindow(x11->display, x11->screen);

    /* Find a visual that matches our 32-bit RGBA framebuffer.  Prefer a
     * 32-bit TrueColor visual so XPutImage can blit the pixel buffer without
     * depth conversion.  Fall back to the default visual (usually 24-bit)
     * which still works — the alpha byte in each 32-bpp pixel is ignored. */
    XVisualInfo vinfo;
    if (XMatchVisualInfo(x11->display, x11->screen, 32, TrueColor, &vinfo)) {
        x11->visual = vinfo.visual;
        x11->depth = 32;
        x11->colormap = XCreateColormap(x11->display, root, x11->visual, AllocNone);
    } else {
        x11->visual = DefaultVisual(x11->display, x11->screen);
        x11->depth = DefaultDepth(x11->display, x11->screen);
        x11->colormap = DefaultColormap(x11->display, x11->screen);
    }

    XSetWindowAttributes attrs;
    attrs.background_pixel = 0;
    attrs.border_pixel = 0;
    attrs.colormap = x11->colormap;
    attrs.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | ExposureMask | FocusChangeMask | StructureNotifyMask |
                       PropertyChangeMask;

    x11->window = XCreateWindow(x11->display,
                                root,
                                0,
                                0, /* x, y position (will be overridden) */
                                (unsigned int)win->width,
                                (unsigned int)win->height,
                                0,           /* border width */
                                x11->depth,  /* depth matching our visual */
                                InputOutput, /* class */
                                x11->visual, /* visual matching our depth */
                                CWBackPixel | CWBorderPixel | CWColormap | CWEventMask,
                                &attrs);

    if (!x11->window) {
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create X11 window");
        x11_cleanup_platform(win);
        return 0;
    }

    /* Set window title */
    XStoreName(x11->display, x11->window, params->title);
    XSetIconName(x11->display, x11->window, params->title);

    x11->xim = XOpenIM(x11->display, NULL, NULL, NULL);
    if (x11->xim) {
        x11->xic = XCreateIC(x11->xim,
                             XNInputStyle,
                             XIMPreeditNothing | XIMStatusNothing,
                             XNClientWindow,
                             x11->window,
                             XNFocusWindow,
                             x11->window,
                             NULL);
    }

    /* Set window size hints (prevents resizing if not resizable) */
    XSizeHints *size_hints = XAllocSizeHints();
    if (size_hints) {
        size_hints->flags = PSize | PMinSize | PMaxSize;
        size_hints->width = win->width;
        size_hints->height = win->height;
        size_hints->min_width = params->resizable ? 1 : win->width;
        size_hints->min_height = params->resizable ? 1 : win->height;
        size_hints->max_width = params->resizable ? 16384 : win->width;
        size_hints->max_height = params->resizable ? 16384 : win->height;
        XSetWMNormalHints(x11->display, x11->window, size_hints);
        XFree(size_hints);
    }

    /* Set up WM_DELETE_WINDOW protocol (intercept close button) */
    x11->wm_delete_window = XInternAtom(x11->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x11->display, x11->window, &x11->wm_delete_window, 1);

    /* Set up XDND (drag-and-drop) protocol */
    x11->xdnd_aware = XInternAtom(x11->display, "XdndAware", False);
    x11->xdnd_enter = XInternAtom(x11->display, "XdndEnter", False);
    x11->xdnd_position = XInternAtom(x11->display, "XdndPosition", False);
    x11->xdnd_status = XInternAtom(x11->display, "XdndStatus", False);
    x11->xdnd_drop = XInternAtom(x11->display, "XdndDrop", False);
    x11->xdnd_finished = XInternAtom(x11->display, "XdndFinished", False);
    x11->xdnd_selection = XInternAtom(x11->display, "XdndSelection", False);
    x11->xdnd_type_list = XInternAtom(x11->display, "XdndTypeList", False);
    x11->text_uri_list = XInternAtom(x11->display, "text/uri-list", False);
    x11->clipboard_atom = XInternAtom(x11->display, "CLIPBOARD", False);
    x11->utf8_string_atom = XInternAtom(x11->display, "UTF8_STRING", False);
    x11->targets_atom = XInternAtom(x11->display, "TARGETS", False);
    x11->incr_atom = XInternAtom(x11->display, "INCR", False);
    x11->clipboard_property_atom = XInternAtom(x11->display, "VIPERGFX_CLIPBOARD", False);
    x11->xdnd_source = 0;
    {
        /* Advertise XDND version 5 support */
        Atom xdnd_version = 5;
        XChangeProperty(x11->display,
                        x11->window,
                        x11->xdnd_aware,
                        XA_ATOM,
                        32,
                        PropModeReplace,
                        (unsigned char *)&xdnd_version,
                        1);
    }

    /* Create graphics context */
    x11->gc = XCreateGC(x11->display, x11->window, 0, NULL);
    if (!x11->gc) {
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create X11 GC");
        x11_cleanup_platform(win);
        return 0;
    }

    /* Allocate the presentation buffer/XImage at the framebuffer size in
     * physical pixels so present and resize stay consistent with win->pixels. */
    if (!x11_recreate_ximage(win)) {
        x11_cleanup_platform(win);
        return 0;
    }

    /* Map (show) the window */
    XMapWindow(x11->display, x11->window);
    XFlush(x11->display);

    return 1;
}

/// @brief Destroy platform-specific window resources for X11.
/// @details Destroys XImage wrapper, closes X11 window, frees graphics
///          context, closes display connection, and frees platform data.
///          Safe to call even if init failed.
///
/// @param win Pointer to the ViperGFX window structure
///
/// @pre  win != NULL
/// @post platform_data freed and set to NULL
/// @post X11 window destroyed and display connection closed (if existed)
void vgfx_platform_destroy_window(struct vgfx_window *win) {
    x11_cleanup_platform(win);
}

static char *x11_strdup_text(const char *text) {
    const char *src = text ? text : "";
    size_t len = strlen(src);
    char *copy = (char *)malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, src, len + 1u);
    return copy;
}

static void x11_handle_selection_request(vgfx_x11_data *x11, XSelectionRequestEvent *request) {
    if (!x11 || !x11->display || !request)
        return;

    XSelectionEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = SelectionNotify;
    reply.display = request->display;
    reply.requestor = request->requestor;
    reply.selection = request->selection;
    reply.target = request->target;
    reply.time = request->time;
    reply.property = None;

    Atom property = request->property != None ? request->property : request->target;
    if (request->selection == x11->clipboard_atom && x11->clipboard_text) {
        if (request->target == x11->targets_atom) {
            Atom targets[] = {x11->targets_atom, x11->utf8_string_atom, XA_STRING};
            XChangeProperty(x11->display,
                            request->requestor,
                            property,
                            XA_ATOM,
                            32,
                            PropModeReplace,
                            (const unsigned char *)targets,
                            (int)(sizeof(targets) / sizeof(targets[0])));
            reply.property = property;
        } else if (request->target == x11->utf8_string_atom || request->target == XA_STRING) {
            const unsigned char *text = (const unsigned char *)x11->clipboard_text;
            size_t len = strlen(x11->clipboard_text);
            if (len > INT32_MAX)
                len = INT32_MAX;
            XChangeProperty(x11->display,
                            request->requestor,
                            property,
                            request->target,
                            8,
                            PropModeReplace,
                            text,
                            (int)len);
            reply.property = property;
        }
    }

    XSendEvent(x11->display, request->requestor, False, 0, (XEvent *)&reply);
    XFlush(x11->display);
}

static Bool x11_clipboard_selection_notify_predicate(Display *display,
                                                     XEvent *event,
                                                     XPointer arg) {
    (void)display;
    vgfx_x11_data *x11 = (vgfx_x11_data *)arg;
    return x11 && event && event->type == SelectionNotify &&
           event->xselection.requestor == x11->window &&
           event->xselection.selection == x11->clipboard_atom;
}

typedef struct {
    vgfx_x11_data *x11;
    Atom property;
} x11_property_wait_t;

static Bool x11_property_new_value_predicate(Display *display, XEvent *event, XPointer arg) {
    (void)display;
    x11_property_wait_t *wait = (x11_property_wait_t *)arg;
    return wait && wait->x11 && event && event->type == PropertyNotify &&
           event->xproperty.window == wait->x11->window &&
           event->xproperty.atom == wait->property && event->xproperty.state == PropertyNewValue;
}

static int x11_append_bytes(
    char **result, size_t *len, size_t *cap, const unsigned char *data, size_t nitems) {
    if (!result || !len || !cap)
        return 0;
    if (nitems == 0)
        return 1;
    if (nitems > SIZE_MAX - *len - 1u)
        return 0;
    size_t needed = *len + nitems + 1u;
    if (needed > *cap) {
        size_t new_cap = *cap ? *cap : 4096u;
        while (new_cap < needed) {
            if (new_cap > SIZE_MAX / 2u) {
                new_cap = needed;
                break;
            }
            new_cap *= 2u;
        }
        char *next = (char *)realloc(*result, new_cap);
        if (!next)
            return 0;
        *result = next;
        *cap = new_cap;
    }
    memcpy(*result + *len, data, nitems);
    *len += nitems;
    (*result)[*len] = '\0';
    return 1;
}

static char *x11_read_incr_text_property(vgfx_x11_data *x11, Atom property, Atom requested_target) {
    if (!x11 || !x11->display || property == None)
        return NULL;

    XDeleteProperty(x11->display, x11->window, property);
    XFlush(x11->display);

    char *result = NULL;
    size_t len = 0;
    size_t cap = 0;
    int64_t start = vgfx_platform_now_ms();
    x11_property_wait_t wait = {x11, property};

    while (vgfx_platform_now_ms() - start < 1000) {
        XEvent prop_event;
        if (!XCheckIfEvent(
                x11->display, &prop_event, x11_property_new_value_predicate, (XPointer)&wait)) {
            usleep(1000);
            continue;
        }

        Atom actual_type = None;
        int actual_format = 0;
        unsigned long nitems = 0;
        unsigned long bytes_after = 0;
        unsigned char *data = NULL;
        int status = XGetWindowProperty(x11->display,
                                        x11->window,
                                        property,
                                        0,
                                        262144,
                                        True,
                                        AnyPropertyType,
                                        &actual_type,
                                        &actual_format,
                                        &nitems,
                                        &bytes_after,
                                        &data);
        if (status != Success) {
            if (data)
                XFree(data);
            free(result);
            return NULL;
        }

        if (nitems == 0 && bytes_after == 0) {
            if (data)
                XFree(data);
            if (!result) {
                result = (char *)malloc(1u);
                if (result)
                    result[0] = '\0';
            }
            return result;
        }

        if (actual_format != 8 ||
            !(actual_type == requested_target || actual_type == x11->utf8_string_atom ||
              actual_type == XA_STRING)) {
            if (data)
                XFree(data);
            free(result);
            return NULL;
        }

        if (!x11_append_bytes(&result, &len, &cap, data, (size_t)nitems)) {
            if (data)
                XFree(data);
            free(result);
            return NULL;
        }
        if (data)
            XFree(data);
        start = vgfx_platform_now_ms();
    }

    free(result);
    return NULL;
}

static char *x11_read_text_property(vgfx_x11_data *x11, Atom property, Atom requested_target) {
    if (!x11 || !x11->display || !x11->window || property == None)
        return NULL;

    char *result = NULL;
    size_t len = 0;
    size_t cap = 0;
    long offset = 0;
    unsigned long bytes_after = 0;

    do {
        Atom actual_type = None;
        int actual_format = 0;
        unsigned long nitems = 0;
        unsigned char *data = NULL;
        int status = XGetWindowProperty(x11->display,
                                        x11->window,
                                        property,
                                        offset,
                                        262144,
                                        False,
                                        AnyPropertyType,
                                        &actual_type,
                                        &actual_format,
                                        &nitems,
                                        &bytes_after,
                                        &data);

        if (status != Success) {
            if (data)
                XFree(data);
            free(result);
            XDeleteProperty(x11->display, x11->window, property);
            return NULL;
        }

        if (actual_type == x11->incr_atom) {
            if (data)
                XFree(data);
            free(result);
            return x11_read_incr_text_property(x11, property, requested_target);
        }

        if (actual_format != 8 ||
            !(actual_type == requested_target || actual_type == x11->utf8_string_atom ||
              actual_type == XA_STRING)) {
            if (data)
                XFree(data);
            free(result);
            XDeleteProperty(x11->display, x11->window, property);
            return NULL;
        }

        if (nitems > 0) {
            if (!x11_append_bytes(&result, &len, &cap, data, (size_t)nitems)) {
                if (data)
                    XFree(data);
                free(result);
                XDeleteProperty(x11->display, x11->window, property);
                return NULL;
            }
        }

        if (data)
            XFree(data);
        offset += (long)((nitems + 3ul) / 4ul);
    } while (bytes_after > 0);

    XDeleteProperty(x11->display, x11->window, property);

    if (!result) {
        result = (char *)malloc(1u);
        if (result)
            result[0] = '\0';
    }
    return result;
}

/// @brief Process pending X11 events and translate to ViperGFX events.
/// @details Polls the X11 event queue in non-blocking mode (XPending).
///          For each XEvent, translates it to a vgfx_event_t and enqueues it.
///          Updates win->key_state, win->mouse_x, win->mouse_y, and
///          win->mouse_button_state to reflect current input state.
///
///          Handles:
///            - Keyboard: KeyPress/KeyRelease → KEY_DOWN/KEY_UP
///            - Mouse move: MotionNotify → MOUSE_MOVE
///            - Mouse buttons: ButtonPress/ButtonRelease → MOUSE_DOWN/MOUSE_UP
///            - Window close: ClientMessage (WM_DELETE_WINDOW) → CLOSE
///            - Focus: FocusIn/FocusOut → FOCUS_GAINED/FOCUS_LOST
///            - Resize: ConfigureNotify → RESIZE (not fully supported in v1)
///            - Expose: Request redraw (handled by marking view dirty)
///
/// @param win Pointer to the ViperGFX window structure
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  win->platform_data != NULL
/// @post All pending XEvents processed and translated
/// @post win->key_state and win->mouse_* updated to reflect current input state
/// @post Corresponding vgfx_event_t enqueued for each XEvent
int vgfx_platform_process_events(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display)
        return 0;

    /* Process all pending events without blocking */
    while (XPending(x11->display) > 0) {
        XEvent event;
        XNextEvent(x11->display, &event);

        int64_t timestamp = vgfx_platform_now_ms();

        switch (event.type) {
            case KeyPress: {
                KeySym keysym = NoSymbol;
                int mods = x11_modifiers(event.xkey.state);
                char text_buf[16];
                char *text_storage = text_buf;
                int text_len = 0;
                if (x11->xic) {
                    Status status = 0;
                    text_len = Xutf8LookupString(x11->xic,
                                                 &event.xkey,
                                                 text_storage,
                                                 (int)sizeof(text_buf),
                                                 &keysym,
                                                 &status);
                    if (status == XBufferOverflow && text_len > 0 && text_len < INT_MAX) {
                        int text_cap = text_len + 1;
                        text_storage = (char *)malloc((size_t)text_cap);
                        if (text_storage) {
                            text_len = Xutf8LookupString(
                                x11->xic, &event.xkey, text_storage, text_cap, &keysym, &status);
                            if (status == XBufferOverflow)
                                text_len = 0;
                        } else {
                            text_storage = text_buf;
                            text_len = 0;
                        }
                    }
                    if (status == XLookupNone)
                        text_len = 0;
                } else {
                    text_len = XLookupString(
                        &event.xkey, text_storage, (int)sizeof(text_buf), &keysym, NULL);
                }
                if (keysym == NoSymbol)
                    keysym = XLookupKeysym(&event.xkey, 0);
                vgfx_key_t key = translate_keysym(keysym);

                if (key != VGFX_KEY_UNKNOWN && key < 512) {
                    int is_repeat = win->key_state[key]; /* Already pressed = repeat */
                    win->key_state[key] = 1;             /* Update input state */

                    vgfx_event_t vgfx_event = {
                        .type = VGFX_EVENT_KEY_DOWN,
                        .time_ms = timestamp,
                        .data.key = {.key = key, .is_repeat = is_repeat, .modifiers = mods}};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                }
                x11_enqueue_text_input_events(win, timestamp, mods, text_storage, text_len);
                if (text_storage != text_buf)
                    free(text_storage);
                break;
            }

            case KeyRelease: {
                /* X11 generates repeated KeyRelease/KeyPress pairs for key repeat.
                 * We detect true release by checking if there's an immediate KeyPress. */
                if (XEventsQueued(x11->display, QueuedAfterReading)) {
                    XEvent next_event;
                    XPeekEvent(x11->display, &next_event);

                    /* If next event is KeyPress for same key, it's a repeat - ignore release */
                    if (next_event.type == KeyPress && next_event.xkey.time == event.xkey.time &&
                        next_event.xkey.keycode == event.xkey.keycode) {
                        break; /* Ignore this release event */
                    }
                }

                KeySym keysym = XLookupKeysym(&event.xkey, 0);
                vgfx_key_t key = translate_keysym(keysym);

                if (key != VGFX_KEY_UNKNOWN && key < 512) {
                    win->key_state[key] = 0; /* Update input state */

                    vgfx_event_t vgfx_event = {
                        .type = VGFX_EVENT_KEY_UP,
                        .time_ms = timestamp,
                        .data.key = {.key = key,
                                     .is_repeat = 0,
                                     .modifiers = x11_modifiers(event.xkey.state)}};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                }
                break;
            }

            case MotionNotify: {
                int32_t x = event.xmotion.x;
                int32_t y = event.xmotion.y;

                win->mouse_x = x; /* Update input state */
                win->mouse_y = y;

                vgfx_event_t vgfx_event = {
                    .type = VGFX_EVENT_MOUSE_MOVE,
                    .time_ms = timestamp,
                    .data.mouse_move = {
                        .x = x, .y = y, .modifiers = x11_modifiers(event.xmotion.state)}};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case ButtonPress: {
                int32_t x = event.xbutton.x;
                int32_t y = event.xbutton.y;
                win->mouse_x = x;
                win->mouse_y = y;

                /* X11 mouse button mapping:
                 *   Button1 = Left (1)
                 *   Button2 = Middle (2)
                 *   Button3 = Right (3)
                 *   Button4/5 = Vertical scroll wheel
                 *   Button6/7 = Horizontal scroll wheel
                 */
                vgfx_mouse_button_t button = VGFX_MOUSE_LEFT;
                if (event.xbutton.button == Button1) {
                    button = VGFX_MOUSE_LEFT;
                } else if (event.xbutton.button == Button2) {
                    button = VGFX_MOUSE_MIDDLE;
                } else if (event.xbutton.button == Button3) {
                    button = VGFX_MOUSE_RIGHT;
                } else if (event.xbutton.button == Button4 || event.xbutton.button == Button5 ||
                           event.xbutton.button == VGFX_X11_BUTTON_SCROLL_LEFT ||
                           event.xbutton.button == VGFX_X11_BUTTON_SCROLL_RIGHT) {
                    float dx = 0.0f;
                    float dy = 0.0f;
                    if (event.xbutton.button == Button4)
                        dy = -1.0f;
                    else if (event.xbutton.button == Button5)
                        dy = 1.0f;
                    else if (event.xbutton.button == VGFX_X11_BUTTON_SCROLL_LEFT)
                        dx = -1.0f;
                    else
                        dx = 1.0f;
                    vgfx_event_t scroll_event = {
                        .type = VGFX_EVENT_SCROLL,
                        .time_ms = timestamp,
                        .data.scroll = {.delta_x = dx,
                                        .delta_y = dy,
                                        .x = x,
                                        .y = y,
                                        .modifiers = x11_modifiers(event.xbutton.state)}};
                    vgfx_internal_enqueue_event(win, &scroll_event);
                    break;
                } else {
                    break; /* Ignore extra buttons */
                }

                if ((int)button >= 0 && button < 8) {
                    win->mouse_button_state[(int)button] = 1; /* Update input state */
                }

                vgfx_event_t vgfx_event = {
                    .type = VGFX_EVENT_MOUSE_DOWN,
                    .time_ms = timestamp,
                    .data.mouse_button = {.x = x,
                                          .y = y,
                                          .button = button,
                                          .modifiers = x11_modifiers(event.xbutton.state)}};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case ButtonRelease: {
                int32_t x = event.xbutton.x;
                int32_t y = event.xbutton.y;
                win->mouse_x = x;
                win->mouse_y = y;

                vgfx_mouse_button_t button = VGFX_MOUSE_LEFT;
                if (event.xbutton.button == Button1) {
                    button = VGFX_MOUSE_LEFT;
                } else if (event.xbutton.button == Button2) {
                    button = VGFX_MOUSE_MIDDLE;
                } else if (event.xbutton.button == Button3) {
                    button = VGFX_MOUSE_RIGHT;
                } else {
                    break; /* Ignore scroll wheel and extra buttons */
                }

                if ((int)button >= 0 && button < 8) {
                    win->mouse_button_state[(int)button] = 0; /* Update input state */
                }

                vgfx_event_t vgfx_event = {
                    .type = VGFX_EVENT_MOUSE_UP,
                    .time_ms = timestamp,
                    .data.mouse_button = {.x = x,
                                          .y = y,
                                          .button = button,
                                          .modifiers = x11_modifiers(event.xbutton.state)}};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case ClientMessage: {
                /* Handle WM_DELETE_WINDOW (window close button clicked) */
                if ((Atom)event.xclient.data.l[0] == x11->wm_delete_window) {
                    if (!win->prevent_close) {
                        x11->close_requested = 1;
                        win->close_requested = 1;
                    }

                    vgfx_event_t vgfx_event = {.type = VGFX_EVENT_CLOSE, .time_ms = timestamp};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                }
                /* XDND: drag entered our window */
                else if (event.xclient.message_type == x11->xdnd_enter) {
                    x11->xdnd_source = (Window)event.xclient.data.l[0];
                }
                /* XDND: drag positioned over our window — accept */
                else if (event.xclient.message_type == x11->xdnd_position) {
                    XEvent reply = {0};
                    reply.type = ClientMessage;
                    reply.xclient.window = x11->xdnd_source;
                    reply.xclient.message_type = x11->xdnd_status;
                    reply.xclient.format = 32;
                    reply.xclient.data.l[0] = (long)x11->window;
                    reply.xclient.data.l[1] = 1; /* Accept drop */
                    reply.xclient.data.l[4] =
                        (long)XInternAtom(x11->display, "XdndActionCopy", False);
                    XSendEvent(x11->display, x11->xdnd_source, False, NoEventMask, &reply);
                    XFlush(x11->display);
                }
                /* XDND: drop completed — request selection data */
                else if (event.xclient.message_type == x11->xdnd_drop) {
                    XConvertSelection(x11->display,
                                      x11->xdnd_selection,
                                      x11->text_uri_list,
                                      x11->xdnd_selection,
                                      x11->window,
                                      CurrentTime);
                }
                break;
            }

            case SelectionNotify: {
                /* XDND: received selection data (file paths as text/uri-list) */
                if (event.xselection.property == x11->xdnd_selection) {
                    char *data =
                        x11_read_text_property(x11, x11->xdnd_selection, x11->text_uri_list);
                    if (data && data[0] != '\0')
                        parse_xdnd_uri_list(
                            win, timestamp, (const unsigned char *)data, strlen(data));
                    free(data);

                    /* Send XdndFinished to complete the protocol */
                    XEvent reply = {0};
                    reply.type = ClientMessage;
                    reply.xclient.window = x11->xdnd_source;
                    reply.xclient.message_type = x11->xdnd_finished;
                    reply.xclient.format = 32;
                    reply.xclient.data.l[0] = (long)x11->window;
                    reply.xclient.data.l[1] = 1; /* Accepted */
                    reply.xclient.data.l[2] =
                        (long)XInternAtom(x11->display, "XdndActionCopy", False);
                    XSendEvent(x11->display, x11->xdnd_source, False, NoEventMask, &reply);
                    XFlush(x11->display);
                    x11->xdnd_source = 0;
                }
                break;
            }

            case SelectionRequest:
                x11_handle_selection_request(x11, &event.xselectionrequest);
                break;

            case SelectionClear:
                if (event.xselectionclear.selection == x11->clipboard_atom) {
                    free(x11->clipboard_text);
                    x11->clipboard_text = NULL;
                }
                break;

            case FocusIn: {
                if (x11->xic)
                    XSetICFocus(x11->xic);
                win->is_focused = 1;
                g_vgfx_cursor_window = win;
                g_vgfx_clipboard_window = win;
                vgfx_event_t vgfx_event = {.type = VGFX_EVENT_FOCUS_GAINED, .time_ms = timestamp};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case FocusOut: {
                if (x11->xic)
                    XUnsetICFocus(x11->xic);
                win->is_focused = 0;
                vgfx_internal_clear_input_state(win);
                vgfx_event_t vgfx_event = {.type = VGFX_EVENT_FOCUS_LOST, .time_ms = timestamp};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case ConfigureNotify: {
                if (event.xconfigure.width > 0 && event.xconfigure.height > 0 &&
                    (event.xconfigure.width != x11->width ||
                     event.xconfigure.height != x11->height)) {
                    int32_t new_w = event.xconfigure.width;
                    int32_t new_h = event.xconfigure.height;
                    (void)x11_resize_backing_store(win, new_w, new_h, timestamp, 1);
                }
                break;
            }

            case Expose: {
                /* Window needs redraw - just note it, vgfx_present will handle */
                break;
            }

            default:
                /* Ignore unhandled event types */
                break;
        }
    }

    return 1;
}

static int x11_mask_byte_index(unsigned long mask) {
    if (!mask)
        return -1;

    int shift = 0;
    while ((mask & 1ul) == 0ul) {
        mask >>= 1;
        shift++;
    }
    if (mask != 0xFFul || (shift % 8) != 0)
        return -1;

    int index = shift / 8;
    return (index >= 0 && index < 4) ? index : -1;
}

static int x11_convert_rgba_to_native32(struct vgfx_window *win, vgfx_x11_data *x11) {
    if (!win || !x11 || !x11->visual || !win->pixels || !x11->ximage_buf)
        return 0;

    int r_index = x11_mask_byte_index(x11->visual->red_mask);
    int g_index = x11_mask_byte_index(x11->visual->green_mask);
    int b_index = x11_mask_byte_index(x11->visual->blue_mask);
    if (r_index < 0 || g_index < 0 || b_index < 0 || r_index == g_index || r_index == b_index ||
        g_index == b_index) {
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Unsupported X11 visual color masks");
        return 0;
    }

    int a_index = -1;
    for (int i = 0; i < 4; i++) {
        if (i != r_index && i != g_index && i != b_index) {
            a_index = i;
            break;
        }
    }

    const uint8_t *src = win->pixels;
    uint8_t *dst = x11->ximage_buf;
    const size_t pixel_count = (size_t)win->width * (size_t)win->height;
    for (size_t i = 0; i < pixel_count; ++i) {
        dst[0] = 0;
        dst[1] = 0;
        dst[2] = 0;
        dst[3] = 0;
        dst[r_index] = src[0];
        dst[g_index] = src[1];
        dst[b_index] = src[2];
        if (a_index >= 0)
            dst[a_index] = src[3];
        src += 4;
        dst += 4;
    }
    return 1;
}

/// @brief Present (blit) the framebuffer to the X11 window.
/// @details Copies the ViperGFX framebuffer (win->pixels) to the X11 window
///          using XPutImage.  The XImage wrapper points directly to our
///          framebuffer, so this is an efficient blit operation.
///
/// @param win Pointer to the ViperGFX window structure
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  win->pixels != NULL (framebuffer valid)
/// @pre  win->platform_data != NULL
/// @post Framebuffer contents visible in X11 window
int vgfx_platform_present(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;
    if (win->skip_software_present)
        return 1;

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window || !x11->ximage)
        return 0;

    if (!x11_convert_rgba_to_native32(win, x11))
        return 0;

    /* Blit presentation buffer to window using XPutImage */
    XPutImage(x11->display,
              x11->window,
              x11->gc,
              x11->ximage,
              0,
              0, /* src x, y */
              0,
              0, /* dst x, y */
              win->width,
              win->height);

    /* Flush to ensure immediate display */
    XFlush(x11->display);

    return 1;
}

/// @brief Get current high-resolution timestamp in milliseconds.
/// @details Returns a monotonic timestamp using CLOCK_MONOTONIC with
///          millisecond precision.  Never decreases, used for frame timing.
///
/// @return Milliseconds since arbitrary epoch (monotonic)
///
/// @post Return value >= previous calls within the same process
int64_t vgfx_platform_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/// @brief Sleep for the specified duration in milliseconds.
/// @details Uses nanosleep() for accurate sub-second delays.  If ms <= 0,
///          returns immediately without sleeping.  Used for FPS limiting.
///
/// @param ms Duration to sleep in milliseconds
void vgfx_platform_sleep_ms(int32_t ms) {
    if (ms > 0) {
        struct timespec ts;
        ts.tv_sec = ms / 1000;
        ts.tv_nsec = (ms % 1000) * 1000000;
        nanosleep(&ts, NULL);
    }
}

//===----------------------------------------------------------------------===//
// Window Title and Fullscreen
//===----------------------------------------------------------------------===//

/// @brief Set the window title.
/// @details Updates the X11 window's title using XStoreName.
///
/// @param win   Pointer to the window structure
/// @param title New title string (UTF-8)
void vgfx_platform_set_title(struct vgfx_window *win, const char *title) {
    if (!win || !win->platform_data || !title)
        return;

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return;

    XStoreName(x11->display, x11->window, title);
    XSetIconName(x11->display, x11->window, title);
    XFlush(x11->display);
}

/// @brief Set the window to fullscreen or windowed mode.
/// @details Uses the EWMH _NET_WM_STATE_FULLSCREEN hint to toggle fullscreen.
///          This is the standard way to request fullscreen on modern X11
///          window managers (GNOME, KDE, etc.).
///
/// @param win        Pointer to the window structure
/// @param fullscreen 1 for fullscreen, 0 for windowed
/// @return 1 on success, 0 on failure
int vgfx_platform_set_fullscreen(struct vgfx_window *win, int fullscreen) {
    if (!win || !win->platform_data)
        return 0;

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return 0;

    /* Get the EWMH atoms for fullscreen state */
    Atom wm_state = XInternAtom(x11->display, "_NET_WM_STATE", False);
    Atom wm_fullscreen = XInternAtom(x11->display, "_NET_WM_STATE_FULLSCREEN", False);

    if (wm_state == None || wm_fullscreen == None)
        return 0;

    /* Send a client message to the window manager to change fullscreen state */
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.window = x11->window;
    event.xclient.message_type = wm_state;
    event.xclient.format = 32;
    event.xclient.data.l[0] = fullscreen ? 1 : 0; /* _NET_WM_STATE_ADD or _NET_WM_STATE_REMOVE */
    event.xclient.data.l[1] = (long)wm_fullscreen;
    event.xclient.data.l[2] = 0; /* No second property */
    event.xclient.data.l[3] = 1; /* Source indication: normal application */

    XSendEvent(x11->display,
               DefaultRootWindow(x11->display),
               False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &event);

    XFlush(x11->display);
    return 1;
}

/// @brief Check if the window is in fullscreen mode.
/// @details Queries the _NET_WM_STATE property to check for fullscreen.
///
/// @param win Pointer to the window structure
/// @return 1 if fullscreen, 0 if windowed
int vgfx_platform_is_fullscreen(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return 0;

    Atom wm_state = XInternAtom(x11->display, "_NET_WM_STATE", False);
    Atom wm_fullscreen = XInternAtom(x11->display, "_NET_WM_STATE_FULLSCREEN", False);

    if (wm_state == None || wm_fullscreen == None)
        return 0;

    /* Query the _NET_WM_STATE property */
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    int status = XGetWindowProperty(x11->display,
                                    x11->window,
                                    wm_state,
                                    0,
                                    1024,
                                    False,
                                    XA_ATOM,
                                    &actual_type,
                                    &actual_format,
                                    &nitems,
                                    &bytes_after,
                                    &data);

    if (status != Success || !data)
        return 0;

    /* Check if _NET_WM_STATE_FULLSCREEN is in the list */
    int is_fullscreen = 0;
    Atom *atoms = (Atom *)data;
    for (unsigned long i = 0; i < nitems; i++) {
        if (atoms[i] == wm_fullscreen) {
            is_fullscreen = 1;
            break;
        }
    }

    XFree(data);
    return is_fullscreen;
}

/// @brief Send a _NET_WM_STATE client message to the window manager.
static void x11_send_wm_state(vgfx_x11_data *x11, int action, Atom atom1, Atom atom2) {
    // action: 0 = remove, 1 = add, 2 = toggle
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.xclient.window = x11->window;
    event.xclient.message_type = XInternAtom(x11->display, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = action;
    event.xclient.data.l[1] = (long)atom1;
    event.xclient.data.l[2] = (long)atom2;
    event.xclient.data.l[3] = 1; // source indication: normal application
    XSendEvent(x11->display,
               DefaultRootWindow(x11->display),
               False,
               SubstructureNotifyMask | SubstructureRedirectMask,
               &event);
    XFlush(x11->display);
}

void vgfx_platform_minimize(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (x11->display && x11->window) {
        XIconifyWindow(x11->display, x11->window, x11->screen);
        XFlush(x11->display);
    }
}

void vgfx_platform_maximize(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return;
    Atom hz = XInternAtom(x11->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom vt = XInternAtom(x11->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    x11_send_wm_state(x11, 1, hz, vt);
}

void vgfx_platform_restore(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return;
    // Remove maximized state
    Atom hz = XInternAtom(x11->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom vt = XInternAtom(x11->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    x11_send_wm_state(x11, 0, hz, vt);
    // Deiconify if minimized
    XMapWindow(x11->display, x11->window);
    XFlush(x11->display);
}

int32_t vgfx_platform_is_minimized(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return 0;
    Atom wm_state_atom = XInternAtom(x11->display, "_NET_WM_STATE", False);
    Atom hidden = XInternAtom(x11->display, "_NET_WM_STATE_HIDDEN", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    int status = XGetWindowProperty(x11->display,
                                    x11->window,
                                    wm_state_atom,
                                    0,
                                    1024,
                                    False,
                                    XA_ATOM,
                                    &actual_type,
                                    &actual_format,
                                    &nitems,
                                    &bytes_after,
                                    &data);
    if (status != Success || !data)
        return 0;
    int found = 0;
    Atom *atoms = (Atom *)data;
    for (unsigned long i = 0; i < nitems; i++) {
        if (atoms[i] == hidden) {
            found = 1;
            break;
        }
    }
    XFree(data);
    return found;
}

int32_t vgfx_platform_is_maximized(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return 0;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return 0;
    Atom wm_state_atom = XInternAtom(x11->display, "_NET_WM_STATE", False);
    Atom hz = XInternAtom(x11->display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom vt = XInternAtom(x11->display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;
    int status = XGetWindowProperty(x11->display,
                                    x11->window,
                                    wm_state_atom,
                                    0,
                                    1024,
                                    False,
                                    XA_ATOM,
                                    &actual_type,
                                    &actual_format,
                                    &nitems,
                                    &bytes_after,
                                    &data);
    if (status != Success || !data)
        return 0;
    int found_hz = 0;
    int found_vt = 0;
    Atom *atoms = (Atom *)data;
    for (unsigned long i = 0; i < nitems; i++) {
        if (atoms[i] == hz)
            found_hz = 1;
        else if (atoms[i] == vt)
            found_vt = 1;
    }
    XFree(data);
    return found_hz && found_vt;
}

void vgfx_platform_get_position(struct vgfx_window *win, int32_t *out_x, int32_t *out_y) {
    if (!win || !win->platform_data) {
        if (out_x)
            *out_x = 0;
        if (out_y)
            *out_y = 0;
        return;
    }
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return;
    Window child;
    int x = 0, y = 0;
    XWindowAttributes attrs;
    XGetWindowAttributes(x11->display, x11->window, &attrs);
    XTranslateCoordinates(x11->display, x11->window, attrs.root, 0, 0, &x, &y, &child);
    if (out_x)
        *out_x = (int32_t)x;
    if (out_y)
        *out_y = (int32_t)y;
}

void vgfx_platform_set_position(struct vgfx_window *win, int32_t x, int32_t y) {
    if (!win || !win->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (x11->display && x11->window) {
        XMoveWindow(x11->display, x11->window, (int)x, (int)y);
        XFlush(x11->display);
    }
}

void vgfx_platform_focus(struct vgfx_window *win) {
    if (!win || !win->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (x11->display && x11->window) {
        XSetInputFocus(x11->display, x11->window, RevertToParent, CurrentTime);
        XFlush(x11->display);
    }
}

int32_t vgfx_platform_is_focused(struct vgfx_window *win) {
    if (!win)
        return 0;
    return win->is_focused;
}

void vgfx_platform_set_prevent_close(struct vgfx_window *win, int32_t prevent) {
    if (win)
        win->prevent_close = prevent;
}

static unsigned int x11_cursor_shape_for_type(int32_t cursor_type) {
    switch (cursor_type) {
        case 1:
            return XC_hand2;
        case 2:
            return XC_xterm;
        case 3:
            return XC_sb_h_double_arrow;
        case 4:
            return XC_sb_v_double_arrow;
        case 5:
            return XC_watch;
        default:
            return XC_left_ptr;
    }
}

static void x11_apply_visible_cursor(vgfx_x11_data *x11) {
    if (!x11 || !x11->display || !x11->window)
        return;
    Cursor cursor = XCreateFontCursor(x11->display, x11_cursor_shape_for_type(x11->cursor_type));
    if (!cursor)
        return;
    XDefineCursor(x11->display, x11->window, cursor);
    XFreeCursor(x11->display, cursor);
}

void vgfx_platform_set_cursor(struct vgfx_window *win, int32_t cursor_type) {
    if (!win || !win->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return;

    x11->cursor_type = cursor_type;
    if (!x11->cursor_visible)
        return;

    x11_apply_visible_cursor(x11);
    XFlush(x11->display);
}

static Cursor x11_blank_cursor(vgfx_x11_data *x11) {
    if (!x11 || !x11->display || !x11->window)
        return 0;
    if (x11->blank_cursor)
        return x11->blank_cursor;

    Pixmap blank = XCreatePixmap(x11->display, x11->window, 1, 1, 1);
    if (!blank)
        return 0;
    XColor dummy;
    memset(&dummy, 0, sizeof(dummy));
    x11->blank_cursor = XCreatePixmapCursor(x11->display, blank, blank, &dummy, &dummy, 0, 0);
    XFreePixmap(x11->display, blank);
    return x11->blank_cursor;
}

void vgfx_platform_set_cursor_visible(struct vgfx_window *win, int32_t visible) {
    if (!win || !win->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return;

    x11->cursor_visible = visible ? 1 : 0;
    if (visible) {
        x11_apply_visible_cursor(x11);
    } else {
        Cursor invisible = x11_blank_cursor(x11);
        if (!invisible)
            return;
        XDefineCursor(x11->display, x11->window, invisible);
    }
    XFlush(x11->display);
}

void vgfx_platform_hide_cursor(void) {
    if (g_vgfx_cursor_window)
        vgfx_platform_set_cursor_visible(g_vgfx_cursor_window, 0);
}

void vgfx_platform_show_cursor(void) {
    if (g_vgfx_cursor_window)
        vgfx_platform_set_cursor_visible(g_vgfx_cursor_window, 1);
}

void vgfx_platform_get_monitor_size(struct vgfx_window *win, int32_t *out_w, int32_t *out_h) {
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;

    Display *display = NULL;
    int screen = 0;
    int close_display = 0;

    if (win && win->platform_data) {
        vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
        display = x11->display;
        screen = x11->screen;
    } else {
        display = XOpenDisplay(NULL);
        close_display = 1;
        if (display)
            screen = DefaultScreen(display);
    }

    if (!display)
        return;
    if (out_w)
        *out_w = (int32_t)DisplayWidth(display, screen);
    if (out_h)
        *out_h = (int32_t)DisplayHeight(display, screen);
    if (close_display)
        XCloseDisplay(display);
}

void vgfx_platform_set_window_size(struct vgfx_window *win, int32_t w, int32_t h) {
    if (!win || !win->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window)
        return;
    int32_t physical_w = x11_logical_to_physical(win, w);
    int32_t physical_h = x11_logical_to_physical(win, h);
    if (physical_w <= 0 || physical_h <= 0)
        return;
    (void)x11_resize_backing_store(win, physical_w, physical_h, vgfx_platform_now_ms(), 1);
    XResizeWindow(x11->display, x11->window, (unsigned int)physical_w, (unsigned int)physical_h);
    XFlush(x11->display);
}

// ============================================================================
// Clipboard (ICCCM CLIPBOARD selection)
// ============================================================================

int vgfx_clipboard_has_format(vgfx_clipboard_format_t format) {
    char *text = NULL;
    int has_text = 0;

    if (format != VGFX_CLIPBOARD_TEXT)
        return 0;

    text = vgfx_clipboard_get_text();
    has_text = text && text[0] != '\0';
    free(text);
    return has_text ? 1 : 0;
}

char *vgfx_clipboard_get_text(void) {
    vgfx_x11_data *x11 = NULL;

    struct vgfx_window *owner = x11_clipboard_window();
    if (!owner || !owner->platform_data)
        return NULL;

    x11 = (vgfx_x11_data *)owner->platform_data;
    if (!x11 || !x11->display || !x11->window)
        return NULL;

    if (x11->clipboard_text && XGetSelectionOwner(x11->display, x11->clipboard_atom) == x11->window)
        return x11_strdup_text(x11->clipboard_text);

    Atom targets[2] = {x11->utf8_string_atom, XA_STRING};
    for (size_t target_index = 0; target_index < 2; target_index++) {
        Atom target = targets[target_index];
        XDeleteProperty(x11->display, x11->window, x11->clipboard_property_atom);
        XConvertSelection(x11->display,
                          x11->clipboard_atom,
                          target,
                          x11->clipboard_property_atom,
                          x11->window,
                          CurrentTime);
        XFlush(x11->display);

        int64_t start = vgfx_platform_now_ms();
        int target_done = 0;
        while (!target_done && vgfx_platform_now_ms() - start < 100) {
            XEvent event;
            if (XCheckIfEvent(x11->display,
                              &event,
                              x11_clipboard_selection_notify_predicate,
                              (XPointer)x11)) {
                if (event.xselection.property == None) {
                    target_done = 1;
                    continue;
                }

                char *copy = x11_read_text_property(x11, x11->clipboard_property_atom, target);
                if (copy)
                    return copy;
                target_done = 1;
            } else {
                usleep(1000);
            }
        }
    }

    return NULL;
}

void vgfx_clipboard_set_text(const char *text) {
    vgfx_x11_data *x11 = NULL;

    struct vgfx_window *owner = x11_clipboard_window();
    if (!owner || !owner->platform_data)
        return;

    x11 = (vgfx_x11_data *)owner->platform_data;
    if (!x11 || !x11->display || !x11->window)
        return;

    char *copy = x11_strdup_text(text);
    if (!copy)
        return;

    free(x11->clipboard_text);
    x11->clipboard_text = copy;
    XSetSelectionOwner(x11->display, x11->clipboard_atom, x11->window, CurrentTime);
    if (XGetSelectionOwner(x11->display, x11->clipboard_atom) != x11->window) {
        free(x11->clipboard_text);
        x11->clipboard_text = NULL;
    }
    XFlush(x11->display);
}

void vgfx_clipboard_clear(void) {
    vgfx_clipboard_set_text("");
}

void *vgfx_get_native_view(vgfx_window_t window) {
    if (!window)
        return NULL;
    vgfx_x11_data *x11 = (vgfx_x11_data *)window->platform_data;
    if (!x11)
        return NULL;
    return (void *)(uintptr_t)x11->window; /* X11 Window is unsigned long */
}

void *vgfx_get_native_display(vgfx_window_t window) {
    if (!window)
        return NULL;
    vgfx_x11_data *x11 = (vgfx_x11_data *)window->platform_data;
    if (!x11)
        return NULL;
    return (void *)x11->display;
}

void vgfx_platform_warp_cursor(vgfx_window_t window, int32_t x, int32_t y) {
    if (!window || !window->platform_data)
        return;
    vgfx_x11_data *x11 = (vgfx_x11_data *)window->platform_data;
    if (!x11->display || !x11->window)
        return;
    float cs = vgfx_internal_coord_scale(window);
    XWarpPointer(x11->display,
                 None,
                 x11->window,
                 0,
                 0,
                 0,
                 0,
                 vgfx_internal_scale_up_i32(x, cs),
                 vgfx_internal_scale_up_i32(y, cs));
    XFlush(x11->display);
}

#endif /* __linux__ || __unix__ */
