//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_debugoverlay.h
// Purpose: Debug overlay for displaying FPS, delta time, and custom watched
//   variables during game development. Renders as a semi-transparent panel
//   in the top-right corner of the canvas.
//
// Key invariants:
//   - Maximum of RT_DEBUG_MAX_WATCHES (16) custom watch entries.
//   - FPS is computed as a rolling average over RT_DEBUG_FPS_HISTORY (16) frames.
//   - The overlay is disabled by default; call rt_debugoverlay_enable() to show.
//   - Draw must be called after all other rendering (renders on top).
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64; no manual free needed.
//
// Links: src/runtime/collections/rt_debugoverlay.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define RT_DEBUG_MAX_WATCHES 16
#define RT_DEBUG_FPS_HISTORY 16

    /// Opaque handle to a DebugOverlay instance.
    typedef struct rt_debugoverlay_impl *rt_debugoverlay;

    /// @brief Create a new DebugOverlay (disabled by default).
    rt_debugoverlay rt_debugoverlay_new(void);

    /// @brief Destroy a DebugOverlay.
    void rt_debugoverlay_destroy(rt_debugoverlay dbg);

    /// @brief Enable the overlay (visible when Draw is called).
    void rt_debugoverlay_enable(rt_debugoverlay dbg);

    /// @brief Disable the overlay (hidden).
    void rt_debugoverlay_disable(rt_debugoverlay dbg);

    /// @brief Toggle the overlay on/off.
    void rt_debugoverlay_toggle(rt_debugoverlay dbg);

    /// @brief Check if the overlay is enabled.
    int8_t rt_debugoverlay_is_enabled(rt_debugoverlay dbg);

    /// @brief Update the overlay with current frame delta time (ms).
    ///        Should be called once per frame to track FPS.
    void rt_debugoverlay_update(rt_debugoverlay dbg, int64_t dt_ms);

    /// @brief Register or update a named integer watch variable.
    /// @param name Label to display next to the value.
    /// @param value The integer value to display.
    void rt_debugoverlay_watch(rt_debugoverlay dbg, rt_string name, int64_t value);

    /// @brief Remove a named watch variable.
    /// @return 1 if found and removed, 0 if not found.
    int8_t rt_debugoverlay_unwatch(rt_debugoverlay dbg, rt_string name);

    /// @brief Clear all watch variables.
    void rt_debugoverlay_clear(rt_debugoverlay dbg);

    /// @brief Get the current computed FPS (rolling average).
    int64_t rt_debugoverlay_get_fps(rt_debugoverlay dbg);

    /// @brief Draw the overlay onto a canvas. Must be called after all other
    ///        rendering so the overlay appears on top.
    /// @param canvas_ptr Opaque pointer to an rt_canvas.
    void rt_debugoverlay_draw(rt_debugoverlay dbg, void *canvas_ptr);

#ifdef __cplusplus
}
#endif
