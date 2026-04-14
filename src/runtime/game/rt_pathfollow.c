//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_pathfollow.c
// Purpose: Waypoint-based path follower for Viper games. Stores an ordered list
//   of 2D waypoints and advances an entity along the path at a configurable
//   speed. The follower interpolates linearly between consecutive waypoints,
//   computing the entity's exact world-space position and direction at any
//   point along the route. Typical uses: enemy patrol routes, projectile
//   trajectories, cutscene camera paths, and conveyor belts.
//
// Key invariants:
//   - IMPORTANT: All coordinates use a fixed-point scale of 1000 units = 1
//     world unit. Pass pixel-scale positions multiplied by 1000. For example,
//     a screen position of (320, 240) is stored as (320000, 240000). Failing
//     to scale results in the follower appearing to advance by a factor of 1000
//     too quickly.
//   - Speed is also in the same fixed-point scale (units × 1000 per frame).
//   - Progress is tracked as a floating-point accumulator of distance traveled.
//     Segment lengths are computed from the Euclidean distance between adjacent
//     waypoints (in fixed-point units).
//   - When the follower reaches the last waypoint it either stops (one-shot) or
//     wraps back to the first waypoint (looping), depending on the loop flag.
//   - Waypoints are stored in a malloc'd array that grows (realloc) when the
//     capacity is exceeded, doubling each time.
//
// Ownership/Lifetime:
//   - PathFollower objects are GC-managed (rt_obj_new_i64). The waypoint array
//     is malloc'd and freed by the GC finalizer. rt_pathfollow_destroy() is a
//     documented no-op for API symmetry.
//
// Links: src/runtime/game/rt_pathfollow.h (public API),
//        docs/viperlib/game.md (PathFollower section — coordinate scale note)
//
//===----------------------------------------------------------------------===//

#include "rt_pathfollow.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// Waypoint structure.
struct waypoint {
    int64_t x;
    int64_t y;
};

/// Internal path follower structure.
struct rt_pathfollow_impl {
    struct waypoint points[RT_PATHFOLLOW_MAX_POINTS];
    int64_t point_count;       ///< Number of waypoints.
    rt_pathfollow_mode_t mode; ///< Path mode.
    int64_t speed;             ///< Speed (units/sec, fixed-point).
    int8_t active;             ///< Is following active.
    int8_t finished;           ///< Has reached end (ONCE mode).
    int8_t reverse;            ///< Direction for PINGPONG.
    int64_t current_x;         ///< Current X position.
    int64_t current_y;         ///< Current Y position.
    int64_t segment;           ///< Current segment index.
    int64_t segment_progress;  ///< Progress within segment (0-1000).
    int64_t total_length;      ///< Total path length (cached).
    int64_t *segment_lengths;  ///< Cached segment lengths.
};

/// @brief Integer square root via Newton-Heron iteration. Returns floor(sqrt(n)) for n>=0,
/// or 0 for negative inputs. Used by `distance()` so the runtime stays libm-free.
static int64_t isqrt(int64_t n) {
    if (n < 0)
        return 0;
    if (n < 2)
        return n;

    int64_t x = n;
    int64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/// @brief Euclidean distance between two fixed-point points. Pre-scales each delta by 1/100 to
/// keep `dx*dx + dy*dy` inside i64 range even at large coordinates, then re-scales the isqrt
/// result by 100. Result is in the same fixed-point units (×1000) as the inputs.
static int64_t distance(int64_t x1, int64_t y1, int64_t x2, int64_t y2) {
    int64_t dx = x2 - x1;
    int64_t dy = y2 - y1;
    // Scale down to prevent overflow, then scale result back
    int64_t dx_scaled = dx / 100;
    int64_t dy_scaled = dy / 100;
    return isqrt(dx_scaled * dx_scaled + dy_scaled * dy_scaled) * 100;
}

/// @brief Rebuild the cached `segment_lengths` array and `total_length` from the waypoint list.
/// Called after every `add_point`. Frees and re-mallocs the array (no in-place realloc), so the
/// per-add cost is O(n). For paths with fewer than 2 points the cache is cleared.
static void recalculate_lengths(rt_pathfollow path) {
    if (!path)
        return;
    if (path->point_count < 2) {
        path->total_length = 0;
        return;
    }

    if (path->segment_lengths)
        free(path->segment_lengths);

    int64_t segments = path->point_count - 1;
    path->segment_lengths = malloc((size_t)segments * sizeof(int64_t));
    if (!path->segment_lengths) {
        path->total_length = 0;
        return;
    }

    path->total_length = 0;
    for (int64_t i = 0; i < segments; i++) {
        path->segment_lengths[i] = distance(
            path->points[i].x, path->points[i].y, path->points[i + 1].x, path->points[i + 1].y);
        path->total_length += path->segment_lengths[i];
    }
}

/// @brief GC finalizer: frees the malloc'd segment-length cache. The waypoint array is inline.
static void pathfollow_finalize(void *obj) {
    struct rt_pathfollow_impl *path = (struct rt_pathfollow_impl *)obj;
    if (path && path->segment_lengths)
        free(path->segment_lengths);
}

/// @brief Construct an empty PathFollower with no waypoints, ONCE mode, and a default speed of
/// 100 world units / second (i.e. 100000 in fixed-point). Caller adds points via `add_point`.
/// Returns a GC-managed handle wired to `pathfollow_finalize`; NULL on allocation failure.
rt_pathfollow rt_pathfollow_new(void) {
    struct rt_pathfollow_impl *path =
        (struct rt_pathfollow_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_pathfollow_impl));
    if (!path)
        return NULL;

    memset(path, 0, sizeof(struct rt_pathfollow_impl));
    path->speed = 100000; // Default: 100 units/sec
    path->mode = RT_PATHFOLLOW_ONCE;
    rt_obj_set_finalizer(path, pathfollow_finalize);
    return path;
}

