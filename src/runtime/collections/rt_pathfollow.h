//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_pathfollow.h
/// @brief Path follower for moving objects along predefined waypoint paths.
///
/// @details Provides smooth movement along paths defined by waypoints with
/// linear interpolation between points. Supports one-shot, looping, and
/// ping-pong traversal modes, speed control, pause/resume, and progress
/// tracking. Coordinates and speed use fixed-point representation where
/// 1000 equals 1 unit, enabling sub-pixel precision without floating-point
/// arithmetic.
///
/// Key invariants: The maximum number of waypoints per path is
///   RT_PATHFOLLOW_MAX_POINTS (64). At least two waypoints must be added
///   before starting. Progress is in the range [0, 1000]. Fixed-point
///   convention: 1000 = 1.0 for coordinates, speed, and angles.
/// Ownership/Lifetime: The caller owns the rt_pathfollow handle and must
///   free it with rt_pathfollow_destroy().
/// Links: rt_pathfollow.c (implementation), rt_tween.h (easing functions
///   for curved paths)
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_PATHFOLLOW_H
#define VIPER_RT_PATHFOLLOW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// Maximum waypoints per path.
#define RT_PATHFOLLOW_MAX_POINTS 64

    /// Path following modes.
    typedef enum rt_pathfollow_mode
    {
        RT_PATHFOLLOW_ONCE = 0,     ///< Play once and stop at end.
        RT_PATHFOLLOW_LOOP = 1,     ///< Loop back to start.
        RT_PATHFOLLOW_PINGPONG = 2, ///< Reverse at endpoints.
    } rt_pathfollow_mode;

    /// Opaque handle to a PathFollower instance.
    typedef struct rt_pathfollow_impl *rt_pathfollow;

    /// @brief Allocates and initializes a new PathFollower with no waypoints.
    /// @return A new PathFollower handle. The caller must free it with
    ///   rt_pathfollow_destroy().
    rt_pathfollow rt_pathfollow_new(void);

    /// @brief Destroys a PathFollower and releases its memory.
    /// @param path The path follower to destroy. Passing NULL is a no-op.
    void rt_pathfollow_destroy(rt_pathfollow path);

    /// @brief Removes all waypoints from the path, resetting it to empty.
    /// @param path The path follower to clear.
    void rt_pathfollow_clear(rt_pathfollow path);

    /// @brief Appends a waypoint to the end of the path.
    /// @param path The path follower to modify.
    /// @param x X coordinate of the waypoint in fixed-point units (1000 = 1
    ///   world unit).
    /// @param y Y coordinate of the waypoint in fixed-point units (1000 = 1
    ///   world unit).
    /// @return 1 if the waypoint was added successfully, 0 if the path has
    ///   reached RT_PATHFOLLOW_MAX_POINTS and cannot accept more.
    int8_t rt_pathfollow_add_point(rt_pathfollow path, int64_t x, int64_t y);

    /// @brief Retrieves the number of waypoints currently in the path.
    /// @param path The path follower to query.
    /// @return The number of waypoints, in [0, RT_PATHFOLLOW_MAX_POINTS].
    int64_t rt_pathfollow_point_count(rt_pathfollow path);

    /// @brief Sets the traversal mode for the path follower.
    /// @param path The path follower to modify.
    /// @param mode Traversal mode: 0 = RT_PATHFOLLOW_ONCE (stop at end),
    ///   1 = RT_PATHFOLLOW_LOOP (jump back to start), 2 = RT_PATHFOLLOW_PINGPONG
    ///   (reverse direction at each endpoint).
    void rt_pathfollow_set_mode(rt_pathfollow path, int64_t mode);

    /// @brief Retrieves the current traversal mode.
    /// @param path The path follower to query.
    /// @return The mode as an integer matching the rt_pathfollow_mode enum.
    int64_t rt_pathfollow_get_mode(rt_pathfollow path);

    /// @brief Sets the movement speed along the path.
    /// @param path The path follower to modify.
    /// @param speed Speed in fixed-point units per second (1000 = 1 world unit
    ///   per second). Must be > 0.
    void rt_pathfollow_set_speed(rt_pathfollow path, int64_t speed);

    /// @brief Retrieves the current movement speed setting.
    /// @param path The path follower to query.
    /// @return Speed in fixed-point units per second.
    int64_t rt_pathfollow_get_speed(rt_pathfollow path);

    /// @brief Starts or resumes path traversal from the current position.
    ///
    /// Requires at least two waypoints to have been added. If called after
    /// rt_pathfollow_pause(), movement continues from where it left off.
    /// @param path The path follower to start.
    void rt_pathfollow_start(rt_pathfollow path);

    /// @brief Pauses path traversal at the current position.
    ///
    /// The follower retains its position and can be resumed with
    /// rt_pathfollow_start().
    /// @param path The path follower to pause.
    void rt_pathfollow_pause(rt_pathfollow path);

    /// @brief Stops traversal and resets the position to the first waypoint.
    /// @param path The path follower to stop.
    void rt_pathfollow_stop(rt_pathfollow path);

    /// @brief Queries whether the path follower is currently moving.
    /// @param path The path follower to query.
    /// @return 1 if the follower is actively traversing the path, 0 if paused
    ///   or stopped.
    int8_t rt_pathfollow_is_active(rt_pathfollow path);

    /// @brief Queries whether the path traversal has completed.
    ///
    /// Only meaningful in RT_PATHFOLLOW_ONCE mode; looping and ping-pong paths
    /// never finish.
    /// @param path The path follower to query.
    /// @return 1 if the follower has reached the last waypoint and stopped,
    ///   0 otherwise.
    int8_t rt_pathfollow_is_finished(rt_pathfollow path);

    /// @brief Advances the path follower by the given time delta.
    ///
    /// Moves the follower along the path at the configured speed. Must be
    /// called once per frame while the follower is active.
    /// @param path The path follower to update.
    /// @param dt Elapsed time since the last update in milliseconds.
    void rt_pathfollow_update(rt_pathfollow path, int64_t dt);

    /// @brief Retrieves the follower's current interpolated X position.
    /// @param path The path follower to query.
    /// @return X coordinate in fixed-point units (1000 = 1 world unit).
    int64_t rt_pathfollow_get_x(rt_pathfollow path);

    /// @brief Retrieves the follower's current interpolated Y position.
    /// @param path The path follower to query.
    /// @return Y coordinate in fixed-point units (1000 = 1 world unit).
    int64_t rt_pathfollow_get_y(rt_pathfollow path);

    /// @brief Retrieves the overall traversal progress as a fixed-point
    ///   fraction.
    /// @param path The path follower to query.
    /// @return Progress from 0 (at first waypoint) to 1000 (at last waypoint).
    int64_t rt_pathfollow_get_progress(rt_pathfollow path);

    /// @brief Sets the traversal progress directly, teleporting the follower
    ///   to the corresponding position on the path.
    /// @param path The path follower to modify.
    /// @param progress Progress value in [0, 1000]. Values outside this range
    ///   are clamped.
    void rt_pathfollow_set_progress(rt_pathfollow path, int64_t progress);

    /// @brief Retrieves the index of the path segment the follower is currently
    ///   traversing.
    /// @param path The path follower to query.
    /// @return Segment index in [0, point_count - 2]. A segment connects
    ///   waypoint[i] to waypoint[i+1].
    int64_t rt_pathfollow_get_segment(rt_pathfollow path);

    /// @brief Retrieves the current direction of travel as an angle.
    /// @param path The path follower to query.
    /// @return Direction angle in fixed-point degrees (1000 = 1 degree), in
    ///   the range [0, 360000). 0 = rightward, 90000 = downward.
    int64_t rt_pathfollow_get_angle(rt_pathfollow path);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_PATHFOLLOW_H
