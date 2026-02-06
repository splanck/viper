//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_smoothvalue.h
/// @brief Smooth value interpolation for camera follow and UI animations.
///
/// Provides a value that smoothly interpolates toward a target using
/// exponential smoothing, useful for camera tracking, UI transitions,
/// and other cases where immediate changes would be jarring.
///
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

    /// Creates a new SmoothValue.
    /// @param initial Initial value.
    /// @param smoothing Smoothing factor (0.0 = instant, 1.0 = never moves). Typically 0.8-0.95.
    /// @return A new SmoothValue instance.
    rt_smoothvalue rt_smoothvalue_new(double initial, double smoothing);

    /// Destroys a SmoothValue and frees its memory.
    /// @param sv The smooth value to destroy.
    void rt_smoothvalue_destroy(rt_smoothvalue sv);

    /// Gets the current smoothed value.
    /// @param sv The smooth value.
    /// @return Current interpolated value.
    double rt_smoothvalue_get(rt_smoothvalue sv);

    /// Gets the current smoothed value as an integer.
    /// @param sv The smooth value.
    /// @return Current interpolated value (rounded).
    int64_t rt_smoothvalue_get_i64(rt_smoothvalue sv);

    /// Gets the target value.
    /// @param sv The smooth value.
    /// @return Target value.
    double rt_smoothvalue_target(rt_smoothvalue sv);

    /// Sets the target value (current value will smoothly approach it).
    /// @param sv The smooth value.
    /// @param target New target value.
    void rt_smoothvalue_set_target(rt_smoothvalue sv, double target);

    /// Sets both current and target value immediately (no smoothing).
    /// @param sv The smooth value.
    /// @param value New value.
    void rt_smoothvalue_set_immediate(rt_smoothvalue sv, double value);

    /// Gets the smoothing factor.
    /// @param sv The smooth value.
    /// @return Smoothing factor (0.0-1.0).
    double rt_smoothvalue_smoothing(rt_smoothvalue sv);

    /// Sets the smoothing factor.
    /// @param sv The smooth value.
    /// @param smoothing New smoothing factor (0.0 = instant, 1.0 = never moves).
    void rt_smoothvalue_set_smoothing(rt_smoothvalue sv, double smoothing);

    /// Updates the smooth value by one frame.
    /// Call once per frame to advance the interpolation.
    /// @param sv The smooth value.
    void rt_smoothvalue_update(rt_smoothvalue sv);

    /// Checks if the value has reached the target (within epsilon).
    /// @param sv The smooth value.
    /// @return 1 if at target, 0 otherwise.
    int8_t rt_smoothvalue_at_target(rt_smoothvalue sv);

    /// Gets the velocity (rate of change per frame).
    /// @param sv The smooth value.
    /// @return Current velocity.
    double rt_smoothvalue_velocity(rt_smoothvalue sv);

    /// Adds an impulse to the current value.
    /// @param sv The smooth value.
    /// @param impulse Value to add immediately.
    void rt_smoothvalue_impulse(rt_smoothvalue sv, double impulse);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_SMOOTHVALUE_H