/// @brief Release the PathFollower; frees the segment-length cache via the finalizer when the
/// refcount reaches zero. Documented as a no-op for API symmetry — GC handles cleanup.
void rt_pathfollow_destroy(rt_pathfollow path) {
    if (!path)
        return;

    if (rt_obj_release_check0(path))
        rt_obj_free(path);
}

/// @brief Reset the follower to a blank slate: no waypoints, no progress, inactive, freed cache.
/// Useful for re-using a PathFollower handle across multiple level loads.
void rt_pathfollow_clear(rt_pathfollow path) {
    if (!path)
        return;

    path->point_count = 0;
    path->segment = 0;
    path->segment_progress = 0;
    path->current_x = 0;
    path->current_y = 0;
    path->active = 0;
    path->finished = 0;
    path->reverse = 0;
    path->total_length = 0;

    if (path->segment_lengths) {
        free(path->segment_lengths);
        path->segment_lengths = NULL;
    }
}

/// @brief Append a waypoint at fixed-point world coords (x, y). The first added point also becomes
/// the follower's starting position. Returns 0 if the cap (`RT_PATHFOLLOW_MAX_POINTS`) is hit.
/// Triggers a full `recalculate_lengths` (O(n)) so the segment cache stays in sync.
int8_t rt_pathfollow_add_point(rt_pathfollow path, int64_t x, int64_t y) {
    if (!path)
        return 0;
    if (path->point_count >= RT_PATHFOLLOW_MAX_POINTS)
        return 0;

    path->points[path->point_count].x = x;
    path->points[path->point_count].y = y;
    path->point_count++;

    // Set initial position if this is the first point
    if (path->point_count == 1) {
        path->current_x = x;
        path->current_y = y;
    }

    recalculate_lengths(path);
    return 1;
}

/// @brief Number of waypoints currently in the path (0 to RT_PATHFOLLOW_MAX_POINTS).
int64_t rt_pathfollow_point_count(rt_pathfollow path) {
    return path ? path->point_count : 0;
}

/// @brief Select traversal mode: ONCE (stops at end), LOOP (wraps to start), PINGPONG (reverses).
/// Out-of-range values are silently ignored (mode is preserved).
void rt_pathfollow_set_mode(rt_pathfollow path, int64_t mode) {
    if (!path)
        return;
    if (mode >= RT_PATHFOLLOW_ONCE && mode <= RT_PATHFOLLOW_PINGPONG)
        path->mode = (rt_pathfollow_mode_t)mode;
}

/// @brief Read the current traversal mode (ONCE/LOOP/PINGPONG as integer constants).
int64_t rt_pathfollow_get_mode(rt_pathfollow path) {
    return path ? path->mode : 0;
}

/// @brief Set traversal speed in fixed-point world-units/second (×1000). Non-positive ignored.
/// Per-frame movement is computed as `(speed * dt_ms) / 1000`.
void rt_pathfollow_set_speed(rt_pathfollow path, int64_t speed) {
    if (path && speed > 0)
        path->speed = speed;
}

/// @brief Read the current speed in fixed-point world-units/second.
int64_t rt_pathfollow_get_speed(rt_pathfollow path) {
    return path ? path->speed : 0;
}

