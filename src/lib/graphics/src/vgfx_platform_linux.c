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
typedef struct
{
    Display *display;      ///< X11 connection to server
    int screen;            ///< Screen number
    Window window;         ///< Native X11 window handle
    GC gc;                 ///< Graphics context for drawing
    Atom wm_delete_window; ///< Atom for WM_DELETE_WINDOW protocol
    XImage *ximage;        ///< XImage wrapper for framebuffer
    int width;             ///< Cached window width
    int height;            ///< Cached window height
    int close_requested;   ///< 1 if WM_DELETE_WINDOW received, 0 otherwise
} vgfx_x11_data;

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
static vgfx_key_t translate_keysym(KeySym keysym)
{
    /* Lowercase letters (convert to uppercase) */
    if (keysym >= XK_a && keysym <= XK_z)
    {
        return (vgfx_key_t)('A' + (keysym - XK_a));
    }

    /* Uppercase letters */
    if (keysym >= XK_A && keysym <= XK_Z)
    {
        return (vgfx_key_t)keysym;
    }

    /* Digits 0-9 */
    if (keysym >= XK_0 && keysym <= XK_9)
    {
        return (vgfx_key_t)keysym;
    }

    /* Special keys */
    switch (keysym)
    {
        case XK_space:
            return VGFX_KEY_SPACE;
        case XK_Return:
            return VGFX_KEY_ENTER;
        case XK_KP_Enter:
            return VGFX_KEY_ENTER;
        case XK_Escape:
            return VGFX_KEY_ESCAPE;
        case XK_Left:
            return VGFX_KEY_LEFT;
        case XK_Right:
            return VGFX_KEY_RIGHT;
        case XK_Up:
            return VGFX_KEY_UP;
        case XK_Down:
            return VGFX_KEY_DOWN;
        default:
            return VGFX_KEY_UNKNOWN;
    }
}

