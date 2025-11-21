//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperGFX Internal Structures and Platform Abstraction
//
// Defines the internal window representation, platform backend interface,
// and internal helper functions.  This header is NOT part of the public API
// and is only included by ViperGFX implementation files.
//
// Platform Backend Contract:
//   Each platform backend (vgfx_platform_*.c) must implement the platform
//   abstraction functions defined below.  The backend is responsible for:
//     - Creating/destroying native OS windows
//     - Processing OS events and translating them to vgfx_event_t
//     - Presenting (blitting) the framebuffer to the screen
//     - Providing high-resolution timing and sleep functions
//
// Internal Window Structure:
//   The vgfx_window structure is the complete representation of a window,
//   containing the framebuffer, event queue, input state, timing info, and
//   platform-specific data.  The public API only exposes an opaque handle.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Internal structures and platform abstraction layer for ViperGFX.
/// @details Not part of the public API.  Defines the complete window structure,
///          platform backend interface, and internal helper functions.

#pragma once

#include "vgfx.h"
#include "vgfx_config.h"
#include <stdint.h>
#include <stddef.h>

//===----------------------------------------------------------------------===//
// Internal Constants
//===----------------------------------------------------------------------===//

/// @def VGFX_INTERNAL_EVENT_QUEUE_SLOTS
/// @brief Physical array size for the lock-free ring buffer.
/// @details One extra slot is allocated beyond the advertised capacity to
///          distinguish between full and empty states without using separate
///          counters.  When (head + 1) % SLOTS == tail, the queue is full.
///          When head == tail, the queue is empty.
#define VGFX_INTERNAL_EVENT_QUEUE_SLOTS (VGFX_EVENT_QUEUE_SIZE + 1)

//===----------------------------------------------------------------------===//
// Internal Window Structure
//===----------------------------------------------------------------------===//

/// @brief Complete internal representation of a ViperGFX window.
/// @details Contains all state required to manage a window: framebuffer,
///          event queue, input tracking, timing, and platform-specific data.
///          The public API exposes this as an opaque vgfx_window_t handle.
///
/// @invariant width > 0 && height > 0
/// @invariant pixels != NULL (4-byte RGBA framebuffer)
/// @invariant stride == width * 4
/// @invariant 0 <= event_head < VGFX_INTERNAL_EVENT_QUEUE_SLOTS
/// @invariant 0 <= event_tail < VGFX_INTERNAL_EVENT_QUEUE_SLOTS
/// @invariant event_overflow >= 0
/// @invariant mouse_x, mouse_y reflect last known cursor position
/// @invariant key_state[k] is 1 if key k is pressed, 0 if released
/// @invariant platform_data is allocated/owned by the platform backend
struct vgfx_window {
    //===------------------------------------------------------------------===//
    // Window Properties
    //===------------------------------------------------------------------===//

    /// @brief Window width in pixels (immutable after creation).
    int32_t  width;

    /// @brief Window height in pixels (immutable after creation).
    int32_t  height;

    /// @brief Target frame rate for this window.
    /// @details fps > 0: Target that specific FPS with frame limiting.
    ///          fps < 0: Unlimited (no frame rate limiting).
    ///          fps == 0: Should not occur after vgfx_create_window().
    int32_t  fps;

    /// @brief Whether the window is resizable (1 = yes, 0 = no).
    /// @details Currently for metadata only; resizing is not fully supported
    ///          in v1 (would require framebuffer reallocation and event queue
    ///          handling).
    int32_t  resizable;

    //===------------------------------------------------------------------===//
    // Framebuffer
    //===------------------------------------------------------------------===//

    /// @brief Pointer to RGBA pixel data (width × height × 4 bytes).
    /// @details Memory is aligned to VGFX_FRAMEBUFFER_ALIGNMENT and owned by
    ///          this structure.  Each pixel is 4 consecutive bytes: R, G, B, A.
    ///          Pixel at (x, y) is at pixels[y * stride + x * 4].
    uint8_t* pixels;

    /// @brief Row stride in bytes (always width * 4 for contiguous rows).
    int32_t  stride;

    //===------------------------------------------------------------------===//
    // Event Queue (Lock-Free SPSC Ring Buffer)
    //===------------------------------------------------------------------===//

    /// @brief Ring buffer storage for events.
    /// @details Array of VGFX_INTERNAL_EVENT_QUEUE_SLOTS elements.  The extra
    ///          slot enables full/empty distinction without a separate counter.
    vgfx_event_t event_queue[VGFX_INTERNAL_EVENT_QUEUE_SLOTS];

    /// @brief Next write position (producer index).
    /// @details Modified only by the platform thread in vgfx_internal_enqueue_event().
    ///          When (head + 1) % SLOTS == tail, the queue is full.
    int32_t      event_head;