/// @brief Begin (or resume) following the path. Requires at least 2 waypoints; otherwise no-op.
/// Clears the `finished` flag so a previously completed ONCE path can be replayed.
void rt_pathfollow_start(rt_pathfollow path) {
    if (!path || path->point_count < 2)
        return;

    path->active = 1;
    path->finished = 0;
}

/// @brief Suspend updates while preserving segment + progress; resume with `start()`.
void rt_pathfollow_pause(rt_pathfollow path) {
    if (path)
        path->active = 0;
}

/// @brief Stop and rewind to the first waypoint (segment=0, progress=0). Waypoints are preserved.
/// Differs from `pause()` (which keeps current position) and `clear()` (which discards waypoints).
void rt_pathfollow_stop(rt_pathfollow path) {
    if (!path)
        return;

    path->active = 0;
    path->finished = 0;
    path->segment = 0;
    path->segment_progress = 0;
    path->reverse = 0;

    if (path->point_count > 0) {
        path->current_x = path->points[0].x;
        path->current_y = path->points[0].y;
    }
}

/// @brief Returns 1 while the follower is actively advancing each `update()` tick.
int8_t rt_pathfollow_is_active(rt_pathfollow path) {
    return path ? path->active : 0;
}

/// @brief Returns 1 once a ONCE-mode path has reached its final waypoint. Always 0 for LOOP/PINGPONG.
int8_t rt_pathfollow_is_finished(rt_pathfollow path) {
    return path ? path->finished : 0;
}

/// @brief Advance the follower by `dt` milliseconds. Spends `(speed * dt) / 1000` units of distance,
/// crossing segment boundaries as needed (a single update can traverse multiple short segments).
/// At end-of-path: ONCE → mark finished/inactive; LOOP → wrap to first segment; PINGPONG → flip
/// `reverse` flag and walk back. Recomputes `current_x/y` once at the end via linear interp of
/// `(segment, segment_progress)`. Inactive/finished/empty paths early-out.
void rt_pathfollow_update(rt_pathfollow path, int64_t dt) {
    if (!path || !path->active || path->finished || path->point_count < 2)
        return;

    if (!path->segment_lengths || path->total_length == 0)
        return;

    // Calculate distance to move this frame
    // speed is units/sec (fixed-point 1000), dt is ms
    int64_t move_dist = (path->speed * dt) / 1000;

    while (move_dist > 0 && !path->finished) {
        int64_t seg = path->segment;
        int64_t seg_len = path->segment_lengths[seg];

        // Calculate remaining distance in current segment
        int64_t seg_traveled = (seg_len * path->segment_progress) / 1000;
        int64_t seg_remaining = seg_len - seg_traveled;

        if (path->reverse) {
            // Moving backwards
            seg_remaining = seg_traveled;
        }

        if (move_dist >= seg_remaining) {
            // Move to next segment
            move_dist -= seg_remaining;

            if (path->reverse) {
                // Moving backwards
                if (seg > 0) {
                    path->segment--;
                    path->segment_progress = 1000;
                } else {
                    // Reached start
                    path->segment_progress = 0;
                    if (path->mode == RT_PATHFOLLOW_PINGPONG) {
                        path->reverse = 0;
                    } else if (path->mode == RT_PATHFOLLOW_LOOP) {
                        path->segment = path->point_count - 2;
                        path->segment_progress = 1000;
                    } else {
                        path->finished = 1;
                        path->active = 0;
                    }
                }
            } else {
                // Moving forward
                if (seg < path->point_count - 2) {
                    path->segment++;
                    path->segment_progress = 0;
                } else {
                    // Reached end
                    path->segment_progress = 1000;
                    if (path->mode == RT_PATHFOLLOW_PINGPONG) {
                        path->reverse = 1;
                    } else if (path->mode == RT_PATHFOLLOW_LOOP) {
                        path->segment = 0;
                        path->segment_progress = 0;
                    } else {
                        path->finished = 1;
                        path->active = 0;
                    }
                }
            }
        } else {
            // Partial movement within segment
            if (seg_len > 0) {
                int64_t progress_delta = (move_dist * 1000) / seg_len;
                if (path->reverse)
                    path->segment_progress -= progress_delta;
                else
                    path->segment_progress += progress_delta;

                // Clamp
                if (path->segment_progress < 0)
                    path->segment_progress = 0;
                if (path->segment_progress > 1000)
                    path->segment_progress = 1000;
            }
            move_dist = 0;
        }
    }

    // Update current position based on segment and progress
    int64_t seg = path->segment;
    int64_t p = path->segment_progress;
    int64_t x1 = path->points[seg].x;
    int64_t y1 = path->points[seg].y;
    int64_t x2 = path->points[seg + 1].x;
    int64_t y2 = path->points[seg + 1].y;

    path->current_x = x1 + ((x2 - x1) * p) / 1000;
    path->current_y = y1 + ((y2 - y1) * p) / 1000;
}

