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
/// Provides smooth movement along paths defined by waypoints:
/// - Linear interpolation between points
/// - Optional looping and ping-pong modes
/// - Speed control and pause/resume
/// - Progress tracking
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_PATHFOLLOW_H
#define VIPER_RT_PATHFOLLOW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Maximum waypoints per path.
#define RT_PATHFOLLOW_MAX_POINTS 64

/// Path following modes.
typedef enum rt_pathfollow_mode
{
    RT_PATHFOLLOW_ONCE = 0,      ///< Play once and stop at end.
    RT_PATHFOLLOW_LOOP = 1,      ///< Loop back to start.
    RT_PATHFOLLOW_PINGPONG = 2,  ///< Reverse at endpoints.
} rt_pathfollow_mode;

/// Opaque handle to a PathFollower instance.
typedef struct rt_pathfollow_impl *rt_pathfollow;

/// Creates a new PathFollower.
/// @return A new PathFollower instance.
rt_pathfollow rt_pathfollow_new(void);

/// Destroys a PathFollower.
/// @param path The path follower to destroy.
void rt_pathfollow_destroy(rt_pathfollow path);

/// Clears all waypoints from the path.
/// @param path The path follower.
void rt_pathfollow_clear(rt_pathfollow path);

/// Adds a waypoint to the path.
/// @param path The path follower.
/// @param x X coordinate (fixed-point: 1000 = 1 unit).
/// @param y Y coordinate (fixed-point: 1000 = 1 unit).
/// @return 1 on success, 0 if path is full.
int8_t rt_pathfollow_add_point(rt_pathfollow path, int64_t x, int64_t y);

/// Gets the number of waypoints.
/// @param path The path follower.
/// @return Number of waypoints.
int64_t rt_pathfollow_point_count(rt_pathfollow path);

/// Sets the path mode.
/// @param path The path follower.
/// @param mode Path mode (0=once, 1=loop, 2=pingpong).
void rt_pathfollow_set_mode(rt_pathfollow path, int64_t mode);

/// Gets the current path mode.
/// @param path The path follower.
/// @return Path mode.
int64_t rt_pathfollow_get_mode(rt_pathfollow path);

/// Sets the movement speed.
/// @param path The path follower.
/// @param speed Speed in units per second (fixed-point: 1000 = 1 unit/sec).
void rt_pathfollow_set_speed(rt_pathfollow path, int64_t speed);

/// Gets the current movement speed.
/// @param path The path follower.
/// @return Speed in units per second.
int64_t rt_pathfollow_get_speed(rt_pathfollow path);

/// Starts or resumes path following.
/// @param path The path follower.
void rt_pathfollow_start(rt_pathfollow path);

/// Pauses path following.
/// @param path The path follower.
void rt_pathfollow_pause(rt_pathfollow path);

/// Stops and resets to the beginning.
/// @param path The path follower.
void rt_pathfollow_stop(rt_pathfollow path);

/// Checks if path following is active.
/// @param path The path follower.
/// @return 1 if active, 0 otherwise.
int8_t rt_pathfollow_is_active(rt_pathfollow path);

/// Checks if the path has finished (only for ONCE mode).
/// @param path The path follower.
/// @return 1 if finished, 0 otherwise.
int8_t rt_pathfollow_is_finished(rt_pathfollow path);

/// Updates the path follower.
/// @param path The path follower.
/// @param dt Delta time in milliseconds.
void rt_pathfollow_update(rt_pathfollow path, int64_t dt);

/// Gets the current X position.
/// @param path The path follower.
/// @return X coordinate (fixed-point: 1000 = 1 unit).
int64_t rt_pathfollow_get_x(rt_pathfollow path);

/// Gets the current Y position.
/// @param path The path follower.
/// @return Y coordinate (fixed-point: 1000 = 1 unit).
int64_t rt_pathfollow_get_y(rt_pathfollow path);

/// Gets the overall progress (0-1000).
/// @param path The path follower.
/// @return Progress from 0 (start) to 1000 (end).
int64_t rt_pathfollow_get_progress(rt_pathfollow path);

/// Sets the progress directly (0-1000).
/// @param path The path follower.
/// @param progress Progress value from 0 to 1000.
void rt_pathfollow_set_progress(rt_pathfollow path, int64_t progress);

/// Gets the current segment index.
/// @param path The path follower.
/// @return Current segment index (0 to point_count-2).
int64_t rt_pathfollow_get_segment(rt_pathfollow path);

/// Gets the direction angle in degrees (0-360).
/// @param path The path follower.
/// @return Angle in degrees (fixed-point: 1000 = 1 degree).
int64_t rt_pathfollow_get_angle(rt_pathfollow path);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_PATHFOLLOW_H