//===----------------------------------------------------------------------===//
// Platform API Implementation
//===----------------------------------------------------------------------===//

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
int vgfx_platform_init_window(struct vgfx_window *win, const vgfx_window_params_t *params)
{
    if (!win || !params)
        return 0;

    /* Allocate platform data structure */
    vgfx_x11_data *x11 = (vgfx_x11_data *)calloc(1, sizeof(vgfx_x11_data));
    if (!x11)
    {
        vgfx_internal_set_error(VGFX_ERR_ALLOC, "Failed to allocate X11 platform data");
        return 0;
    }

    win->platform_data = x11;
    x11->close_requested = 0;
    x11->width = params->width;
    x11->height = params->height;

    /* Open connection to X server */
    x11->display = XOpenDisplay(NULL);
    if (!x11->display)
    {
        free(x11);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to open X11 display");
        return 0;
    }

    x11->screen = DefaultScreen(x11->display);
    Window root = RootWindow(x11->display, x11->screen);

    /* Create window with 32-bit depth for RGBA support */
    XSetWindowAttributes attrs;
    attrs.background_pixel = BlackPixel(x11->display, x11->screen);
    attrs.border_pixel = BlackPixel(x11->display, x11->screen);
    attrs.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | ExposureMask | FocusChangeMask | StructureNotifyMask;

    x11->window = XCreateWindow(x11->display,
                                root,
                                0,
                                0, /* x, y position (will be overridden) */
                                params->width,
                                params->height,
                                0,              /* border width */
                                CopyFromParent, /* depth */
                                InputOutput,    /* class */
                                CopyFromParent, /* visual */
                                CWBackPixel | CWBorderPixel | CWEventMask,
                                &attrs);

    if (!x11->window)
    {
        XCloseDisplay(x11->display);
        free(x11);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create X11 window");
        return 0;
    }

    /* Set window title */
    XStoreName(x11->display, x11->window, params->title);
    XSetIconName(x11->display, x11->window, params->title);

    /* Set window size hints (prevents resizing if not resizable) */
    XSizeHints *size_hints = XAllocSizeHints();
    if (size_hints)
    {
        size_hints->flags = PSize | PMinSize | PMaxSize;
        size_hints->width = params->width;
        size_hints->height = params->height;
        size_hints->min_width = params->resizable ? 1 : params->width;
        size_hints->min_height = params->resizable ? 1 : params->height;
        size_hints->max_width = params->resizable ? 16384 : params->width;
        size_hints->max_height = params->resizable ? 16384 : params->height;
        XSetWMNormalHints(x11->display, x11->window, size_hints);
        XFree(size_hints);
    }

    /* Set up WM_DELETE_WINDOW protocol (intercept close button) */
    x11->wm_delete_window = XInternAtom(x11->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x11->display, x11->window, &x11->wm_delete_window, 1);

    /* Create graphics context */
    x11->gc = XCreateGC(x11->display, x11->window, 0, NULL);
    if (!x11->gc)
    {
        XDestroyWindow(x11->display, x11->window);
        XCloseDisplay(x11->display);
        free(x11);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create X11 GC");
        return 0;
    }

    /* Create XImage wrapper for framebuffer (32-bit RGBA, ZPixmap) */
    /* Note: XImage does NOT own the pixel data; it just wraps win->pixels */
    x11->ximage = XCreateImage(x11->display,
                               DefaultVisual(x11->display, x11->screen),
                               24,                  /* depth (24-bit RGB, ignore alpha) */
                               ZPixmap,             /* format */
                               0,                   /* offset */
                               (char *)win->pixels, /* data pointer (points to our framebuffer) */
                               params->width,
                               params->height,
                               32,         /* bitmap_pad (32-bit alignment) */
                               win->stride /* bytes_per_line */
    );

    if (!x11->ximage)
    {
        XFreeGC(x11->display, x11->gc);
        XDestroyWindow(x11->display, x11->window);
        XCloseDisplay(x11->display);
        free(x11);
        win->platform_data = NULL;
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to create XImage");
        return 0;
    }

    /* Set byte order for XImage (LSBFirst for little-endian RGBA) */
    x11->ximage->byte_order = LSBFirst;

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
void vgfx_platform_destroy_window(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return;

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;

    /* Destroy XImage (does NOT free win->pixels, we manage that separately) */
    if (x11->ximage)
    {
        x11->ximage->data = NULL; /* Prevent XDestroyImage from freeing our pixels */
        XDestroyImage(x11->ximage);
        x11->ximage = NULL;
    }

    /* Free graphics context */
    if (x11->gc)
    {
        XFreeGC(x11->display, x11->gc);
        x11->gc = NULL;
    }

    /* Destroy window */
    if (x11->window)
    {
        XDestroyWindow(x11->display, x11->window);
        x11->window = 0;
    }

    /* Close display connection */
    if (x11->display)
    {
        XCloseDisplay(x11->display);
        x11->display = NULL;
    }

    /* Free platform data */
    free(x11);
    win->platform_data = NULL;
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
int vgfx_platform_process_events(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return 0;

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display)
        return 0;

    /* Process all pending events without blocking */
    while (XPending(x11->display) > 0)
    {
        XEvent event;
        XNextEvent(x11->display, &event);

        int64_t timestamp = vgfx_platform_now_ms();

        switch (event.type)
        {
            case KeyPress:
            {
                KeySym keysym = XLookupKeysym(&event.xkey, 0);
                vgfx_key_t key = translate_keysym(keysym);

                if (key != VGFX_KEY_UNKNOWN && key < 512)
                {
                    int is_repeat = win->key_state[key]; /* Already pressed = repeat */
                    win->key_state[key] = 1;             /* Update input state */

                    vgfx_event_t vgfx_event = {.type = VGFX_EVENT_KEY_DOWN,
                                               .time_ms = timestamp,
                                               .data.key = {.key = key, .is_repeat = is_repeat}};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                }
                break;
            }

            case KeyRelease:
            {
                /* X11 generates repeated KeyRelease/KeyPress pairs for key repeat.
                 * We detect true release by checking if there's an immediate KeyPress. */
                if (XEventsQueued(x11->display, QueuedAfterReading))
                {
                    XEvent next_event;
                    XPeekEvent(x11->display, &next_event);

                    /* If next event is KeyPress for same key, it's a repeat - ignore release */
                    if (next_event.type == KeyPress && next_event.xkey.time == event.xkey.time &&
                        next_event.xkey.keycode == event.xkey.keycode)
                    {
                        break; /* Ignore this release event */
                    }
                }

                KeySym keysym = XLookupKeysym(&event.xkey, 0);
                vgfx_key_t key = translate_keysym(keysym);

                if (key != VGFX_KEY_UNKNOWN && key < 512)
                {
                    win->key_state[key] = 0; /* Update input state */

                    vgfx_event_t vgfx_event = {.type = VGFX_EVENT_KEY_UP,
                                               .time_ms = timestamp,
                                               .data.key = {.key = key, .is_repeat = 0}};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                }
                break;
            }

            case MotionNotify:
            {
                int32_t x = event.xmotion.x;
                int32_t y = event.xmotion.y;

                win->mouse_x = x; /* Update input state */
                win->mouse_y = y;

                vgfx_event_t vgfx_event = {.type = VGFX_EVENT_MOUSE_MOVE,
                                           .time_ms = timestamp,
                                           .data.mouse_move = {.x = x, .y = y}};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case ButtonPress:
            {
                int32_t x = event.xbutton.x;
                int32_t y = event.xbutton.y;

                /* X11 mouse button mapping:
                 *   Button1 = Left (1)
                 *   Button2 = Middle (2)
                 *   Button3 = Right (3)
                 *   Button4/5 = Scroll wheel (ignored in v1)
                 */
                vgfx_mouse_button_t button = VGFX_MOUSE_LEFT;
                if (event.xbutton.button == Button1)
                {
                    button = VGFX_MOUSE_LEFT;
                }
                else if (event.xbutton.button == Button2)
                {
                    button = VGFX_MOUSE_MIDDLE;
                }
                else if (event.xbutton.button == Button3)
                {
                    button = VGFX_MOUSE_RIGHT;
                }
                else
                {
                    break; /* Ignore scroll wheel and extra buttons */
                }

                if (button < 8)
                {
                    win->mouse_button_state[button] = 1; /* Update input state */
                }

                vgfx_event_t vgfx_event = {.type = VGFX_EVENT_MOUSE_DOWN,
                                           .time_ms = timestamp,
                                           .data.mouse_button = {.x = x, .y = y, .button = button}};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case ButtonRelease:
            {
                int32_t x = event.xbutton.x;
                int32_t y = event.xbutton.y;

                vgfx_mouse_button_t button = VGFX_MOUSE_LEFT;
                if (event.xbutton.button == Button1)
                {
                    button = VGFX_MOUSE_LEFT;
                }
                else if (event.xbutton.button == Button2)
                {
                    button = VGFX_MOUSE_MIDDLE;
                }
                else if (event.xbutton.button == Button3)
                {
                    button = VGFX_MOUSE_RIGHT;
                }
                else
                {
                    break; /* Ignore scroll wheel and extra buttons */
                }

                if (button < 8)
                {
                    win->mouse_button_state[button] = 0; /* Update input state */
                }

                vgfx_event_t vgfx_event = {.type = VGFX_EVENT_MOUSE_UP,
                                           .time_ms = timestamp,
                                           .data.mouse_button = {.x = x, .y = y, .button = button}};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case ClientMessage:
            {
                /* Handle WM_DELETE_WINDOW (window close button clicked) */
                if ((Atom)event.xclient.data.l[0] == x11->wm_delete_window)
                {
                    x11->close_requested = 1;

                    vgfx_event_t vgfx_event = {.type = VGFX_EVENT_CLOSE, .time_ms = timestamp};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                }
                break;
            }

            case FocusIn:
            {
                vgfx_event_t vgfx_event = {.type = VGFX_EVENT_FOCUS_GAINED, .time_ms = timestamp};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case FocusOut:
            {
                vgfx_event_t vgfx_event = {.type = VGFX_EVENT_FOCUS_LOST, .time_ms = timestamp};
                vgfx_internal_enqueue_event(win, &vgfx_event);
                break;
            }

            case ConfigureNotify:
            {
                /* Window resized - note: full resize support not in v1 */
                if (event.xconfigure.width != x11->width || event.xconfigure.height != x11->height)
                {
                    x11->width = event.xconfigure.width;
                    x11->height = event.xconfigure.height;

                    vgfx_event_t vgfx_event = {.type = VGFX_EVENT_RESIZE,
                                               .time_ms = timestamp,
                                               .data.resize = {.width = event.xconfigure.width,
                                                               .height = event.xconfigure.height}};
                    vgfx_internal_enqueue_event(win, &vgfx_event);
                }
                break;
            }

            case Expose:
            {
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
int vgfx_platform_present(struct vgfx_window *win)
{
    if (!win || !win->platform_data)
        return 0;

    vgfx_x11_data *x11 = (vgfx_x11_data *)win->platform_data;
    if (!x11->display || !x11->window || !x11->ximage)
        return 0;

    /* Blit framebuffer to window using XPutImage */
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
int64_t vgfx_platform_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/// @brief Sleep for the specified duration in milliseconds.
/// @details Uses nanosleep() for accurate sub-second delays.  If ms <= 0,
///          returns immediately without sleeping.  Used for FPS limiting.
///
/// @param ms Duration to sleep in milliseconds
void vgfx_platform_sleep_ms(int32_t ms)
{
    if (ms > 0)
    {
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
void vgfx_platform_set_title(struct vgfx_window *win, const char *title)
{
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
int vgfx_platform_set_fullscreen(struct vgfx_window *win, int fullscreen)
{
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
int vgfx_platform_is_fullscreen(struct vgfx_window *win)
{
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
    for (unsigned long i = 0; i < nitems; i++)
    {
        if (atoms[i] == wm_fullscreen)
        {
            is_fullscreen = 1;
            break;
        }
    }

    XFree(data);
    return is_fullscreen;
}

#endif /* __linux__ || __unix__ */