/// @brief Read the follower's current world X position (fixed-point ×1000). Caller divides by
/// 1000 to recover pixel coordinates.
int64_t rt_pathfollow_get_x(rt_pathfollow path) {
    return path ? path->current_x : 0;
}

/// @brief Read the follower's current world Y position (fixed-point ×1000).
int64_t rt_pathfollow_get_y(rt_pathfollow path) {
    return path ? path->current_y : 0;
}

/// @brief Compute total path progress as a 0–1000 fraction (per mille). Sums fully-traversed
/// segment lengths plus the partial distance into the current segment, divides by `total_length`.
/// Returns 0 for empty/unbuilt paths.
int64_t rt_pathfollow_get_progress(rt_pathfollow path) {
    if (!path || path->point_count < 2 || path->total_length == 0)
        return 0;

    // Calculate total distance traveled
    int64_t traveled = 0;
    for (int64_t i = 0; i < path->segment; i++) {
        traveled += path->segment_lengths[i];
    }
    traveled += (path->segment_lengths[path->segment] * path->segment_progress) / 1000;

    return (traveled * 1000) / path->total_length;
}

/// @brief Teleport the follower to a fractional path position (0–1000 per mille). Walks the
/// segment cache until the accumulated distance covers `target_dist`, then sets segment +
/// progress and updates `current_x/y`. Useful for cinematic seeks or save-state restoration.
void rt_pathfollow_set_progress(rt_pathfollow path, int64_t progress) {
    if (!path || path->point_count < 2 || path->total_length == 0)
        return;

    if (progress < 0)
        progress = 0;
    if (progress > 1000)
        progress = 1000;

    // Find the segment for this progress
    int64_t target_dist = (path->total_length * progress) / 1000;
    int64_t accumulated = 0;

    for (int64_t i = 0; i < path->point_count - 1; i++) {
        if (accumulated + path->segment_lengths[i] >= target_dist) {
            path->segment = i;
            int64_t seg_dist = target_dist - accumulated;
            if (path->segment_lengths[i] == 0)
                path->segment_progress = 0;
            else
                path->segment_progress = (seg_dist * 1000) / path->segment_lengths[i];
            break;
        }
        accumulated += path->segment_lengths[i];
    }

    // Update position
    int64_t seg = path->segment;
    int64_t p = path->segment_progress;
    path->current_x =
        path->points[seg].x + ((path->points[seg + 1].x - path->points[seg].x) * p) / 1000;
    path->current_y =
        path->points[seg].y + ((path->points[seg + 1].y - path->points[seg].y) * p) / 1000;
}

/// @brief Read the index of the segment the follower is currently traversing (0..point_count-2).
int64_t rt_pathfollow_get_segment(rt_pathfollow path) {
    return path ? path->segment : 0;
}

/// @brief Get the approximate direction angle of the current path segment.
/// @note Returns one of 8 cardinal/ordinal directions (0, 45, 90, ..., 315 degrees).
///       For smoother rotation, use atan2 on consecutive positions instead.
int64_t rt_pathfollow_get_angle(rt_pathfollow path) {
    if (!path || path->point_count < 2)
        return 0;

    int64_t seg = path->segment;
    int64_t dx = path->points[seg + 1].x - path->points[seg].x;
    int64_t dy = path->points[seg + 1].y - path->points[seg].y;

    if (path->reverse) {
        dx = -dx;
        dy = -dy;
    }

    // Approximate atan2 using lookup or simple quadrant detection
    // For simplicity, return angle in 8 cardinal/ordinal directions
    // More precise: use a lookup table or CORDIC

    if (dx == 0 && dy == 0)
        return 0;

    // Simple 8-direction approximation
    // Right=0, Down=90, Left=180, Up=270
    if (dx > 0 && dy == 0)
        return 0;
    if (dx > 0 && dy > 0)
        return 45000; // 45 degrees
    if (dx == 0 && dy > 0)
        return 90000; // 90 degrees
    if (dx < 0 && dy > 0)
        return 135000; // 135 degrees
    if (dx < 0 && dy == 0)
        return 180000; // 180 degrees
    if (dx < 0 && dy < 0)
        return 225000; // 225 degrees
    if (dx == 0 && dy < 0)
        return 270000; // 270 degrees
    if (dx > 0 && dy < 0)
        return 315000; // 315 degrees

    return 0;
}