    /// @brief Next read position (consumer index).
    /// @details Modified only by the application thread in vgfx_poll_event().
    ///          When head == tail, the queue is empty.
    int32_t      event_tail;

    /// @brief Count of events dropped since last vgfx_get_overflow() call.
    /// @details Incremented by the platform thread when the queue is full and
    ///          a non-CLOSE event would have been enqueued.  Reset to zero by
    ///          vgfx_get_overflow().
    int32_t      event_overflow;

    //===------------------------------------------------------------------===//
    // Input State
    //===------------------------------------------------------------------===//

    /// @brief Per-key state array (1 = pressed, 0 = released).
    /// @details Indexed by vgfx_key_t values (must be < 512).  Updated by
    ///          platform backend when processing keyboard events.
    uint8_t key_state[512];

    /// @brief Current mouse X coordinate in window-relative pixels.
    /// @details Updated by platform backend on mouse move events.  May be
    ///          negative or >= width if cursor is outside the window.
    int32_t mouse_x;

    /// @brief Current mouse Y coordinate in window-relative pixels.
    /// @details Updated by platform backend on mouse move events.  May be
    ///          negative or >= height if cursor is outside the window.
    int32_t mouse_y;

    /// @brief Per-button state array (1 = pressed, 0 = released).
    /// @details Indexed by vgfx_mouse_button_t values.  Updated by platform
    ///          backend when processing mouse button events.
    uint8_t mouse_button_state[8];

    //===------------------------------------------------------------------===//
    // Timing
    //===------------------------------------------------------------------===//

    /// @brief Duration of last frame in milliseconds.
    /// @details Updated by vgfx_present() after each frame completes.  Used
    ///          for performance diagnostics and can be queried via
    ///          vgfx_get_last_frame_time().
    int64_t last_frame_time_ms;

    /// @brief Absolute timestamp for when the next frame should start.
    /// @details Used for frame rate limiting.  If fps > 0, vgfx_present()
    ///          sleeps until this deadline before returning.  Computed as:
    ///          next_frame_deadline = last_start_time + (1000 / fps).
    int64_t next_frame_deadline;

    //===------------------------------------------------------------------===//
    // Platform-Specific Data
    //===------------------------------------------------------------------===//

    /// @brief Opaque pointer to platform-specific window data.
    /// @details Allocated and owned by the platform backend.  On macOS, this
    ///          points to a structure containing NSWindow, NSView, etc.  On
    ///          Linux, it would contain X11 Display/Window handles.  Must be
    ///          freed by vgfx_platform_destroy_window().
    void* platform_data;
};

//===----------------------------------------------------------------------===//
// Platform Backend Interface
//===----------------------------------------------------------------------===//
// Each platform backend (vgfx_platform_macos.m, vgfx_platform_linux.c, etc.)
// must provide implementations for the following functions.  The core library
// (vgfx.c) calls these to delegate OS-specific operations.
//===----------------------------------------------------------------------===//

/// @brief Initialize platform-specific window resources.
/// @details Allocates win->platform_data and creates the native OS window.
///          The window should be visible and ready for rendering when this
///          function returns.  On failure, the backend must clean up any
///          partially allocated resources and set a descriptive error via
///          vgfx_internal_set_error().
///
/// @param win    Pointer to the window structure (framebuffer already allocated)
/// @param params Window creation parameters (title, dimensions, flags)
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  params != NULL
/// @pre  win->pixels != NULL (framebuffer already allocated by core)
/// @post On success: win->platform_data != NULL, native window visible
/// @post On failure: win->platform_data == NULL, error set
int vgfx_platform_init_window(struct vgfx_window* win,
                               const vgfx_window_params_t* params);

/// @brief Destroy platform-specific window resources.
/// @details Closes the native OS window and frees win->platform_data.  Must
///          be safe to call even if vgfx_platform_init_window() failed (in
///          which case platform_data may be NULL or partially initialized).
///
/// @param win Pointer to the window structure
///
/// @pre  win != NULL
/// @post win->platform_data == NULL, native window destroyed
void vgfx_platform_destroy_window(struct vgfx_window* win);

/// @brief Process pending OS events and update window state.
/// @details Polls the OS event queue, translates native events into
///          vgfx_event_t, and enqueues them via vgfx_internal_enqueue_event().
///          Also updates win->key_state, win->mouse_x, win->mouse_y, and
///          win->mouse_button_state to reflect the current input state.
///
///          Special handling for CLOSE events: If the user closes the window
///          (clicks the close button), the backend must enqueue a CLOSE event
///          and may return 0 to signal that the window is no longer valid.
///
/// @param win Pointer to the window structure
/// @return 1 on success, 0 on fatal error (e.g., window destroyed)
///
/// @pre  win != NULL
/// @pre  win->platform_data != NULL
/// @post win->key_state, mouse_*, and event queue are updated
int vgfx_platform_process_events(struct vgfx_window* win);

