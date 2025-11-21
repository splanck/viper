//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperGFX Core Implementation
//
// Platform-agnostic implementation of the ViperGFX API.  Provides window
// lifecycle management, event queue operations, framebuffer operations,
// and input polling.  Platform-specific functionality is delegated to the
// platform backend via function pointers defined in vgfx_internal.h.
//
// Key Design Decisions:
//   - Thread-Local Error Storage: Errors are thread-local (C11 _Thread_local)
//     so concurrent windows can have independent error states.
//   - Lock-Free Event Queue: Uses SPSC ring buffer with FIFO eviction policy
//     that prioritizes CLOSE events.
//   - Aligned Framebuffer: Allocated with VGFX_FRAMEBUFFER_ALIGNMENT for
//     cache performance and potential SIMD optimizations.
//   - Integer-Only Math: All coordinates and dimensions use int32_t for
//     deterministic, portable behavior.
//   - FPS Limiting: Deadline-based scheduling that resyncs if falling behind.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Core implementation of the ViperGFX API (platform-agnostic).
/// @details Implements window management, event handling, drawing operations,
///          and input polling.  Delegates OS-specific tasks to the platform
///          backend (vgfx_platform_*.c).

#include "vgfx.h"
#include "vgfx_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

//===----------------------------------------------------------------------===//
// Platform-Specific Headers for Aligned Allocation
//===----------------------------------------------------------------------===//

#if defined(_WIN32)
    #include <malloc.h>
#elif defined(__APPLE__) || defined(__linux__)
    #include <stdlib.h>
#endif

//===----------------------------------------------------------------------===//
// Thread-Local Error State
//===----------------------------------------------------------------------===//
// Errors are stored per-thread so concurrent windows can have independent
// error states.  Uses C11 _Thread_local on conforming compilers, falls back
// to platform-specific TLS on Windows, and global storage as last resort.
//===----------------------------------------------------------------------===//

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    /// @brief C11 thread-local error message string.
    _Thread_local static const char* g_last_error_str = NULL;
    /// @brief C11 thread-local error code.
    _Thread_local static vgfx_error_t g_last_error_code = VGFX_ERR_NONE;
#elif defined(_WIN32)
    /// @brief Windows TLS error message string.
    __declspec(thread) static const char* g_last_error_str = NULL;
    /// @brief Windows TLS error code.
    __declspec(thread) static vgfx_error_t g_last_error_code = VGFX_ERR_NONE;
#else
    /// @brief Fallback global error message (not thread-safe).
    static const char* g_last_error_str = NULL;
    /// @brief Fallback global error code (not thread-safe).
    static vgfx_error_t g_last_error_code = VGFX_ERR_NONE;
#endif

//===----------------------------------------------------------------------===//
// Global State
//===----------------------------------------------------------------------===//

/// @brief Global default FPS applied when window params specify fps == 0.
static int32_t g_default_fps = VGFX_DEFAULT_FPS;

/// @brief Optional user-provided logging callback for error messages.
static vgfx_log_fn g_log_callback = NULL;

//===----------------------------------------------------------------------===//
// Internal Helper Functions
//===----------------------------------------------------------------------===//

/// @brief Set the thread-local error state and invoke logging.
/// @details Stores the error code/message in TLS, prints to stderr, and
///          calls the user-provided log callback (if any).  This function
///          is called by both the core library and platform backends when
///          an error occurs.
///
/// @param code Error code (enum vgfx_error_t)
/// @param msg  Descriptive error message (UTF-8, may be NULL)
///
/// @post g_last_error_code == code
/// @post g_last_error_str == msg
/// @post Message printed to stderr (if msg != NULL)
/// @post Log callback invoked (if set and msg != NULL)
void vgfx_internal_set_error(vgfx_error_t code, const char* msg) {
    g_last_error_code = code;
    g_last_error_str = msg;

    /* Print to stderr */
    if (msg) {
        fprintf(stderr, "vgfx: %s\n", msg);
    }

    /* Call logging callback if set */
    if (g_log_callback && msg) {
        g_log_callback(msg);
    }
}

