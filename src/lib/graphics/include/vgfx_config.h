//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperGFX Configuration Macros
//
// Defines compile-time configuration constants for the ViperGFX library,
// including default window parameters, resource limits, and memory alignment
// settings.  All macros are guarded with #ifndef so projects can override
// them via compiler flags or by defining them before including vgfx.h.
//
// Override examples:
//   - Compiler flags: -DVGFX_DEFAULT_WIDTH=800 -DVGFX_DEFAULT_HEIGHT=600
//   - Before include:  #define VGFX_DEFAULT_FPS 30
//                      #include "vgfx.h"
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Compile-time configuration macros for ViperGFX.
/// @details Provides defaults for window dimensions, frame rates, resource
///          limits, and memory alignment.  All settings can be overridden
///          at compile time via preprocessor definitions.

#pragma once

//===----------------------------------------------------------------------===//
// Default Window Parameters
//===----------------------------------------------------------------------===//

/// @def VGFX_DEFAULT_WIDTH
/// @brief Default window width in pixels when params.width <= 0.
/// @details Used by vgfx_create_window() if the width field in
///          vgfx_window_params_t is zero or negative.  Must be in range
///          [1, VGFX_MAX_WIDTH].
#ifndef VGFX_DEFAULT_WIDTH
#define VGFX_DEFAULT_WIDTH   640
#endif

/// @def VGFX_DEFAULT_HEIGHT
/// @brief Default window height in pixels when params.height <= 0.
/// @details Used by vgfx_create_window() if the height field in
///          vgfx_window_params_t is zero or negative.  Must be in range
///          [1, VGFX_MAX_HEIGHT].
#ifndef VGFX_DEFAULT_HEIGHT
#define VGFX_DEFAULT_HEIGHT  480
#endif

/// @def VGFX_DEFAULT_TITLE
/// @brief Default window title when params.title is NULL.
/// @details UTF-8 encoded string used as the window caption.  The platform
///          backend may truncate or modify the title based on OS conventions.
#ifndef VGFX_DEFAULT_TITLE
#define VGFX_DEFAULT_TITLE   "ViperGFX"
#endif

/// @def VGFX_DEFAULT_FPS
/// @brief Default frame rate limit when params.fps == 0.
/// @details Target frames per second for the window's event loop.  The actual
///          frame rate may be lower if rendering takes longer than 1/FPS.
///
///          Special values at runtime (in vgfx_window_params_t.fps):
///            fps == 0   → Use VGFX_DEFAULT_FPS (this macro)
///            fps < 0    → Unlimited (no frame rate limiting)
///            fps > 0    → Target that specific frame rate
///
///          This macro must be positive.
#ifndef VGFX_DEFAULT_FPS
#define VGFX_DEFAULT_FPS     60
#endif

//===----------------------------------------------------------------------===//
// Framebuffer Configuration
//===----------------------------------------------------------------------===//

/// @def VGFX_COLOR_DEPTH
/// @brief Color depth of the internal framebuffer in bits per pixel.
/// @details For ViperGFX v1, this MUST remain 32 (RGBA 8-8-8-8 format).
///          Each pixel is represented as a 32-bit vgfx_color_t value with
///          8 bits per channel (red, green, blue, alpha).
///
/// @warning Overriding this macro to any value other than 32 is UNSUPPORTED
///          and will lead to undefined behavior.  The entire API assumes
///          4 bytes per pixel.
#ifndef VGFX_COLOR_DEPTH
#define VGFX_COLOR_DEPTH     32  /* RGBA (8-8-8-8) - DO NOT CHANGE */
#endif

/// @def VGFX_FRAMEBUFFER_ALIGNMENT
/// @brief Memory alignment boundary for framebuffer allocations in bytes.
/// @details Ensures the framebuffer base address is aligned to this boundary,
///          which can improve cache performance and enable SIMD optimizations.
///          Must be a power of 2.  Minimum recommended value is 16.
///
///          Default: 64 bytes (optimal for modern CPUs with 64-byte cache lines)
///
/// @warning If set below 16, some platforms may incur performance penalties.
///          Values that are not powers of 2 may cause alignment issues.
#ifndef VGFX_FRAMEBUFFER_ALIGNMENT
#define VGFX_FRAMEBUFFER_ALIGNMENT 64
#endif

//===----------------------------------------------------------------------===//
// Resource Limits and Safety Constraints
//===----------------------------------------------------------------------===//

/// @def VGFX_MAX_WIDTH
/// @brief Maximum allowed window width in pixels.
/// @details Constrains memory allocation to prevent integer overflow when
///          computing framebuffer size (width * height * 4).  Attempts to
///          create windows larger than this will fail gracefully.
///
///          This limit ensures width * height * 4 fits in size_t on all
///          supported platforms.
#ifndef VGFX_MAX_WIDTH
#define VGFX_MAX_WIDTH       4096
#endif

/// @def VGFX_MAX_HEIGHT
/// @brief Maximum allowed window height in pixels.
/// @details Constrains memory allocation to prevent integer overflow when
///          computing framebuffer size (width * height * 4).  Attempts to
///          create windows larger than this will fail gracefully.
///
///          This limit ensures width * height * 4 fits in size_t on all
///          supported platforms.
#ifndef VGFX_MAX_HEIGHT
#define VGFX_MAX_HEIGHT      4096
#endif

/// @def VGFX_EVENT_QUEUE_SIZE
/// @brief Capacity of the lock-free event queue (number of events).
/// @details Determines how many unprocessed events can accumulate before
///          new events are dropped.  The event queue uses a lock-free SPSC
///          (single producer, single consumer) ring buffer design where:
///            - Producer: Platform backend thread (window events from OS)
///            - Consumer: Application thread (vgfx_poll_event() calls)
///
///          Power-of-2 sizes enable efficient modulo indexing via bitwise AND,
///          but any positive value is supported.  Larger queues reduce the
///          risk of event loss during processing spikes at the cost of memory.
///
/// @note Memory overhead: VGFX_EVENT_QUEUE_SIZE * sizeof(vgfx_event_t)
///       (typically ~64 bytes per event, so 256 events = ~16 KB)
#ifndef VGFX_EVENT_QUEUE_SIZE
#define VGFX_EVENT_QUEUE_SIZE 256
#endif

//===----------------------------------------------------------------------===//
// End of ViperGFX Configuration
//===----------------------------------------------------------------------===//