/// @brief Present (blit) the framebuffer to the native window.
/// @details Transfers the contents of win->pixels to the OS window surface
///          so they become visible on screen.  This may involve creating a
///          platform-specific bitmap, copying pixel data, and invalidating
///          the window's view to trigger a redraw.
///
/// @param win Pointer to the window structure
/// @return 1 on success, 0 on failure
///
/// @pre  win != NULL
/// @pre  win->pixels != NULL
/// @pre  win->platform_data != NULL
/// @post Framebuffer contents are visible in the native window
int vgfx_platform_present(struct vgfx_window* win);

/// @brief Sleep for the specified duration.
/// @details Used by vgfx_present() for frame rate limiting.  The actual sleep
///          duration may be slightly longer due to OS scheduler granularity.
///          If ms <= 0, this function returns immediately without sleeping.
///
/// @param ms Duration to sleep in milliseconds
void vgfx_platform_sleep_ms(int32_t ms);

/// @brief Get current high-resolution timestamp in milliseconds.
/// @details Returns a monotonic timestamp (never decreases) with millisecond
///          precision.  The epoch is arbitrary but consistent within a process.
///          Used for frame timing and FPS limiting.
///
/// @return Milliseconds since an arbitrary epoch (monotonic)
///
/// @post Return value >= previous calls within the same process
int64_t vgfx_platform_now_ms(void);

//===----------------------------------------------------------------------===//
// Internal Helper Functions
//===----------------------------------------------------------------------===//
// These functions are implemented in vgfx.c and used internally by the core
// library and platform backends.  They are NOT part of the public API.
//===----------------------------------------------------------------------===//

/// @brief Set the thread-local error code and message.
/// @details Stores the error information so it can be retrieved via
///          vgfx_get_last_error() and vgfx_get_last_error_message().  This
///          function is called by both the core library and platform backends
///          when an error occurs.
///
/// @param code Error code (enum vgfx_error_t)
/// @param msg  Descriptive error message (UTF-8, not NULL)
///
/// @pre  msg != NULL
/// @post vgfx_get_last_error() returns code, vgfx_get_last_error_message() returns msg
void vgfx_internal_set_error(vgfx_error_t code, const char* msg);

/// @brief Enqueue an event into the window's lock-free ring buffer.
/// @details Attempts to add the event to the queue.  If the queue is full:
///            - CLOSE events are always enqueued (overwriting oldest event)
///            - Other events are dropped and event_overflow is incremented
///
///          This function is safe to call from the platform thread (producer).
///
/// @param win   Pointer to the window structure
/// @param event Pointer to the event to enqueue (copied into the queue)
/// @return 1 if event was enqueued, 0 if queue was full (non-CLOSE event dropped)
///
/// @pre  win != NULL
/// @pre  event != NULL
/// @post Event is in the queue OR event_overflow incremented
int vgfx_internal_enqueue_event(struct vgfx_window* win, const vgfx_event_t* event);

/// @brief Dequeue the next event from the window's ring buffer.
/// @details Removes and returns the oldest event from the queue.  If the queue
///          is empty, returns 0 without modifying out_event.
///
///          This function is safe to call from the application thread (consumer).
///
/// @param win       Pointer to the window structure
/// @param out_event Pointer to event storage (filled on success)
/// @return 1 if an event was dequeued, 0 if queue is empty
///
/// @pre  win != NULL
/// @pre  out_event != NULL
/// @post If 1 returned: out_event contains the next event, event_tail advanced
/// @post If 0 returned: out_event unchanged, queue is empty
int vgfx_internal_dequeue_event(struct vgfx_window* win, vgfx_event_t* out_event);

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
/// @post If 1 returned: out_event contains the next event, queue unchanged
/// @post If 0 returned: out_event unchanged, queue is empty
int vgfx_internal_peek_event(struct vgfx_window* win, vgfx_event_t* out_event);

/// @brief Check if pixel coordinates are within the window's bounds.
/// @details Fast bounds check for drawing operations.  Returns true if the
///          pixel at (x, y) is inside the framebuffer [0, width) × [0, height).
///
/// @param win Pointer to the window structure (may be NULL, returns 0)
/// @param x   X coordinate in pixels
/// @param y   Y coordinate in pixels
/// @return 1 if (x, y) is in bounds, 0 otherwise
///
/// @post Return value is 1 iff win != NULL && 0 <= x < width && 0 <= y < height
static inline int vgfx_internal_in_bounds(struct vgfx_window* win, int32_t x, int32_t y) {
    return (win && x >= 0 && x < win->width && y >= 0 && y < win->height);
}

//===----------------------------------------------------------------------===//
// End of Internal Definitions
//===----------------------------------------------------------------------===//