/// @brief Clamp an integer value to the range [min, max].
/// @details Returns the value unchanged if within range, otherwise returns
///          the nearest boundary.  Used for sanitizing FPS values.
///
/// @param value Input value to clamp
/// @param min   Minimum allowed value
/// @param max   Maximum allowed value
/// @return value clamped to [min, max]
static inline int32_t clamp_int(int32_t value, int32_t min, int32_t max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/// @brief Allocate memory aligned to a specified boundary.
/// @details Wrapper around platform-specific aligned allocation functions:
///            - Windows: _aligned_malloc()
///            - POSIX:   posix_memalign()
///            - Fallback: malloc() (may not be aligned)
///
/// @param alignment Alignment boundary in bytes (must be power of 2)
/// @param size      Number of bytes to allocate
/// @return Pointer to aligned memory on success, NULL on failure
///
/// @pre  alignment is a power of 2
/// @post On success: returned pointer is aligned to alignment
static void* aligned_alloc_wrapper(size_t alignment, size_t size) {
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#elif defined(__APPLE__) || defined(__linux__)
    void* ptr = NULL;
    if (posix_memalign(&ptr, alignment, size) == 0) {
        return ptr;
    }
    return NULL;
#else
    /* Fallback to regular malloc (may not satisfy alignment requirement) */
    (void)alignment; /* Suppress unused parameter warning */
    return malloc(size);
#endif
}

/// @brief Free memory allocated by aligned_alloc_wrapper().
/// @details Uses the appropriate platform-specific deallocation function.
///
/// @param ptr Pointer to memory returned by aligned_alloc_wrapper() (may be NULL)
///
/// @post Memory is freed and ptr is invalid
static void aligned_free_wrapper(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

//===----------------------------------------------------------------------===//
// Event Queue Implementation (Lock-Free SPSC Ring Buffer)
//===----------------------------------------------------------------------===//
// Single producer (platform thread), single consumer (application thread).
// Uses FIFO eviction when full, with special handling for CLOSE events.
//===----------------------------------------------------------------------===//

/// @brief Enqueue an event into the window's ring buffer.
/// @details Attempts to add the event to the queue.  If the queue is full,
///          implements FIFO eviction with the following policy:
///            - If oldest event is CLOSE: drop new event (unless also CLOSE)
///            - If oldest event is not CLOSE: drop oldest, enqueue new
///            - Dropped events (except CLOSE) increment event_overflow counter
///
///          This ensures CLOSE events are never lost once enqueued.
///
/// @param win   Pointer to the window structure
/// @param event Pointer to the event to enqueue (copied into queue)
/// @return 1 if event was enqueued, 0 if dropped (queue full)
///
/// @pre  win != NULL
/// @pre  event != NULL
/// @post Event is in queue OR event_overflow incremented (non-CLOSE dropped)
int vgfx_internal_enqueue_event(struct vgfx_window* win, const vgfx_event_t* event) {
    if (!win || !event) return 0;

    int32_t next_head = (win->event_head + 1) % VGFX_INTERNAL_EVENT_QUEUE_SLOTS;

    /* Queue full? */
    if (next_head == win->event_tail) {
        /* FIFO eviction: drop oldest event to make room for new event.
         * Exception: Never drop CLOSE events - they signal window destruction. */
        if (win->event_queue[win->event_tail].type == VGFX_EVENT_CLOSE) {
            /* Oldest event is CLOSE - can't drop it */
            if (event->type == VGFX_EVENT_CLOSE) {
                /* Duplicate CLOSE event - drop the new one (no overflow increment) */
                return 0;
            } else {
                /* New event is regular - drop it to preserve CLOSE */
                win->event_overflow++;
                return 0;
            }
        } else {
            /* Oldest event is not CLOSE - drop it to make room for new event */
            win->event_tail = (win->event_tail + 1) % VGFX_INTERNAL_EVENT_QUEUE_SLOTS;
            win->event_overflow++;
        }
    }

    /* Enqueue event (queue now has space) */
    win->event_queue[win->event_head] = *event;
    win->event_head = next_head;
    return 1;
}

/// @brief Dequeue the next event from the window's ring buffer.
/// @details Removes and returns the oldest event from the queue.  If the queue
///          is empty, returns 0 without modifying out_event.
///
/// @param win       Pointer to the window structure
/// @param out_event Pointer to event storage (filled on success)
/// @return 1 if an event was dequeued, 0 if queue is empty
///
/// @pre  win != NULL
/// @pre  out_event != NULL
/// @post If 1 returned: out_event contains oldest event, event_tail advanced
/// @post If 0 returned: out_event unchanged, queue is empty
int vgfx_internal_dequeue_event(struct vgfx_window* win, vgfx_event_t* out_event) {
    if (!win || !out_event) return 0;

    /* Queue empty? */
    if (win->event_head == win->event_tail) {
        return 0;
    }

    /* Dequeue event */
    *out_event = win->event_queue[win->event_tail];
    win->event_tail = (win->event_tail + 1) % VGFX_INTERNAL_EVENT_QUEUE_SLOTS;
    return 1;
}

/// @brief Peek at the next event without removing it from the queue.
/// @details Returns the oldest event without advancing event_tail.  Useful for
///          checking if a specific event type is pending without consuming it.
///
/// @param win       Pointer to the window structure
/// @param out_event Pointer to event storage (filled on success)
/// @return 1 if an event is available, 0 if queue is empty
///
/// @pre  win != NULL
/// @pre  out_event != NULL
/// @post If 1 returned: out_event contains oldest event, queue unchanged
/// @post If 0 returned: out_event unchanged, queue is empty
int vgfx_internal_peek_event(struct vgfx_window* win, vgfx_event_t* out_event) {
    if (!win || !out_event) return 0;

    /* Queue empty? */
    if (win->event_head == win->event_tail) {
        return 0;
    }

    /* Peek event (don't modify tail) */
    *out_event = win->event_queue[win->event_tail];
    return 1;
}

//===----------------------------------------------------------------------===//
// Version Functions
//===----------------------------------------------------------------------===//

/// @brief Get the library version as a packed 32-bit integer.
/// @details Format: (major << 16) | (minor << 8) | patch
///          Example: Version 1.2.3 returns 0x00010203
///
/// @return Packed version number
uint32_t vgfx_version(void) {
    return (VGFX_VERSION_MAJOR << 16) | (VGFX_VERSION_MINOR << 8) | VGFX_VERSION_PATCH;
}

/// @brief Get the library version as a human-readable string.
/// @details Returns a static string in "major.minor.patch" format.
///
/// @return Pointer to static version string (e.g., "1.0.0")
const char* vgfx_version_string(void) {
    return "1.0.0";
}

//===----------------------------------------------------------------------===//
// Error Handling
//===----------------------------------------------------------------------===//

/// @brief Get the last error message (thread-local).
/// @details Returns the error message set by the most recent error in this
///          thread.  The returned pointer is valid until the next error or
///          until vgfx_clear_last_error() is called.
///
/// @return Error message string, or NULL if no error has occurred
const char* vgfx_get_last_error(void) {
    return g_last_error_str;
}

/// @brief Clear the thread-local error state.
/// @details Resets error code to VGFX_OK and error message to NULL.
///
/// @post vgfx_get_last_error() returns NULL
/// @post vgfx_last_error_code() returns VGFX_OK
void vgfx_clear_error(void) {
    g_last_error_str = NULL;
    g_last_error_code = VGFX_ERR_NONE;
}

/// @brief Get the last error code (thread-local).
/// @details Returns the error code set by the most recent error in this thread.
///
/// @return Error code (VGFX_OK if no error)
vgfx_error_t vgfx_last_error_code(void) {
    return g_last_error_code;
}

/// @brief Set a user-provided logging callback for error messages.
/// @details The callback is invoked whenever an error occurs (in addition to
///          stderr printing).  Useful for integrating ViperGFX errors with
///          application logging systems.
///
/// @param fn Logging callback function (or NULL to disable)
///
/// @post Future errors will invoke fn(message) if fn != NULL
void vgfx_set_log_callback(vgfx_log_fn fn) {
    g_log_callback = fn;
}

//===----------------------------------------------------------------------===//
// Configuration Functions
//===----------------------------------------------------------------------===//

/// @brief Set the global default FPS for new windows.
/// @details Changes the default frame rate used when vgfx_window_params_t.fps
///          is 0.  Affects future calls to vgfx_create_window() but does not
///          modify existing windows.
///
/// @param fps Target FPS (clamped to [1, 1000]) or negative for unlimited
///
/// @post vgfx_get_default_fps() returns fps (clamped if positive)
void vgfx_set_default_fps(int32_t fps) {
    if (fps > 0) {
        g_default_fps = clamp_int(fps, 1, 1000);
    } else {
        g_default_fps = fps; /* Allow negative for unlimited */
    }
}

/// @brief Get the current global default FPS.
/// @details Returns the value set by vgfx_set_default_fps() or the initial
///          default (VGFX_DEFAULT_FPS = 60).
///
/// @return Global default FPS (positive, or negative for unlimited)
int32_t vgfx_get_default_fps(void) {
    return g_default_fps;
}

/// @brief Set the FPS limit for an existing window.
/// @details Changes the frame rate limit for vgfx_update().  Takes effect on
///          the next call to vgfx_update().
///
/// @param window Window handle
/// @param fps    Target FPS (0 = use default, positive = target, negative = unlimited)
///
/// @pre  window != NULL
/// @post window->fps is updated to fps (or g_default_fps if fps == 0)
void vgfx_set_fps(vgfx_window_t window, int32_t fps) {
    if (!window) return;

    if (fps == 0) {
        window->fps = g_default_fps;
    } else if (fps > 0) {
        window->fps = clamp_int(fps, 1, 1000);
    } else {
        window->fps = fps; /* Allow negative for unlimited */
    }
}

/// @brief Get the current FPS limit for a window.
/// @details Returns the window's frame rate limit (may differ from global default).
///
/// @param window Window handle
/// @return Window's FPS limit, or -1 if window is NULL
int32_t vgfx_get_fps(vgfx_window_t window) {
    if (!window) return -1;
    return window->fps;
}

//===----------------------------------------------------------------------===//
// Window Management
//===----------------------------------------------------------------------===//

/// @brief Create a window parameter structure with default values.
/// @details Fills in defaults from VGFX_DEFAULT_* macros:
///            - width:     VGFX_DEFAULT_WIDTH (640)
///            - height:    VGFX_DEFAULT_HEIGHT (480)
///            - title:     VGFX_DEFAULT_TITLE ("ViperGFX")
///            - fps:       VGFX_DEFAULT_FPS (60)
///            - resizable: 0 (false)
///
/// @return Window parameters initialized with defaults
vgfx_window_params_t vgfx_window_params_default(void) {
    vgfx_window_params_t params;
    params.width = VGFX_DEFAULT_WIDTH;
    params.height = VGFX_DEFAULT_HEIGHT;
    params.title = VGFX_DEFAULT_TITLE;
    params.fps = VGFX_DEFAULT_FPS;
    params.resizable = 0;
    return params;
}

/// @brief Create a new window with the specified parameters.
/// @details Allocates a window structure, framebuffer, and platform resources.
///          The window is immediately visible and ready for rendering.  Returns
///          NULL on failure (sets thread-local error state).
///
///          Parameter defaults are applied for invalid/missing values:
///            - width <= 0:  Use VGFX_DEFAULT_WIDTH
///            - height <= 0: Use VGFX_DEFAULT_HEIGHT
///            - title NULL:  Use VGFX_DEFAULT_TITLE
///            - fps == 0:    Use global default FPS
///
/// @param params Window creation parameters (or NULL for all defaults)
/// @return Window handle on success, NULL on failure (check vgfx_get_last_error())
///
/// @post On success: Window is visible, framebuffer cleared to black with alpha=0xFF
/// @post On failure: Error state set, no resources leaked
vgfx_window_t vgfx_create_window(const vgfx_window_params_t* params) {
    /* Apply defaults if params is NULL or fields are invalid */
    vgfx_window_params_t actual_params;

    if (params) {
        actual_params = *params;
    } else {
        actual_params = vgfx_window_params_default();
    }

    /* Apply defaults for invalid fields */
    if (actual_params.width <= 0) {
        actual_params.width = VGFX_DEFAULT_WIDTH;
    }
    if (actual_params.height <= 0) {
        actual_params.height = VGFX_DEFAULT_HEIGHT;
    }
    if (!actual_params.title) {
        actual_params.title = VGFX_DEFAULT_TITLE;
    }

    /* Validate dimensions against safety limits */
    if (actual_params.width > VGFX_MAX_WIDTH || actual_params.height > VGFX_MAX_HEIGHT) {
        vgfx_internal_set_error(VGFX_ERR_INVALID_PARAM,
            "Window dimensions exceed maximum (4096x4096)");
        return NULL;
    }

    /* Allocate window structure */
    struct vgfx_window* win = (struct vgfx_window*)calloc(1, sizeof(struct vgfx_window));
    if (!win) {
        vgfx_internal_set_error(VGFX_ERR_ALLOC, "Failed to allocate window structure");
        return NULL;
    }

    /* Initialize window properties */
    win->width = actual_params.width;
    win->height = actual_params.height;
    win->resizable = actual_params.resizable;
    win->stride = win->width * 4;

    /* Set FPS (apply default if params.fps == 0, clamp if positive) */
    if (actual_params.fps == 0) {
        win->fps = g_default_fps;
    } else if (actual_params.fps > 0) {
        win->fps = clamp_int(actual_params.fps, 1, 1000);
    } else {
        win->fps = actual_params.fps; /* Negative = unlimited */
    }

    /* Allocate framebuffer (aligned for cache performance) */
    size_t fb_size = (size_t)win->width * (size_t)win->height * 4;
    win->pixels = (uint8_t*)aligned_alloc_wrapper(VGFX_FRAMEBUFFER_ALIGNMENT, fb_size);
    if (!win->pixels) {
        free(win);
        vgfx_internal_set_error(VGFX_ERR_ALLOC, "Failed to allocate framebuffer");
        return NULL;
    }

    /* Clear framebuffer to black (RGB = 0, 0, 0) */
    memset(win->pixels, 0, fb_size);

    /* Set all alpha channels to 0xFF (fully opaque) */
    for (size_t i = 3; i < fb_size; i += 4) {
        win->pixels[i] = 0xFF;
    }

    /* Initialize event queue (empty ring buffer) */
    win->event_head = 0;
    win->event_tail = 0;
    win->event_overflow = 0;

    /* Initialize input state (all keys/buttons released) */
    memset(win->key_state, 0, sizeof(win->key_state));
    memset(win->mouse_button_state, 0, sizeof(win->mouse_button_state));
    win->mouse_x = 0;
    win->mouse_y = 0;

    /* Initialize timing (start frame deadline at current time) */
    win->last_frame_time_ms = 0;
    win->next_frame_deadline = vgfx_platform_now_ms();

    /* Initialize platform-specific resources (native window, etc.) */
    if (!vgfx_platform_init_window(win, &actual_params)) {
        aligned_free_wrapper(win->pixels);
        free(win);
        /* Error already set by platform backend */
        return NULL;
    }

    return win;
}

/// @brief Destroy a window and free all associated resources.
/// @details Closes the native window, frees the framebuffer, and deallocates
///          the window structure.  Safe to call with NULL (no-op).
///
/// @param window Window handle (may be NULL)
///
/// @post If window != NULL: Native window closed, all resources freed, handle invalid
void vgfx_destroy_window(vgfx_window_t window) {
    if (!window) return;

    /* Destroy platform resources (native window, platform_data) */
    vgfx_platform_destroy_window(window);

    /* Free framebuffer */
    if (window->pixels) {
        aligned_free_wrapper(window->pixels);
    }

    /* Free window structure */
    free(window);
}

/// @brief Process events, present framebuffer, and perform frame limiting.
/// @details Single call that performs a complete frame update:
///            1. Process OS events (keyboard, mouse, window events)
///            2. Present (blit) the framebuffer to the screen
///            3. Sleep if necessary to maintain target FPS
///            4. Update frame timing statistics
///
///          If fps > 0, sleeps until the next frame deadline to maintain the
///          target frame rate.  If fps < 0, returns immediately (unlimited FPS).
///
/// @param window Window handle
/// @return 1 on success, 0 on failure (e.g., event processing error)
///
/// @pre  window != NULL
/// @post OS events processed and queued
/// @post Framebuffer contents visible on screen
/// @post If fps > 0: slept until frame deadline
/// @post window->last_frame_time_ms updated with frame duration
int32_t vgfx_update(vgfx_window_t window) {
    if (!window) return 0;

    int64_t frame_start = vgfx_platform_now_ms();

    /* Process OS events (keyboard, mouse, window) */
    if (!vgfx_platform_process_events(window)) {
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Event processing error");
        return 0;
    }

    /* Present framebuffer to native window */
    if (!vgfx_platform_present(window)) {
        vgfx_internal_set_error(VGFX_ERR_PLATFORM, "Failed to present framebuffer");
        return 0;
    }

    /* FPS limiting (only if fps > 0) */
    if (window->fps > 0) {
        int64_t now = vgfx_platform_now_ms();
        int32_t target_frame_time = 1000 / window->fps;

        /* Sleep if we're ahead of schedule */
        if (now < window->next_frame_deadline) {
            int32_t sleep_time = (int32_t)(window->next_frame_deadline - now);
            vgfx_platform_sleep_ms(sleep_time);
            now = vgfx_platform_now_ms();
        }

        /* Update deadline for next frame (additive to avoid drift) */
        window->next_frame_deadline += target_frame_time;

        /* Resync if we fell behind by more than one frame (prevents runaway) */
        if (window->next_frame_deadline < now - target_frame_time) {
            window->next_frame_deadline = now;
        }
    }

    /* Calculate frame time (for diagnostics) */
    int64_t frame_end = vgfx_platform_now_ms();
    window->last_frame_time_ms = frame_end - frame_start;

    return 1;
}

/// @brief Get the duration of the last frame in milliseconds.
/// @details Returns the time elapsed during the most recent vgfx_update() call,
///          including event processing, rendering, and sleep time.
///
/// @param window Window handle
/// @return Last frame duration in milliseconds, or -1 if window is NULL
int32_t vgfx_frame_time_ms(vgfx_window_t window) {
    if (!window) return -1;
    return (int32_t)window->last_frame_time_ms;
}

/// @brief Get the window's dimensions.
/// @details Retrieves the width and height of the window's framebuffer.
///
/// @param window Window handle
/// @param width  Pointer to store width (may be NULL)
/// @param height Pointer to store height (may be NULL)
/// @return 1 on success, 0 if window is NULL
///
/// @post If width != NULL: *width contains window width
/// @post If height != NULL: *height contains window height
int32_t vgfx_get_size(vgfx_window_t window, int32_t* width, int32_t* height) {
    if (!window) return 0;

    if (width) *width = window->width;
    if (height) *height = window->height;
    return 1;
}

//===----------------------------------------------------------------------===//
// Event Handling
//===----------------------------------------------------------------------===//

/// @brief Poll the next event from the window's event queue.
/// @details Dequeues and returns the oldest event.  If the queue is empty,
///          returns 0 without modifying out_event.  Events are generated by
///          vgfx_update() calling vgfx_platform_process_events().
///
/// @param window    Window handle
/// @param out_event Pointer to event storage (filled on success)
/// @return 1 if an event was retrieved, 0 if queue is empty or window is NULL
///
/// @pre  window != NULL
/// @pre  out_event != NULL
/// @post If 1 returned: out_event contains oldest event, event removed from queue
/// @post If 0 returned: out_event unchanged, queue is empty
int32_t vgfx_poll_event(vgfx_window_t window, vgfx_event_t* out_event) {
    if (!window || !out_event) return 0;
    return vgfx_internal_dequeue_event(window, out_event);
}

/// @brief Peek at the next event without removing it from the queue.
/// @details Returns the oldest event without dequeuing it.  Useful for checking
///          if a specific event type is pending.
///
/// @param window    Window handle
/// @param out_event Pointer to event storage (filled on success)
/// @return 1 if an event is available, 0 if queue is empty or window is NULL
///
/// @pre  window != NULL
/// @pre  out_event != NULL
/// @post If 1 returned: out_event contains oldest event, queue unchanged
/// @post If 0 returned: out_event unchanged, queue is empty
int32_t vgfx_peek_event(vgfx_window_t window, vgfx_event_t* out_event) {
    if (!window || !out_event) return 0;
    return vgfx_internal_peek_event(window, out_event);
}

/// @brief Discard all events from the window's event queue.
/// @details Dequeues and discards all pending events.  Useful for ignoring
///          accumulated events (e.g., after a pause or dialog).
///
/// @param window Window handle
/// @return Number of events discarded, or 0 if window is NULL
///
/// @post Event queue is empty
int32_t vgfx_flush_events(vgfx_window_t window) {
    if (!window) return 0;

    int32_t count = 0;
    vgfx_event_t dummy;
    while (vgfx_internal_dequeue_event(window, &dummy)) {
        count++;
    }
    return count;
}

/// @brief Get and reset the event overflow counter.
/// @details Returns the number of events dropped due to queue overflow since
///          the last call to this function.  The counter is reset to zero
///          after reading.  Non-zero values indicate the queue was too small
///          or vgfx_poll_event() was not called frequently enough.
///
/// @param window Window handle
/// @return Number of events dropped since last query, or 0 if window is NULL
///
/// @post window->event_overflow == 0
int32_t vgfx_event_overflow_count(vgfx_window_t window) {
    if (!window) return 0;

    int32_t count = window->event_overflow;
    window->event_overflow = 0; /* Reset after reading */
    return count;
}

//===----------------------------------------------------------------------===//
// Drawing Operations
//===----------------------------------------------------------------------===//

/// @brief Set a single pixel to the specified color.
/// @details Directly writes to the framebuffer at (x, y).  Alpha is always
///          set to 0xFF (fully opaque).  Silent no-op if coordinates are out
///          of bounds or window is NULL.
///
/// @param window Window handle
/// @param x      X coordinate in pixels
/// @param y      Y coordinate in pixels
/// @param color  RGB color (format: 0x00RRGGBB)
///
/// @post If (x, y) in bounds: pixel at (x, y) is set to color with alpha=0xFF
void vgfx_pset(vgfx_window_t window, int32_t x, int32_t y, vgfx_color_t color) {
    /* Silent no-op if out of bounds or NULL window */
    if (!vgfx_internal_in_bounds(window, x, y)) return;

    int32_t offset = y * window->stride + x * 4;
    window->pixels[offset + 0] = (color >> 16) & 0xFF; /* R */
    window->pixels[offset + 1] = (color >> 8) & 0xFF;  /* G */
    window->pixels[offset + 2] = (color >> 0) & 0xFF;  /* B */
    window->pixels[offset + 3] = 0xFF;                 /* A (fully opaque) */
}

/// @brief Get the color of a single pixel.
/// @details Reads the RGB color from the framebuffer at (x, y).  Alpha channel
///          is ignored (always fully opaque in ViperGFX v1).
///
/// @param window    Window handle
/// @param x         X coordinate in pixels
/// @param y         Y coordinate in pixels
/// @param out_color Pointer to store color (format: 0x00RRGGBB)
/// @return 1 on success, 0 if out of bounds, window is NULL, or out_color is NULL
///
/// @post If 1 returned: *out_color contains RGB color at (x, y)
int32_t vgfx_point(vgfx_window_t window, int32_t x, int32_t y, vgfx_color_t* out_color) {
    if (!vgfx_internal_in_bounds(window, x, y) || !out_color) return 0;

    int32_t offset = y * window->stride + x * 4;
    uint8_t r = window->pixels[offset + 0];
    uint8_t g = window->pixels[offset + 1];
    uint8_t b = window->pixels[offset + 2];

    *out_color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    return 1;
}

/// @brief Clear the entire framebuffer to a solid color.
/// @details Sets all pixels to the specified color with alpha=0xFF.  Fast
///          operation that writes directly to the framebuffer.
///
/// @param window Window handle
/// @param color  RGB color (format: 0x00RRGGBB)
///
/// @post All pixels are set to color with alpha=0xFF
void vgfx_cls(vgfx_window_t window, vgfx_color_t color) {
    if (!window) return;

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 0) & 0xFF;

    size_t pixel_count = (size_t)window->width * (size_t)window->height;
    for (size_t i = 0; i < pixel_count; i++) {
        window->pixels[i * 4 + 0] = r;
        window->pixels[i * 4 + 1] = g;
        window->pixels[i * 4 + 2] = b;
        window->pixels[i * 4 + 3] = 0xFF;
    }
}

//===----------------------------------------------------------------------===//
// Drawing Primitives (Forwarding to vgfx_draw.c)
//===----------------------------------------------------------------------===//
// These functions are implemented in vgfx_draw.c using Bresenham and midpoint
// algorithms.  The public API functions forward to the internal implementations.
//===----------------------------------------------------------------------===//

/* Forward declarations for drawing primitives (implemented in vgfx_draw.c) */
void vgfx_draw_line(vgfx_window_t window, int32_t x1, int32_t y1,
                    int32_t x2, int32_t y2, vgfx_color_t color);
void vgfx_draw_rect(vgfx_window_t window, int32_t x, int32_t y,
                    int32_t w, int32_t h, vgfx_color_t color);
void vgfx_draw_fill_rect(vgfx_window_t window, int32_t x, int32_t y,
                         int32_t w, int32_t h, vgfx_color_t color);
void vgfx_draw_circle(vgfx_window_t window, int32_t cx, int32_t cy,
                      int32_t radius, vgfx_color_t color);
void vgfx_draw_fill_circle(vgfx_window_t window, int32_t cx, int32_t cy,
                           int32_t radius, vgfx_color_t color);

/// @brief Draw a line from (x1, y1) to (x2, y2).
/// @details Uses Bresenham's line algorithm (implemented in vgfx_draw.c).
///
/// @param window Window handle
/// @param x1     Start X coordinate
/// @param y1     Start Y coordinate
/// @param x2     End X coordinate
/// @param y2     End Y coordinate
/// @param color  RGB color (format: 0x00RRGGBB)
void vgfx_line(vgfx_window_t window, int32_t x1, int32_t y1,
               int32_t x2, int32_t y2, vgfx_color_t color) {
    vgfx_draw_line(window, x1, y1, x2, y2, color);
}

/// @brief Draw an unfilled rectangle.
/// @details Draws the outline of a rectangle with top-left corner at (x, y)
///          and dimensions w × h.  Implemented in vgfx_draw.c.
///
/// @param window Window handle
/// @param x      Top-left X coordinate
/// @param y      Top-left Y coordinate
/// @param w      Width in pixels
/// @param h      Height in pixels
/// @param color  RGB color (format: 0x00RRGGBB)
void vgfx_rect(vgfx_window_t window, int32_t x, int32_t y,
               int32_t w, int32_t h, vgfx_color_t color) {
    vgfx_draw_rect(window, x, y, w, h, color);
}

/// @brief Draw a filled rectangle.
/// @details Fills a rectangle with top-left corner at (x, y) and dimensions
///          w × h.  Implemented in vgfx_draw.c.
///
/// @param window Window handle
/// @param x      Top-left X coordinate
/// @param y      Top-left Y coordinate
/// @param w      Width in pixels
/// @param h      Height in pixels
/// @param color  RGB color (format: 0x00RRGGBB)
void vgfx_fill_rect(vgfx_window_t window, int32_t x, int32_t y,
                    int32_t w, int32_t h, vgfx_color_t color) {
    vgfx_draw_fill_rect(window, x, y, w, h, color);
}

/// @brief Draw an unfilled circle.
/// @details Draws the outline of a circle centered at (cx, cy) with the
///          specified radius.  Uses midpoint circle algorithm (vgfx_draw.c).
///
/// @param window Window handle
/// @param cx     Center X coordinate
/// @param cy     Center Y coordinate
/// @param radius Radius in pixels
/// @param color  RGB color (format: 0x00RRGGBB)
void vgfx_circle(vgfx_window_t window, int32_t cx, int32_t cy,
                 int32_t radius, vgfx_color_t color) {
    vgfx_draw_circle(window, cx, cy, radius, color);
}

/// @brief Draw a filled circle.
/// @details Fills a circle centered at (cx, cy) with the specified radius.
///          Uses scanline filling algorithm (vgfx_draw.c).
///
/// @param window Window handle
/// @param cx     Center X coordinate
/// @param cy     Center Y coordinate
/// @param radius Radius in pixels
/// @param color  RGB color (format: 0x00RRGGBB)
void vgfx_fill_circle(vgfx_window_t window, int32_t cx, int32_t cy,
                      int32_t radius, vgfx_color_t color) {
    vgfx_draw_fill_circle(window, cx, cy, radius, color);
}

//===----------------------------------------------------------------------===//
// Color Utilities
//===----------------------------------------------------------------------===//

/// @brief Extract RGB components from a packed color value.
/// @details Splits a vgfx_color_t (0x00RRGGBB) into separate R, G, B bytes.
///
/// @param color Packed RGB color (format: 0x00RRGGBB)
/// @param r     Pointer to store red component (may be NULL)
/// @param g     Pointer to store green component (may be NULL)
/// @param b     Pointer to store blue component (may be NULL)
///
/// @post If r != NULL: *r contains red component [0, 255]
/// @post If g != NULL: *g contains green component [0, 255]
/// @post If b != NULL: *b contains blue component [0, 255]
void vgfx_color_to_rgb(vgfx_color_t color, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (r) *r = (color >> 16) & 0xFF;
    if (g) *g = (color >> 8) & 0xFF;
    if (b) *b = (color >> 0) & 0xFF;
}

//===----------------------------------------------------------------------===//
// Input Polling
//===----------------------------------------------------------------------===//

/// @brief Check if a key is currently pressed.
/// @details Returns the current state of the specified key.  Updated by
///          vgfx_update() -> vgfx_platform_process_events().
///
/// @param window Window handle
/// @param key    Key code (must be < 512 and != VGFX_KEY_UNKNOWN)
/// @return 1 if key is pressed, 0 if released, window is NULL, or key is invalid
///
/// @pre  key < 512
int32_t vgfx_key_down(vgfx_window_t window, vgfx_key_t key) {
    if (!window || key == VGFX_KEY_UNKNOWN || key >= 512) return 0;
    return window->key_state[key] != 0;
}

/// @brief Get the current mouse cursor position.
/// @details Returns the mouse position in window-relative coordinates.  The
///          position may be outside [0, width) × [0, height) if the cursor
///          is outside the window.
///
/// @param window Window handle
/// @param x      Pointer to store X coordinate (may be NULL)
/// @param y      Pointer to store Y coordinate (may be NULL)
/// @return 1 if cursor is inside the window bounds, 0 otherwise or if window is NULL
///
/// @post If x != NULL: *x contains cursor X coordinate
/// @post If y != NULL: *y contains cursor Y coordinate
int32_t vgfx_mouse_pos(vgfx_window_t window, int32_t* x, int32_t* y) {
    if (!window) return 0;

    /* Always fill output pointers */
    if (x) *x = window->mouse_x;
    if (y) *y = window->mouse_y;

    /* Return 1 if inside bounds, 0 otherwise */
    return (window->mouse_x >= 0 && window->mouse_x < window->width &&
            window->mouse_y >= 0 && window->mouse_y < window->height);
}

/// @brief Check if a mouse button is currently pressed.
/// @details Returns the current state of the specified mouse button.  Updated
///          by vgfx_update() -> vgfx_platform_process_events().
///
/// @param window Window handle
/// @param button Mouse button code (must be < 8)
/// @return 1 if button is pressed, 0 if released, window is NULL, or button is invalid
///
/// @pre  button < 8
int32_t vgfx_mouse_button(vgfx_window_t window, vgfx_mouse_button_t button) {
    if (!window || button >= 8) return 0;
    return window->mouse_button_state[button] != 0;
}

//===----------------------------------------------------------------------===//
// Framebuffer Access
//===----------------------------------------------------------------------===//

/// @brief Get direct access to the window's framebuffer.
/// @details Returns a structure with pointers and dimensions for direct pixel
///          manipulation.  The framebuffer is in RGBA 8-8-8-8 format with
///          4 bytes per pixel (row-major, top-down).
///
/// @param window   Window handle
/// @param out_info Pointer to framebuffer info structure (filled on success)
/// @return 1 on success, 0 if window or out_info is NULL
///
/// @pre  window != NULL
/// @pre  out_info != NULL
/// @post If 1 returned: out_info filled with framebuffer details
///
/// @warning Direct framebuffer access bypasses bounds checking.  Incorrect
///          writes may corrupt memory.  Prefer vgfx_pset() for safety.
int32_t vgfx_get_framebuffer(vgfx_window_t window, vgfx_framebuffer_t* out_info) {
    if (!window || !out_info) return 0;

    out_info->pixels = window->pixels;
    out_info->width = window->width;
    out_info->height = window->height;
    out_info->stride = window->stride;
    return 1;
}

//===----------------------------------------------------------------------===//
// End of Core Implementation
//===----------------------------------------------------------------------===//
