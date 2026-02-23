//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_smoothvalue.h
// Purpose: Smooth value interpolation for camera follow and UI animations, applying exponential smoothing each frame so the current value asymptotically approaches the target.
//
// Key invariants:
//   - Smoothing factor must be in [0, 1); values >= 1 cause undefined behavior.
//   - rt_smoothvalue_update must be called exactly once per frame.
//   - The value never exactly reaches the target; it asymptotically approaches it.
//   - Setting the value directly (rt_smoothvalue_set) snaps without smoothing.
//
// Ownership/Lifetime:
//   - Caller owns the handle; destroy with rt_smoothvalue_destroy.
//   - No reference counting; explicit destruction is required.
//
// Links: src/runtime/collections/rt_smoothvalue.c (implementation)
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_SMOOTHVALUE_H
#define VIPER_RT_SMOOTHVALUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Opaque handle to a SmoothValue instance.
    typedef struct rt_smoothvalue_impl *rt_smoothvalue;

    /// @brief Create a new SmoothValue.
    /// @param initial Initial value (both current and target are set to this).
    /// @param smoothing Smoothing factor (0.0 = instant, 1.0 = never moves).
    ///                  Typically 0.8-0.95 for pleasant animations.
    /// @return A new SmoothValue instance.
    rt_smoothvalue rt_smoothvalue_new(double initial, double smoothing);

    /// @brief Destroy a SmoothValue and free its memory.
    /// @param sv The smooth value to destroy.
    void rt_smoothvalue_destroy(rt_smoothvalue sv);

    /// @brief Get the current smoothed value.
    /// @param sv The smooth value.
    /// @return Current interpolated value.
    double rt_smoothvalue_get(rt_smoothvalue sv);

    /// @brief Get the current smoothed value as an integer.
    /// @param sv The smooth value.
    /// @return Current interpolated value (rounded to nearest integer).
    int64_t rt_smoothvalue_get_i64(rt_smoothvalue sv);

    /// @brief Get the target value.
    /// @param sv The smooth value.
    /// @return The target value that the current value is approaching.
    double rt_smoothvalue_target(rt_smoothvalue sv);

    /// @brief Set the target value (current value will smoothly approach it).
    /// @param sv The smooth value.
    /// @param target New target value.
    void rt_smoothvalue_set_target(rt_smoothvalue sv, double target);

    /// @brief Set both current and target value immediately (no smoothing).
    /// @param sv The smooth value.
    /// @param value New value applied to both current and target, bypassing
    ///              interpolation.
    void rt_smoothvalue_set_immediate(rt_smoothvalue sv, double value);

    /// @brief Get the smoothing factor.
    /// @param sv The smooth value.
    /// @return Smoothing factor in the range [0.0, 1.0).
    double rt_smoothvalue_smoothing(rt_smoothvalue sv);

    /// @brief Set the smoothing factor.
    /// @param sv The smooth value.
    /// @param smoothing New smoothing factor (0.0 = instant, 1.0 = never moves).
    void rt_smoothvalue_set_smoothing(rt_smoothvalue sv, double smoothing);

    /// @brief Update the smooth value by one frame.
    /// @details Call once per frame to advance the interpolation. The current
    ///          value moves toward the target by: current += (target - current)
    ///          * (1 - smoothing).
    /// @param sv The smooth value.
    void rt_smoothvalue_update(rt_smoothvalue sv);

    /// @brief Check if the value has reached the target (within epsilon).
    /// @param sv The smooth value.
    /// @return 1 if at target, 0 otherwise.
    int8_t rt_smoothvalue_at_target(rt_smoothvalue sv);

    /// @brief Get the velocity (rate of change per frame).
    /// @param sv The smooth value.
    /// @return Current velocity (difference between this frame and last).
    double rt_smoothvalue_velocity(rt_smoothvalue sv);

    /// @brief Add an impulse to the current value.
    /// @param sv The smooth value.
    /// @param impulse Value to add immediately to the current value
    ///                (does not change the target).
    void rt_smoothvalue_impulse(rt_smoothvalue sv, double impulse);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_SMOOTHVALUE_H
