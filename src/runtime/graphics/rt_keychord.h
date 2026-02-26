//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_keychord.h
// Purpose: Key chord (simultaneous) and combo (sequential) detector that recognizes registered
// named input patterns and reports which pattern fired each frame.
//
// Key invariants:
//   - Chord detection fires when all registered keys are pressed simultaneously.
//   - Combo detection fires when keys are pressed in the registered sequence within a time window.
//   - rt_keychord_update must be called once per frame with the current key state.
//   - Named patterns are unique; registering the same name twice replaces the previous binding.
//
// Ownership/Lifetime:
//   - Detector objects are heap-allocated opaque pointers.
//   - Caller is responsible for lifetime management.
//
// Links: src/runtime/graphics/rt_keychord.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new key chord/combo detector.
    /// @return Opaque detector handle.
    void *rt_keychord_new(void);

    /// @brief Register a chord (simultaneous key press).
    /// @param obj Detector handle.
    /// @param name Name for this chord (e.g., "copy").
    /// @param keys Seq of key codes (int64_t) that must be held together.
    void rt_keychord_define(void *obj, rt_string name, void *keys);

    /// @brief Register a combo (sequential key press with timing window).
    /// @param obj Detector handle.
    /// @param name Name for this combo (e.g., "hadouken").
    /// @param keys Seq of key codes in order.
    /// @param window_frames Max frames between consecutive keys.
    void rt_keychord_define_combo(void *obj, rt_string name, void *keys, int64_t window_frames);

    /// @brief Update detector state. Call once per frame after Canvas.Poll().
    /// @param obj Detector handle.
    void rt_keychord_update(void *obj);

    /// @brief Check if a chord is currently active (all keys held).
    /// @param obj Detector handle.
    /// @param name Chord name.
    /// @return 1 if active, 0 otherwise.
    int8_t rt_keychord_active(void *obj, rt_string name);

    /// @brief Check if a chord/combo was triggered this frame.
    /// @param obj Detector handle.
    /// @param name Chord or combo name.
    /// @return 1 if just triggered, 0 otherwise.
    int8_t rt_keychord_triggered(void *obj, rt_string name);

    /// @brief Get combo progress (number of keys matched so far).
    /// @param obj Detector handle.
    /// @param name Combo name.
    /// @return Number of keys matched (0 to N).
    int64_t rt_keychord_progress(void *obj, rt_string name);

    /// @brief Remove a named chord or combo.
    /// @param obj Detector handle.
    /// @param name Name to remove.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_keychord_remove(void *obj, rt_string name);

    /// @brief Remove all chords and combos.
    /// @param obj Detector handle.
    void rt_keychord_clear(void *obj);

    /// @brief Get the number of registered chords/combos.
    /// @param obj Detector handle.
    /// @return Count of registered entries.
    int64_t rt_keychord_count(void *obj);

#ifdef __cplusplus
}
#endif
